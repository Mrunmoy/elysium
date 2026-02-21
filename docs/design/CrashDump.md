# Crash Dump System -- Design Document

## Overview

Fault handler and diagnostic system for ms-os on ARM Cortex-M (STM32F207ZGT6,
STM32F407ZGT6) and future Cortex-A targets. When the CPU hits an unrecoverable
fault (HardFault, MemManage, BusFault, UsageFault), the system captures register
state, fault status, and thread context, then outputs a structured crash dump
via UART. A host-side Python tool monitors the serial port and automatically
translates addresses to source file:line using `arm-none-eabi-addr2line`.

## Three-Layer Architecture

The crash dump system is split into three layers to separate portable formatting
logic from CPU-specific and board-specific code:

| Layer | Directory | What varies | Example |
|-------|-----------|-------------|---------|
| **Common** | `kernel/src/core/` | Nothing -- pure formatting | crash dump structure, hex conversion |
| **Arch** | `kernel/src/arch/{cortex-m3,cortex-m4}/` | CPU core | SCB fault registers, stack frame layout, CFSR decoding, test fault instructions |
| **Board** | `kernel/src/board/{stm32f207zgt6,stm32f407zgt6}/` | SoC peripherals | UART init/output, LED blink, clock-based delay |

### Call Flow

```
FaultHandlers.s (arch assembly, unchanged)
    |
    v
faultHandlerC(stackFrame, excReturn)        [CrashDumpCommon.cpp]  COMMON
    |-- boardEnsureOutput()                  [CrashDumpBoard.cpp]   BOARD
    |-- archPopulateFaultInfo(info, ...)     [CrashDumpArch.cpp]    ARCH
    |-- faultPrint / faultPrintHex           [CrashDumpCommon.cpp]  COMMON
    |     \-- boardFaultPutChar()            [CrashDumpBoard.cpp]   BOARD
    |     \-- boardFaultFlush()              [CrashDumpBoard.cpp]   BOARD
    |-- archDecodeFaultBits(info)            [CrashDumpArch.cpp]    ARCH
    |-- boardFaultBlink()                    [CrashDumpBoard.cpp]   BOARD
```

### Files

| File | Layer | Purpose |
|------|-------|---------|
| `kernel/inc/kernel/CrashDump.h` | Public | FaultType enum, crashDumpInit(), triggerTestFault() |
| `kernel/inc/kernel/CrashDumpArch.h` | Public | FaultInfo struct, arch function declarations |
| `kernel/inc/kernel/CrashDumpBoard.h` | Public | Board output function declarations |
| `kernel/src/core/CrashDumpInternal.h` | Internal | faultPrint(), faultPrintHex() declarations |
| `kernel/src/core/CrashDumpCommon.cpp` | Common | Portable crash dump formatter (zero hardware addresses) |
| `kernel/src/arch/cortex-m3/CrashDumpArch.cpp` | Arch | M3 fault info population and CFSR/HFSR decoding |
| `kernel/src/arch/cortex-m4/CrashDumpArch.cpp` | Arch | M4 fault info population, CFSR/HFSR + MLSPERR decoding |
| `kernel/src/arch/cortex-m3/FaultHandlers.s` | Arch | Naked ASM entry points for fault exceptions |
| `kernel/src/arch/cortex-m4/FaultHandlers.s` | Arch | Naked ASM entry points for fault exceptions |
| `kernel/src/board/stm32f207zgt6/CrashDumpBoard.cpp` | Board | STM32F207 USART1/GPIO/LED output |
| `kernel/src/board/stm32f407zgt6/CrashDumpBoard.cpp` | Board | STM32F407 USART1/GPIO/LED output |
| `test/kernel/MockCrashDump.cpp` | Test | No-op stubs for host testing |
| `test/kernel/MockCrashDump.h` | Test | Mock state for crash dump output capture |
| `test/kernel/CrashDumpTest.cpp` | Test | Unit tests for formatting logic |

### CMake Integration

The root `CMakeLists.txt` defines `MSOS_BOARD_DIR` alongside `MSOS_ARCH_DIR`:

```cmake
# stm32f207zgt6 (default target)
set(MSOS_ARCH_DIR cortex-m3)
set(MSOS_BOARD_DIR stm32f207zgt6)

# stm32f407zgt6
set(MSOS_ARCH_DIR cortex-m4)
set(MSOS_BOARD_DIR stm32f407zgt6)
```

`kernel/CMakeLists.txt` uses both variables:

