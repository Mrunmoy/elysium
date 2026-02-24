# Phase 4: Multi-Target (Cortex-A9 Port)

## Overview

Phase 4 ports ms-os to the PYNQ-Z2 development board (Xilinx Zynq-7020 SoC,
dual Cortex-A9 @ 650 MHz, 512 MB DDR3). This required replacing all
Cortex-M-specific kernel and HAL code with architecture-agnostic abstractions,
implementing a GIC v1 interrupt controller driver, an SCU private timer for
the system tick, IRQ-based context switching (no PendSV on A-profile), a
Zynq PS UART driver, and a Cortex-A9 crash dump system using CP15 fault
status registers.

---

## Architecture Abstraction (Arch.h)

The original `CortexM.h` was renamed to `Arch.h` and redesigned as an
architecture-agnostic interface. Each architecture provides its own
implementation in a separate directory selected at build time by CMake.

```cpp
namespace kernel::arch
{
    void triggerContextSwitch();         // PendSV (M) or SGI #0 (A9)
    void configureSysTick(uint32_t);     // SysTick (M) or SCU timer (A9)
    void enterCritical();                // cpsid i
    void exitCritical();                 // cpsie i
    void startFirstThread();             // SVC #0 on both
    void setInterruptPriorities();       // NVIC (M) or GIC (A9)
    uint32_t initialStatusRegister();    // xPSR (M) or CPSR (A9)
    bool inIsrContext();                 // VECTACTIVE (M) or CPSR mode (A9)
    void waitForInterrupt();             // wfi
}
```

### Initial Status Register

Threads need an initial status register value on the stack frame so that
`startFirstThread` and context switch restore can atomically enter the
correct CPU state.

| Architecture | Value | Meaning |
|---|---|---|
| Cortex-M3/M4 | `0x01000000` | xPSR with Thumb bit set (T=1) |
| Cortex-A9 | `0x0000001F` | CPSR: SYS mode, ARM state, IRQ/FIQ enabled |

### ISR Context Detection

| Architecture | Method |
|---|---|
| Cortex-M | `(SCB->ICSR & VECTACTIVE) != 0` |
| Cortex-A9 | `CPSR[4:0] != 0x10 (USR) && CPSR[4:0] != 0x1F (SYS)` |

---

## Target Hardware: PYNQ-Z2

| Parameter | Value |
|---|---|
| SoC | Xilinx Zynq-7020 (XC7Z020-1CLG400C) |
| CPU | Dual Cortex-A9 MPCore @ 650 MHz |
| RAM | 512 MB DDR3 |
| UART | PS UART0 on MIO 14/15 (Cadence IP, 0xE0000000) |
| UART ref clock | 100 MHz (IO PLL) |
| LEDs | On PL fabric (not accessible without bitstream) |
| JTAG | FTDI FT2232 (VID:PID 0403:6010) |
| Debug tool | OpenOCD via FTDI channel |
| Serial port | /dev/ttyUSB1 or /dev/ttyUSB2 at 115200 baud |

Only CPU0 is used. CPU1 is parked in a WFE loop at boot (checked via
the MPIDR register).

### Compiler Flags

```
-mcpu=cortex-a9  -marm  -mfpu=vfpv3-d16  -mfloat-abi=softfp  -fno-exceptions  -fno-rtti
```

ARM mode (`-marm`) is used throughout. No Thumb interworking.

---

## Memory Layout (DDR)

Everything is loaded into DDR at 0x00100000. The first 1 MB is reserved
by the Zynq BootROM/OCM. For JTAG development, u-boot initializes DDR
and PLLs, so VMA == LMA and no `.data` copy is needed at startup.

