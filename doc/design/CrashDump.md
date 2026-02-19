# Crash Dump System -- Design Document

## Overview

Fault handler and diagnostic system for ms-os on Cortex-M3 (STM32F207ZGT6).
When the CPU hits an unrecoverable fault (HardFault, MemManage, BusFault,
UsageFault), the system captures register state, fault status, and thread
context, then outputs a structured crash dump via UART. A host-side Python
tool monitors the serial port and automatically translates addresses to
source file:line using `arm-none-eabi-addr2line`.

## Architecture

```
TARGET (Cortex-M3)                         HOST (Python)
-------------------                        -------------

Fault occurs
  |
  v
FaultHandlers.s                            tools/crash_monitor.py
  naked ASM wrapper                          |
  detect MSP vs PSP                          | monitors serial port
  pass stack frame ptr                       | detects crash markers
  to C handler                               | extracts addresses
  |                                          | runs addr2line
  v                                          | prints decoded output
CrashDump.cpp                               |
  extract R0-R3,R12,LR,PC,xPSR              v
  read SCB: CFSR,HFSR,MMFAR,BFAR       Terminal with colored output
  read thread context from g_currentTcb
  format and print via polled UART
  blink LED + halt (debug) or reset (release)
```

## Target-Side Design

### Files

| File | Purpose |
|------|---------|
| `kernel/src/arch/cortex-m3/FaultHandlers.s` | Naked ASM entry points for HardFault, MemManage, BusFault, UsageFault |
| `kernel/src/core/CrashDump.cpp` | C++ handler: register extraction, fault decoding, UART output |
| `kernel/inc/kernel/CrashDump.h` | Public header: init function, extern "C" handler declaration |

### Assembly Stubs (FaultHandlers.s)

All four fault handlers use the same pattern:

```asm
HardFault_Handler:
    tst     lr, #4          @ Test EXC_RETURN bit 2
    ite     eq
    mrseq   r0, msp         @ Bit 2 = 0: fault used MSP (handler mode)
    mrsne   r0, psp         @ Bit 2 = 1: fault used PSP (thread mode)
    mov     r1, lr          @ Pass EXC_RETURN as second arg
    b       faultHandlerC   @ Branch to C handler
```

Key points:
- No function prologue (naked) -- preserves LR = EXC_RETURN
- Uses `.global` to override weak symbols in Startup.s
- `--whole-archive` linkage (already configured) ensures override
- All four handlers branch to the same C function `faultHandlerC`

### C Handler (CrashDump.cpp)

```cpp
extern "C" void faultHandlerC(std::uint32_t *stackFrame, std::uint32_t excReturn);
```

The handler:

1. **Extracts stacked registers** from the hardware exception frame:
   - stackFrame[0..7] = R0, R1, R2, R3, R12, LR, PC, xPSR

2. **Reads SCB fault status registers**:
   - CFSR (0xE000ED28): MemManage + BusFault + UsageFault combined
   - HFSR (0xE000ED2C): HardFault status
   - MMFAR (0xE000ED34): MemManage fault address (if MMARVALID)
   - BFAR (0xE000ED38): BusFault address (if BFARVALID)

3. **Reads thread context** from `g_currentTcb`:
   - Thread ID, name, stack base, stack size

4. **Outputs structured crash dump** via polled UART:
   - Uses `hal::uartWriteString()` (polling, no interrupts, no heap)
   - If UART not initialized, performs direct register-level init

5. **Post-output behavior**:
   - Blink LED on PC13 for visual indication
   - Infinite loop (halt for debugger)

### Crash Dump Output Format

Machine-parseable format with clear markers:

```
=== CRASH DUMP BEGIN ===
Fault: HardFault (FORCED)
Thread: led (id=1)
Registers:
  PC : 08000ABC
  LR : 08000A34
  SP : 200007B8
  R0 : 00000000
  R1 : 00000001
  R2 : 20000400
  R3 : 00000000
  R12: 00000000
  xPSR: 61000000
Fault Status:
  CFSR: 00008200
  HFSR: 40000000
  MMFAR: 00000000
  BFAR: CCCCCCCC
Decoded:
  -> FORCED: Escalated to HardFault
  -> PRECISERR: Precise data bus error
  -> BFARVALID: BFAR = CCCCCCCC
Stack: base=20000400 size=1024
EXC_RETURN: FFFFFFFD
=== CRASH DUMP END ===
```

Key design choices:
- `=== CRASH DUMP BEGIN ===` / `=== CRASH DUMP END ===` markers for host tool parsing
- Register values as 8-char hex (no 0x prefix, fixed width) for easy regex matching
- Decoded fault bits as human-readable strings prefixed with `  -> `
- Thread name and ID for identifying which thread faulted

### Fault Bit Decoding

CFSR sub-registers:

**MemManage (bits 0-7)**:
- Bit 0: IACCVIOL -- instruction access violation
- Bit 1: DACCVIOL -- data access violation
- Bit 7: MMARVALID -- MMFAR holds valid address

**BusFault (bits 8-15)**:
- Bit 8: IBUSERR -- instruction bus error
- Bit 9: PRECISERR -- precise data bus error
- Bit 10: IMPRECISERR -- imprecise data bus error
- Bit 11: UNSTKERR -- unstacking error
- Bit 12: STKERR -- stacking error
- Bit 15: BFARVALID -- BFAR holds valid address

**UsageFault (bits 16-31)**:
- Bit 16: UNDEFINSTR -- undefined instruction
- Bit 17: INVSTATE -- invalid EPSR.T bit (ARM mode on Thumb-only CPU)
- Bit 18: INVPC -- invalid EXC_RETURN
- Bit 19: NOCP -- coprocessor access (none on M3)
- Bit 24: UNALIGNED -- unaligned access
- Bit 25: DIVBYZERO -- divide by zero (requires SCB->CCR.DIV_0_TRP)

HFSR:
- Bit 1: VECTTBL -- vector table read error
- Bit 30: FORCED -- escalated from configurable fault

### Initialization

`kernel::crashDumpInit()` called from `kernel::init()`:
- Enables MemManage, BusFault, and UsageFault handlers in SCB->SHCSR
  (otherwise they all escalate to HardFault)
- Optionally enables DIV_0_TRP in SCB->CCR for divide-by-zero detection
- Enables UNALIGN_TRP in SCB->CCR for unaligned access detection

### Exception-Safe UART

The crash dump must work even if UART was never initialized (e.g., fault
occurs during startup before main() configures peripherals). The handler
checks USART1->CR1.UE (UART enable bit) and if not set, performs minimal
direct register-level UART initialization:
- Enable GPIOA and USART1 clocks in RCC
- Configure PA9 as AF7 push-pull
- Set BRR for 115200 at APB2=60MHz
- Enable UE + TE in CR1

## Host-Side Design

### File

`tools/crash_monitor.py` -- standalone Python script, no pip install needed.

### Features (inspired by ESP-IDF idf_monitor and STM32F407 crash_monitor.py)

1. **Serial monitoring**: connects to serial port, displays all output
2. **Crash detection**: watches for `=== CRASH DUMP BEGIN ===` marker
3. **Address extraction**: regex matches 8-char hex values after PC/LR labels
4. **addr2line integration**: runs `arm-none-eabi-addr2line -fiaC -e <elf>`
5. **Colored output**: crash dump in red, decoded addresses in green
6. **Timestamp**: prepends timestamp to each line
7. **Reconnect**: auto-reconnects if serial port disconnects

### Usage

```bash
python3 tools/crash_monitor.py --port /dev/ttyUSB0 --baud 115200 --elf build/app/threads/threads
```

### Address Decoding

When a crash dump is detected, the tool:
1. Collects all lines between BEGIN/END markers
2. Extracts PC and LR hex values
3. Runs: `arm-none-eabi-addr2line -fiaC -e <elf> <pc> <lr>`
4. Prints decoded output:
   ```
   === Decoded Crash Location ===
   PC: 0x08000ABC -> app_main() at app/threads/main.cpp:42
   LR: 0x08000A34 -> kernel::yield() at kernel/src/core/Kernel.cpp:102
   ```

## Testing Strategy

### Host-Side Unit Tests

`test/kernel/CrashDumpTest.cpp`:
- Mock UART output (record strings written)
- Call `faultHandlerC()` with known stack frames
- Assert output contains expected register values
- Assert fault bit decoding is correct
- Assert thread context (name, ID) is included

### On-Target Test

`kernel::triggerTestFault(FaultType type)`:
- `DivideByZero`: `volatile int x = 0; return 1/x;`
- `InvalidMemory`: `*reinterpret_cast<volatile int*>(0xCCCCCCCC) = 0;`
- `UndefinedInstruction`: inline `.word 0xFFFFFFFF`

### Host-Side Tool Tests

`test/tools/test_crash_monitor.py` (pytest):
- Feed known crash dump text, verify address extraction
- Mock addr2line subprocess, verify invocation args
- Verify colored output formatting

## Integration

### CMake Changes

`kernel/CMakeLists.txt`:
```cmake
add_library(kernel STATIC
    ...existing files...
    src/arch/cortex-m3/FaultHandlers.s
    src/core/CrashDump.cpp
)

set_source_files_properties(src/arch/cortex-m3/FaultHandlers.s PROPERTIES
    LANGUAGE ASM
)
```

### Kernel Init

`kernel::init()` calls `crashDumpInit()` to enable configurable fault
handlers before any threads are created.

### No Changes Needed

- Startup.s: fault handlers are weak, overridden by --whole-archive
- Linker.ld: no new sections needed
- App code: no changes (crash dump is automatic on any fault)