```cmake
add_library(kernel STATIC
    src/core/CrashDumpCommon.cpp
    src/arch/${MSOS_ARCH_DIR}/CrashDumpArch.cpp
    src/arch/${MSOS_ARCH_DIR}/FaultHandlers.s
    src/board/${MSOS_BOARD_DIR}/CrashDumpBoard.cpp
    ...
)
```

### FaultInfo Struct

Defined in `kernel/inc/kernel/CrashDumpArch.h`. This is the bridge between
arch-specific code (which populates it) and common code (which formats it):

```cpp
struct FaultInfo
{
    std::uint32_t pc;
    std::uint32_t lr;
    std::uint32_t sp;
    std::uint32_t r0, r1, r2, r3, r12;
    std::uint32_t statusReg;       // xPSR (Cortex-M) or CPSR (Cortex-A)

    std::uint32_t faultReg[4];     // M: CFSR, HFSR, MMFAR, BFAR
    const char *faultRegNames[4];  // "CFSR ", "HFSR ", etc.

    const char *faultType;         // "HardFault", "DataAbort", etc.
    std::uint32_t excInfo;         // EXC_RETURN (M) or exception mode (A)
};
```

### Arch Functions

Each architecture directory provides four functions:

```cpp
void archPopulateFaultInfo(FaultInfo &info, uint32_t *stackFrame, uint32_t excReturn);
void archDecodeFaultBits(const FaultInfo &info);
void archCrashDumpInit();
void archTriggerTestFault(FaultType type);
```

### Board Functions

Each board directory provides four functions:

```cpp
void boardEnsureOutput();       // Init output device if not ready
void boardFaultPutChar(char c); // Blocking single-char output
void boardFaultFlush();         // Wait for TX complete
[[noreturn]] void boardFaultBlink(); // Blink LED forever + halt
```

## Cortex-M3 Exception Model

### Why No Reserved RAM Region Is Needed

On Cortex-A/R processors, exception handling requires dedicated RAM regions
and manual register saves because:
- The CPU has banked registers per mode (FIQ, IRQ, SVC, ABT, UND)
- On exception entry, the CPU switches mode and bank-swaps SP, LR, SPSR
- The handler must manually save SPSR, switch back to the faulting mode,
  read out the faulting registers, and restore state
- A reserved RAM region is needed because the faulting mode's stack cannot
  be trusted

**Cortex-M3 is fundamentally different.** It has no traditional ARM modes.
Instead it uses a two-level privilege model (Handler mode vs Thread mode)
with hardware-managed exception entry. The NVIC handles all register saving
automatically before the first handler instruction executes.

### Hardware Automatic Stacking

When any exception (fault, interrupt, SysTick, SVC) occurs, the Cortex-M3
hardware performs these steps atomically before the handler runs:

1. **Determines the active stack pointer** -- MSP (Main Stack Pointer) if
   in Handler mode, or PSP (Process Stack Pointer) if in Thread mode with
   CONTROL.SPSEL=1.

2. **Pushes 8 registers** onto the active stack in this fixed order:

```
    Higher address
    +----------+
    |   xPSR   |  [SP + 28]   Processor status (includes faulting IPSR/flags)
    +----------+
    |    PC    |  [SP + 24]   Exact address of the faulting instruction
    +----------+
    |    LR    |  [SP + 20]   Return address of the function that faulted
    +----------+
    |   R12    |  [SP + 16]
    +----------+
    |    R3    |  [SP + 12]
    +----------+
    |    R2    |  [SP + 8]
    +----------+
    |    R1    |  [SP + 4]
    +----------+
    |    R0    |  [SP + 0]    <-- SP points here when handler starts
    +----------+
    Lower address
```

   This is the "caller-saved" subset defined by the ARM AAPCS calling
   convention. The hardware saves exactly the registers that a C function
   call would clobber, so the handler can be a normal C function.

3. **Sets LR to an EXC_RETURN magic value** that encodes:
   - Bit 2: which stack was active (0=MSP, 1=PSP)
   - Bit 3: thread vs handler mode return
   - Bit 4: FPU frame (not applicable on M3, always 1)

   Common values:
   - `0xFFFFFFF1` -- return to Handler mode, use MSP (nested exception)
   - `0xFFFFFFF9` -- return to Thread mode, use MSP
   - `0xFFFFFFFD` -- return to Thread mode, use PSP (typical for RTOS threads)

4. **Loads the handler address** from the vector table and begins execution
   in Handler mode using MSP.