```
DDR: 0x00100000 (511 MB)
  +------------------+  0x00100000
  | .vectors         |  Exception vector table (8 entries)
  | .text.boot       |  Boot code (runs before MMU/C setup)
  | .text            |  Program code
  | .rodata          |  Read-only data
  | .ARM.extab/exidx |  Exception handling tables
  | .init_array      |  C++ static constructors
  | .data            |  Initialized globals (in DDR, no copy needed)
  +------------------+
  | .bss             |  Uninitialized globals (zeroed at boot)
  +------------------+
  | .stacks          |  Per-mode exception stacks
  |   IRQ    1K      |
  |   SVC    2K      |
  |   ABT    1K      |
  |   FIQ    1K      |
  |   UND    1K      |
  |   SYS    4K      |
  +------------------+
  | .mmu_table (16K) |  L1 translation table (16K-aligned)
  +------------------+
  | .heap (16K)      |  Dynamic allocation heap
  +------------------+
```

### Per-Mode Stacks

ARM Cortex-A9 has banked SP registers per exception mode. Each mode
needs its own stack, configured in Startup.s via CPS mode switches:

| Mode | CPSR[4:0] | Stack Size | Purpose |
|---|---|---|---|
| IRQ | 0x12 | 1 KB | IRQ handler + context switch |
| SVC | 0x13 | 2 KB | SVC handler (first thread launch) |
| ABT | 0x17 | 1 KB | Data/Prefetch Abort handlers |
| FIQ | 0x11 | 1 KB | FIQ handler (unused, reserved) |
| UND | 0x1B | 1 KB | Undefined Instruction handler |
| SYS | 0x1F | 4 KB | Pre-main system stack |

Thread stacks are separate from these mode stacks. Threads run in SYS
mode (0x1F) with their own stack pointer, set during context switch.

---

## Boot Sequence (Startup.s)

```
_vector_table (8 ARM branch instructions)
  |
_boot:
  1. cpsid if                   -- Disable IRQ/FIQ
  2. MPIDR check                -- CPU1 -> _cpu1_wait (WFE loop)
  3. Set VBAR                   -- Point exceptions to our vector table
  4. Disable MMU/caches         -- Clean slate after FSBL/u-boot
  5. Invalidate TLBs, I-cache, branch predictor
  6. Invalidate L1 D-cache      -- Iterate 4 ways x 256 sets (DCISW)
  7. Disable + invalidate L2    -- PL310 at 0xF8F02100 (stale JTAG data)
  8. _setup_mmu                 -- Flat 1:1 L1 page table, enable MMU
  9. Set per-mode stacks        -- CPS to each mode, load SP from linker
  10. Zero .bss
  11. __libc_init_array          -- C++ static constructors
  12. SystemInit()               -- Set clock globals
  13. main()
```

### MMU Setup

The MMU must be enabled before any C/C++ code runs. With MMU disabled,
all memory on Cortex-A9 is Strongly Ordered, which requires natural
alignment for all accesses. GCC emits unaligned stack accesses in normal
C code, causing alignment faults.

A flat 1:1 L1 translation table is built (4096 x 1 MB section entries):

| Address Range | Sections | Attributes |
|---|---|---|
| 0x00000000 - 0x1FFFFFFF | 0-511 | Normal, Inner/Outer WB-WA, Shareable |
| 0x20000000 - 0xFFFFFFFF | 512-4095 | Device, Shareable |

Section descriptor format (ARMv7-A short descriptor):
- Normal (DDR): `0x00011C0E` -- TEX=001 C=1 B=1 S=1 AP=11
- Device (peripherals): `0x00000C06` -- TEX=000 C=0 B=1 AP=11

DACR is set to 0xFFFFFFFF (all domains = Manager, no permission checks).

### L2 Cache Invalidation

Required for JTAG reload workflows. When a new binary is written to DRAM
via the debug port, the L2 cache (PL310 controller at 0xF8F02000) may
hold stale code/data from the previous binary. Startup.s disables the L2
controller and invalidates all 8 ways before enabling the MMU.

### SystemInit

Minimal: sets clock globals only. PLLs and DDR are already configured
by u-boot (JTAG development model).

```cpp
uint32_t SystemCoreClock = 650000000;   // CPU 650 MHz (CPU_3x2x)
uint32_t g_apb1Clock     = 100000000;   // UART ref clock (IO PLL)
uint32_t g_apb2Clock     = 100000000;
```

SLCR is unlocked (key 0xDF0D) for peripheral register access.

---

## GIC v1 (Generic Interrupt Controller)

The Zynq-7020 GIC sits in the Cortex-A9 MPCore private peripheral region
at 0xF8F00000.

### Register Map

| Register | Address | Purpose |
|---|---|---|
| ICDDCR | 0xF8F01000 | Distributor control (enable) |
| ICDISER0 | 0xF8F01100 | Interrupt set-enable (IDs 0-31) |
| ICDIPR | 0xF8F01400 | Interrupt priority (byte per ID) |
| ICDSGIR | 0xF8F01F00 | Software Generated Interrupt |
| ICCICR | 0xF8F00100 | CPU interface control |
| ICCPMR | 0xF8F00104 | Priority mask |
| ICCBPR | 0xF8F00108 | Binary point |
| ICCIAR | 0xF8F0010C | Interrupt acknowledge |
| ICCEOIR | 0xF8F00110 | End of interrupt |

### Initialization (setInterruptPriorities)

```cpp
void setInterruptPriorities()
{
    // Distributor: enable secure + non-secure
    reg(ICDDCR) = 0x3;

    // Enable SGI 0 (context switch), set priority 0xF0 (lowest usable)
    reg(ICDISER0) |= (1 << 0);
    ICDIPR[0] = 0xF0;

    // CPU interface: priority mask 0xFF (allow all), binary point 0
    reg(ICCPMR) = 0xFF;
    reg(ICCBPR) = 0;
    reg(ICCICR) = 0x3;
}
```

### Interrupt IDs

| ID | Source | Priority | Purpose |
|---|---|---|---|
| 0 | SGI #0 | 0xF0 (lowest) | Context switch trigger |
| 29 | Private Timer | 0xA0 (mid) | System tick (1 ms) |

SGI #0 replaces PendSV as the context switch mechanism. It is the
lowest-priority interrupt, ensuring all higher-priority interrupts
(including the tick) complete before the context switch runs.

---

## SCU Private Timer (System Tick)

Replaces Cortex-M SysTick. Clocks at PERIPHCLK = CPU_CLK / 2 = 325 MHz.

| Register | Offset from 0xF8F00600 | Purpose |
|---|---|---|
| LOAD | +0x00 | Reload value |
| COUNTER | +0x04 | Current count (down-counter) |
| CONTROL | +0x08 | Enable, auto-reload, IRQ enable |
| ISR | +0x0C | Interrupt status (write 1 to clear) |

### Configuration

```cpp
void configureSysTick(uint32_t ticks)
{
    // Caller passes SystemCoreClock/1000 (based on CPU_CLK).
    // Halve for PERIPHCLK.
    uint32_t loadVal = (ticks / 2) - 1;

    reg(CONTROL) = 0;              // Stop
    reg(LOAD)    = loadVal;        // Set reload
    reg(ISR)     = 1;              // Clear pending
    reg(CONTROL) = ENABLE | AUTO_RELOAD | IRQ_ENABLE;

    // Enable ID 29 in GIC distributor, priority 0xA0
    reg(ICDISER0) |= (1 << 29);
    ICDIPR[29] = 0xA0;
}
```

The timer ISR (`PrivateTimer_Handler`) clears the event flag by writing
1 to the ISR register. The IRQ dispatcher then calls `SysTick_Handler`
(the same portable tick handler used on Cortex-M).

---

## Context Switch (ContextSwitch.s)

### Stack Frame Layout (16 words, 64 bytes)

Identical to the Cortex-M layout for portability of `threadCreate()`:

```
[SP+0  .. SP+7 ]  r4-r11        (software-saved by IRQ handler)
[SP+8  .. SP+13]  r0-r3, r12, LR (software-saved on IRQ entry)
[SP+14]           PC             (return address from SRS)
[SP+15]           CPSR           (saved status from SRS)
```

On Cortex-M, the hardware pushes r0-r3/r12/LR/PC/xPSR automatically.
On Cortex-A9, the IRQ handler does this in software using SRS + PUSH.

### IRQ Handler Flow