### Cortex-M4 FPU Extended Frame

On Cortex-M4 with FPU enabled, when a context was using the FPU
(EXC_RETURN[4] == 0), the hardware pushes an extended 26-word frame:

```
    [SP + 0..7]   = R0, R1, R2, R3, R12, LR, PC, xPSR  (same as basic)
    [SP + 8..23]  = S0-S15
    [SP + 24]     = FPSCR
    [SP + 25]     = Reserved
```

The integer registers R0-xPSR are always at offsets [0..7] regardless of
whether the basic (8-word) or extended (26-word) frame was pushed. The
M4-specific CFSR bit 5 (MLSPERR) reports lazy FP stacking errors.

### How Our Fault Handler Uses This

Since the hardware has already saved the faulting context, our handler only
needs to:

1. **Read EXC_RETURN bit 2** to determine which stack has the frame:

```asm
    tst     lr, #4          @ Test bit 2 of EXC_RETURN
    ite     eq
    mrseq   r0, msp         @ Bit 2 = 0: frame is on MSP
    mrsne   r0, psp         @ Bit 2 = 1: frame is on PSP
```

2. **Pass the stack frame pointer to C** as the first argument (R0).
   The C handler indexes into it like a struct:

```cpp
    uint32_t r0   = stackFrame[0];
    uint32_t r1   = stackFrame[1];
    uint32_t r2   = stackFrame[2];
    uint32_t r3   = stackFrame[3];
    uint32_t r12  = stackFrame[4];
    uint32_t lr   = stackFrame[5];   // caller of faulting function
    uint32_t pc   = stackFrame[6];   // exact faulting instruction
    uint32_t xpsr = stackFrame[7];   // processor flags at fault time
```

3. **Read SCB registers** for fault details. These are memory-mapped and
   persist until explicitly cleared:
   - CFSR (0xE000ED28): combined MemManage + BusFault + UsageFault status
   - HFSR (0xE000ED2C): HardFault-specific status
   - MMFAR (0xE000ED34): faulting address for MemManage
   - BFAR (0xE000ED38): faulting address for BusFault

No reserved RAM, no mode switching, no manual register saving. The hardware
does all of it.

### What About R4-R11?

The hardware only pushes R0-R3, R12, LR, PC, xPSR (the "caller-saved" set).
R4-R11 are "callee-saved" and are NOT in the exception frame. This is fine
for crash diagnostics because:

- PC tells us the exact faulting instruction
- LR tells us who called the faulting function
- R0-R3 hold the first four function arguments
- R4-R11 are preserved across function calls anyway (the faulting function
  would have saved them on its own stack frame if it used them)

If full R4-R11 values are needed, they can be read directly in the assembly
stub (before branching to C) since the handler runs immediately and no
other code has modified them yet. Our current implementation does not do
this as PC + LR + fault registers are sufficient for diagnosis.

### Cortex-M3 vs Cortex-A/R Summary

| Feature | Cortex-M3 | Cortex-A/R |
|---------|-----------|------------|
| Exception modes | Handler + Thread (2) | FIQ, IRQ, SVC, ABT, UND, SYS (7+) |
| Banked registers | MSP/PSP only | SP, LR, SPSR per mode |
| Auto stack frame | Yes (8 registers) | No (manual save required) |
| Faulting PC | In stack frame [6] | In LR_abt (offset varies) |
| Stack trust | Frame on faulting thread's stack | Need dedicated abort stack |
| Reserved RAM | Not needed | Typically needed for safety |
| Handler can be C | Yes (directly) | Needs ASM wrapper for mode switching |

## Common Layer (CrashDumpCommon.cpp)

The common layer contains zero hardware addresses. It:

1. Calls `boardEnsureOutput()` to initialize the output device.
2. Calls `archPopulateFaultInfo()` to fill the `FaultInfo` struct.
3. Formats and prints the crash dump using `faultPrint()` and
   `faultPrintHex()`, which delegate to `boardFaultPutChar()` and
   `boardFaultFlush()`.
4. Calls `archDecodeFaultBits()` for arch-specific bit interpretation.
5. Calls `boardFaultBlink()` to halt with a visual indicator.

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

## Arch Layer (CrashDumpArch.cpp)

### Cortex-M3

- Reads SCB fault registers: CFSR, HFSR, MMFAR, BFAR
- Decodes MemManage, BusFault, UsageFault, HardFault bits
- Enables configurable fault handlers (SHCSR) and traps (CCR)
- Triggers test faults via Thumb-2 inline assembly (UDIV, bad pointer, 0xDEFE)