```
IRQ_Handler:
  sub     lr, lr, #4            // Adjust LR (ARM pipeline offset)
  srsdb   sp!, #0x1F            // Save {LR_irq, SPSR_irq} to SYS stack
  cps     #0x1F                 // Switch to SYS mode (thread's SP)
  push    {r0-r3, r12, lr}      // Save caller-saved + thread LR

  // CRITICAL: Do NOT use r4-r11 here. They belong to the
  // interrupted thread and are saved only during context switch.

  // Acknowledge interrupt (read ICCIAR)
  ldr     r0, =GIC_CPU_BASE
  ldr     r1, [r0, #ICCIAR]
  push    {r1}                   // Save ICCIAR value for EOI
  ubfx    r0, r1, #0, #10       // Extract interrupt ID (bits 9:0)

  // Dispatch by interrupt ID
  cmp     r0, #29               // Private Timer?
  beq     .Ltimer_irq           //   -> PrivateTimer_Handler + SysTick_Handler
  cmp     r0, #0                // SGI 0 (context switch)?
  beq     .Lsgi_context_switch  //   -> (no action, switch in epilogue)

  // EOI
  pop     {r1}
  str     r1, [r0, #ICCEOIR]

  // Context switch check
  ldr     r2, [=g_currentTcb]
  ldr     r3, [=g_nextTcb]
  cmp     r2, r3
  beq     .Lno_switch

  // Save outgoing: push r4-r11, store SP in TCB
  push    {r4-r11}
  str     sp, [r2, #0]

  // Load incoming: update g_currentTcb, load SP, pop r4-r11
  str     r3, [r0]              // g_currentTcb = g_nextTcb
  ldr     sp, [r3, #0]
  pop     {r4-r11}

.Lno_switch:
  pop     {r0-r3, r12, lr}
  rfeia   sp!                    // Restore PC + CPSR atomically
```

### Key Differences from Cortex-M

| Aspect | Cortex-M (PendSV) | Cortex-A9 (IRQ) |
|---|---|---|
| Trigger | Set PendSV pending bit | SGI #0 via ICDSGIR |
| HW frame push | Automatic (8 words) | Manual (SRS + PUSH) |
| Frame restore | EXC_RETURN in LR | RFE instruction |
| Mode on entry | Handler mode (MSP) | IRQ mode (banked) |
| Thread SP | PSP (CONTROL.SPSEL=1) | SYS mode SP (banked) |
| Register safety | r4-r11 untouched by HW | Must avoid r4-r11 before save |
| Context switch timing | Tail-chained after ISR | End of IRQ handler |

### Register Safety

The IRQ handler must not clobber r4-r11 before the context switch saves
them. An earlier bug used r4/r5 for GIC base address and ICCIAR value;
on context switch, the outgoing thread's callee-saved registers were
corrupted. The fix uses only r0-r3 (already saved) and the stack for
temporaries.

### SVC Handler (First Thread Launch)

```asm
SVC_Handler:
  ldr     sp, [g_currentTcb->stackPointer]
  pop     {r4-r11}
  pop     {r0-r3, r12, lr}
  rfeia   sp!                    // Enter first thread in SYS mode
```

No outgoing context to save. RFE atomically loads PC and CPSR from the
stack, switching from SVC mode to SYS mode with IRQs enabled.

### Context Switch Trigger

```cpp
void triggerContextSwitch()
{
    // ICDSGIR: target list filter=0, CPU target=bit 0, SGI ID=0
    reg(ICDSGIR) = (1u << 16) | 0;
}
```

---

## HAL: Zynq-7000 Drivers

### PS UART (hal/src/zynq7000/Uart.cpp)

Cadence UART IP at 0xE0000000 (UART0) and 0xE0001000 (UART1). The PYNQ-Z2
UART0 connects to the FTDI USB-serial chip on MIO 14/15.

| Register | Offset | Purpose |
|---|---|---|
| CR | 0x00 | Control (TX/RX enable/disable/reset) |
| MR | 0x04 | Mode (8N1, clock source) |
| BRGR | 0x18 | Baud rate generator (CD divider) |
| SR | 0x2C | Channel status (FIFO flags) |
| FIFO | 0x30 | TX/RX data FIFO |
| BDIV | 0x34 | Baud rate divider |

Baud rate formula: `baud = uart_ref_clk / (CD * (BDIV + 1))`

With BDIV=4 and uart_ref_clk=100 MHz: `CD = 100M / (115200 * 5) = 173`

Thread-safe output uses CPSR save/restore guards (not PRIMASK as on
Cortex-M):

```cpp
uint32_t disableIrq()
{
    uint32_t cpsr;
    __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    __asm volatile("cpsid i" ::: "memory");
    return cpsr;
}

void restoreIrq(uint32_t cpsr)
{
    __asm volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
}
```

### GPIO Stub (hal/src/zynq7000/Gpio.cpp)

PYNQ-Z2 LEDs are on PL (FPGA fabric) pins, not accessible without a
bitstream. All GPIO functions are no-ops so applications that reference
GPIO link cleanly.

### Clock Control (hal/src/zynq7000/Rcc.cpp)

Zynq uses SLCR (System Level Control Registers) instead of an RCC
peripheral. Clock gating is controlled through APER_CLK_CTRL at
SLCR + 0x12C. SLCR must be unlocked (key 0xDF0D at offset 0x008) before
any register writes.

| APER_CLK_CTRL Bit | Peripheral |
|---|---|
| 20 | UART0 |
| 21 | UART1 |
| 22 | GPIO |

### Watchdog Stub (hal/src/zynq7000/Watchdog.cpp)

The Zynq PS has a System Watchdog Timer (SWDT) at 0xF8005000, but it is
not used in the current port. `watchdogInit` and `watchdogFeed` are
no-ops.

---

## Crash Dump System

### Architecture Layers

The crash dump system is split into three layers, selected at link time:

```
FaultHandlers.s  (arch-specific assembly)
       |
       v
CrashDumpArch.cpp (arch-specific: populate FaultInfo, decode bits)
       |
       v
CrashDumpCommon.cpp (portable: format and print crash dump)
       |
       v
CrashDumpBoard.cpp (board-specific: UART output, LED blink)
```

### FaultInfo Struct (Architecture-Agnostic)

```cpp
struct FaultInfo
{
    uint32_t pc, lr, sp;
    uint32_t r0, r1, r2, r3, r12;
    uint32_t statusReg;           // xPSR (M) or CPSR (A9)
    uint32_t faultReg[4];         // M: CFSR/HFSR/MMFAR/BFAR
    const char *faultRegNames[4]; // A9: DFSR/IFSR/DFAR/IFAR
    const char *faultType;        // "DataAbort", "HardFault", etc.
    uint32_t excInfo;             // EXC_RETURN (M) or fault type code (A9)
};
```

### Cortex-A9 Fault Handlers (FaultHandlers.s)

Each fault handler saves the exception frame to the SYS mode stack
using SRS, then calls `faultHandlerC` with a fault type code in r1:

| Handler | LR Adjust | r1 Value | Fault Type |
|---|---|---|---|
| DataAbort_Handler | `sub lr, lr, #8` | 1 | Data Abort |
| PrefetchAbort_Handler | `sub lr, lr, #4` | 2 | Prefetch Abort |
| Undefined_Handler | `sub lr, lr, #4` | 3 | Undefined Instruction |

The LR adjustment accounts for the ARM pipeline: data aborts push
LR = fault address + 8, prefetch aborts and undefined instructions push
LR = fault address + 4.

On Cortex-M, `faultHandlerC` receives the EXC_RETURN value in r1. On
Cortex-A9, this parameter is repurposed as the fault type code since
there is no EXC_RETURN mechanism.

### Cortex-A9 Fault Registers (CP15)

Read via MRC instructions:

| Register | CP15 Access | Purpose |
|---|---|---|
| DFSR | `mrc p15, 0, r, c5, c0, 0` | Data Fault Status Register |
| IFSR | `mrc p15, 0, r, c5, c0, 1` | Instruction Fault Status Register |
| DFAR | `mrc p15, 0, r, c6, c0, 0` | Data Fault Address Register |
| IFAR | `mrc p15, 0, r, c6, c0, 2` | Instruction Fault Address Register |

vs. Cortex-M:

| Register | Address | Purpose |
|---|---|---|
| CFSR | 0xE000ED28 | Configurable Fault Status Register |
| HFSR | 0xE000ED2C | HardFault Status Register |
| MMFAR | 0xE000ED34 | MemManage Fault Address Register |
| BFAR | 0xE000ED38 | BusFault Fault Address Register |

### DFSR/IFSR Decoding

The fault status field is a 5-bit value composed of bits [10, 3:0]:

```cpp
uint32_t status = (fsr & 0xF) | ((fsr >> 6) & 0x10);
```

| Status Code | Fault |
|---|---|
| 0x01 | Alignment fault |
| 0x05 | Translation fault (section) |
| 0x07 | Translation fault (page) |
| 0x09 | Domain fault (section) |
| 0x0B | Domain fault (page) |
| 0x0D | Permission fault (section) |
| 0x0F | Permission fault (page) |
| 0x08 | Synchronous external abort |
| 0x16 | Asynchronous external abort |

### Cortex-A9 Fault Initialization

No-op. Unlike Cortex-M (where SHCSR must enable individual fault
handlers), Cortex-A9 data abort, prefetch abort, and undefined
instruction exceptions are always enabled.

### Test Faults