### Cortex-M4

Same as M3, plus:
- Decodes MLSPERR (CFSR bit 5) for lazy FP stacking errors
- Header comment documents FPU extended frame layout

### Fault Bit Decoding

CFSR sub-registers:

**MemManage (bits 0-7)**:
- Bit 0: IACCVIOL -- instruction access violation
- Bit 1: DACCVIOL -- data access violation
- Bit 3: MUNSTKERR -- unstacking error
- Bit 4: MSTKERR -- stacking error
- Bit 5: MLSPERR -- lazy FP stacking error (M4 only)
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
- Enables DIV_0_TRP in SCB->CCR for divide-by-zero detection
- Enables UNALIGN_TRP in SCB->CCR for unaligned access detection

## Board Layer (CrashDumpBoard.cpp)

### Exception-Safe UART

The crash dump must work even if UART was never initialized (e.g., fault
occurs during startup before main() configures peripherals). The handler
checks USART1->CR1.UE (UART enable bit) and if not set, performs minimal
direct register-level UART initialization:
- Enable GPIOA and USART1 clocks in RCC
- Configure PA9 as AF7 push-pull
- Set BRR for 115200 at APB2 clock rate
- Enable UE + TE in CR1

### LED Blink

After the crash dump is printed, `boardFaultBlink()` toggles PC13 (LED)
in a 250ms on / 250ms off pattern using a calibrated delay loop. This
provides a visual indication of a fault even without a serial connection.
The function is marked `[[noreturn]]` as it never returns.

### Board Differences

Both STM32F207 and STM32F407 use the same USART1 peripheral IP and GPIO
layout (PA9 = TX, PC13 = LED), so the board files are currently identical.
When boards diverge (e.g., different UART or LED pin), each file can be
edited independently.

## Porting Guide: Adding a New Board/Arch

### New Architecture (e.g., Cortex-A9)

1. Create `kernel/src/arch/cortex-a9/CrashDumpArch.cpp`
   - Implement `archPopulateFaultInfo()` reading DFSR, DFAR, IFSR, IFAR
   - Implement `archDecodeFaultBits()` for ARMv7-A fault model
   - Implement `archCrashDumpInit()` (set up exception vectors)
   - Implement `archTriggerTestFault()` with ARM-mode test faults
2. Create `kernel/src/arch/cortex-a9/FaultHandlers.s` with ARM-mode stubs
3. Add CMake target: `set(MSOS_ARCH_DIR cortex-a9)`
4. CrashDumpCommon.cpp is untouched

### New Board (e.g., PYNQ-Z2)

1. Create `kernel/src/board/pynq-z2/CrashDumpBoard.cpp`
   - Implement UART output via Xilinx PS UART (0xE0001000)
   - Implement LED blink via MIO GPIO
2. Add CMake target: `set(MSOS_BOARD_DIR pynq-z2)`
3. CrashDumpCommon.cpp and CrashDumpArch.cpp are untouched

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
- Compile real `CrashDumpCommon.cpp` in test build (not the mock)
- Mock `boardFaultPutChar()` to capture output into `test::g_crashOutput`
- Mock `archPopulateFaultInfo()` to populate FaultInfo from test data
- Mock `archDecodeFaultBits()` as no-op (tested via arch-specific tests)
- Mock `boardFaultBlink()` to return via `setjmp`/`longjmp` (it's `[[noreturn]]`)
- Test cases:
  - `faultPrintHex()` with known values (0, 0xDEADBEEF, 0xFFFFFFFF)
  - `faultHandlerC()` with known stack frame, assert formatted output
  - Thread context: verify thread name and ID appear when `g_currentTcb` is set
  - Null thread context: verify "(none)" when `g_currentTcb` is nullptr

### On-Target Test

`kernel::triggerTestFault(FaultType type)`:
- `DivideByZero`: inline asm `udiv` (C division is UB, GCC optimizes it away)
- `InvalidMemory`: `*reinterpret_cast<volatile uint32_t*>(0xCCCCCCCC) = 0xDEADBEEF;`
- `UndefinedInstruction`: inline `.short 0xDEFE` (Thumb permanently undefined)

### Host-Side Tool Tests

`test/tools/test_crash_monitor.py` (pytest):
- Feed known crash dump text, verify address extraction
- Mock addr2line subprocess, verify invocation args
- Verify colored output formatting