| FaultType | Cortex-M | Cortex-A9 |
|---|---|---|
| DivideByZero | `SDIV r0, r0, r1` (with CCR DIV_0_TRP) | `UDF #0` (ARM does not trap div-by-zero) |
| InvalidMemory | Write to 0xCCCCCCCC | Write to 0xCCCCCCCC |
| UndefinedInstruction | `.word 0xDEADDEAD` | `.word 0xE7F000F0` (UDF #0, permanently undefined) |

### Board-Specific Output (PYNQ-Z2)

`boardEnsureOutput()` initializes PS UART0 from scratch if not already
enabled (safe to call from any fault context). `boardFaultBlink()` enters
a WFE loop since the PYNQ-Z2 LEDs are on PL fabric and not accessible.

---

## MPU Stub (Cortex-A9)

The Cortex-A9 has a full MMU, not a simple MPU. MMU page table
configuration is deferred to a future phase. For Phase 4:

- `mpuInit()` and `mpuConfigureThreadRegion()` are no-ops
- Portable math functions (`mpuRoundUpSize`, `mpuSizeEncoding`,
  `mpuValidateStack`, `mpuComputeThreadConfig`) are retained since
  `threadCreate()` calls them unconditionally
- Context switch assembly does not update any memory protection registers

---

## Build System Integration

### MSOS_TARGET CMake Variable

The build system uses `MSOS_TARGET` to select per-target source
directories and compiler flags:

| MSOS_TARGET | CPU Flags | Arch Dir | HAL Dir | Startup Dir |
|---|---|---|---|---|
| stm32f207zgt6 | `-mcpu=cortex-m3 -mthumb` | cortex-m3 | stm32f4 | stm32f207zgt6 |
| stm32f407zgt6 | `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16` | cortex-m3 | stm32f4 | stm32f407zgt6 |
| pynq-z2 | `-mcpu=cortex-a9 -marm -mfpu=vfpv3-d16 -mfloat-abi=softfp` | cortex-a9 | zynq7000 | pynq-z2 |

### MSOS_HAL_DIR

Selects the HAL source directory. STM32 targets share `stm32f4/`;
PYNQ-Z2 uses `zynq7000/`.

### No CMSIS Device Headers

Zynq-7000 has no CMSIS device header (unlike STM32). The startup
CMakeLists.txt conditionally includes CMSIS headers only for STM32
targets.

---

## JTAG Development Workflow

```
1. u-boot boots on PYNQ-Z2 (configures PLLs, DDR, clocks)
2. OpenOCD connects via FTDI FT2232 (/dev/ttyUSB1)
3. GDB loads ELF to DDR at 0x00100000
4. Reset -> _boot -> SystemInit -> main
```

OpenOCD configuration targets the FTDI FT2232 dual-channel chip
(channel 0 = JTAG, channel 1 = UART serial).

---

## Design Decisions

| Decision | Rationale |
|---|---|
| Arch.h abstraction | Single interface for all architectures, no #ifdef in .cpp |
| SGI #0 for context switch | Lowest-priority software interrupt, replaces PendSV |
| SCU private timer | Per-CPU timer, replaces SysTick (not available on A-profile) |
| Identical 16-word stack frame | Keeps threadCreate() portable across architectures |
| SRS/RFE instructions | Atomic save/restore of return state across mode switches |
| Only r0-r3 in IRQ handler | Avoids clobbering callee-saved registers before context save |
| MMU enabled before C code | Strongly Ordered memory breaks unaligned GCC stack accesses |
| L2 invalidation at boot | JTAG reload may leave stale data in PL310 cache |
| CPU1 parked in WFE | Single-core for simplicity, SMP deferred |
| GPIO no-op stub | LEDs on PL fabric, no bitstream loaded |
| Fault type code in r1 | Repurposes excReturn parameter for A9 fault identification |
| UDF #0 for DivideByZero test | ARM does not trap integer division by zero |

---

## Test Coverage

Phase 4 functionality is verified through:

- Existing host unit tests (all kernel tests pass unchanged since the
  architecture abstraction is transparent to test code)
- On-target verification on PYNQ-Z2 hardware:
  - Thread creation and context switching (multi-thread demo)
  - System tick timing via SCU private timer
  - UART output (printf/stdout redirection)
  - Crash dump triggered by each fault type
  - Sleep/yield correctness

No new host test files were added for Phase 4 since the architecture
layer is tested via hardware and the portable kernel code is already
covered by the existing test suite using mock implementations.

---

## Files

| File | Purpose |
|---|---|
| `kernel/inc/kernel/Arch.h` | Architecture-agnostic kernel interface |
| `kernel/inc/kernel/CrashDump.h` | Crash dump public API |
| `kernel/inc/kernel/CrashDumpArch.h` | FaultInfo struct + arch function declarations |
| `kernel/inc/kernel/CrashDumpBoard.h` | Board output function declarations |
| `kernel/src/arch/cortex-a9/Arch.cpp` | GIC, private timer, critical sections, ISR detection |
| `kernel/src/arch/cortex-a9/ContextSwitch.s` | IRQ handler + SVC handler (ARM mode) |
| `kernel/src/arch/cortex-a9/FaultHandlers.s` | DataAbort, PrefetchAbort, Undefined handlers |
| `kernel/src/arch/cortex-a9/CrashDumpArch.cpp` | CP15 fault register reading + decoding |
| `kernel/src/arch/cortex-a9/Mpu.cpp` | MMU/MPU stub (no-ops + portable math) |
| `kernel/src/core/CrashDumpCommon.cpp` | Portable crash dump formatter |
| `kernel/src/board/pynq-z2/CrashDumpBoard.cpp` | PS UART0 polled output, WFE blink |
| `kernel/src/board/stm32f207zgt6/CrashDumpBoard.cpp` | USART1 polled output, PC13 LED blink |
| `startup/pynq-z2/Startup.s` | Boot sequence, MMU setup, vector table, mode stacks |
| `startup/pynq-z2/SystemInit.cpp` | Clock globals, SLCR unlock |
| `startup/pynq-z2/Linker.ld` | DDR memory layout, stack/heap sections |
| `hal/src/zynq7000/Uart.cpp` | PS UART0/1 driver (Cadence IP) |
| `hal/src/zynq7000/Gpio.cpp` | GPIO no-op stub (LEDs on PL) |
| `hal/src/zynq7000/Rcc.cpp` | SLCR APER_CLK_CTRL clock gating |
| `hal/src/zynq7000/Watchdog.cpp` | Watchdog no-op stub |
