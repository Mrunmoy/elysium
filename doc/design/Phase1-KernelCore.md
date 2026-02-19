# Phase 1: Kernel Core -- Context Switching, Scheduler, Thread Management

## Overview

Phase 1 adds the minimal kernel core that enables multithreading on the
STM32F207ZGT6 (Cortex-M3, 120 MHz). The deliverable is two threads running
concurrently: one toggling an LED, another printing to UART.

## Architecture

### Component Diagram

```
+-------------------+     +-------------------+
|  app/threads/     |     |  hal/             |
|  main.cpp         |---->|  Gpio, Uart, Rcc  |
+-------------------+     +-------------------+
         |
         v
+-------------------+     +-------------------+
|  kernel/          |     |  startup/         |
|  Kernel.cpp       |---->|  Startup.s        |
|  Thread.cpp       |     |  SystemInit.cpp   |
|  Scheduler.cpp    |     |  Linker.ld        |
+-------------------+     +-------------------+
         |
         v
+-------------------+
|  arch/cortex-m3/  |
|  ContextSwitch.s  |
|  CortexM.cpp      |
+-------------------+
```

### Execution Modes

- **MSP (Main Stack Pointer)**: Used by kernel, ISRs, and Reset_Handler
- **PSP (Process Stack Pointer)**: Used by all threads
- **Thread mode**: Normal execution (all threads + idle)
- **Handler mode**: Exception/interrupt handlers (SysTick, PendSV, SVC)

### Interrupt Priority

| Exception | Priority | Role |
|-----------|----------|------|
| SVC       | default  | Launches first thread |
| SysTick   | 0xFE     | Periodic tick, scheduler |
| PendSV    | 0xFF     | Context switch (lowest, runs after all ISRs) |

## Data Structures

### Thread Control Block (TCB)

```cpp
struct ThreadControlBlock
{
    uint32_t *m_stackPointer;      // [offset 0] -- assembly reads this
    ThreadState m_state;           // Inactive, Ready, Running, Suspended
    ThreadId m_id;
    uint8_t m_priority;            // Reserved for Phase 2
    const char *m_name;
    uint32_t *m_stackBase;         // Stack overflow detection
    uint32_t m_stackSize;
    uint32_t m_timeSliceRemaining;
    uint32_t m_timeSlice;
};
```

`m_stackPointer` MUST be the first field at offset 0. The PendSV assembly
handler uses `LDR r0, [tcb, #0]` and `STR r0, [tcb, #0]` to load/store it.

### Initial Stack Frame (16 words = 64 bytes)

Built by `threadCreate()` at the top of the thread's stack:

```
SP+0:  r4            0              \
SP+4:  r5            0               |  Software context
SP+8:  r6            0               |  (pushed/popped by PendSV)
SP+12: r7            0               |
SP+16: r8            0               |
SP+20: r9            0               |
SP+24: r10           0               |
SP+28: r11           0              /
SP+32: r0            arg pointer    \
SP+36: r1            0               |  Hardware exception frame
SP+40: r2            0               |  (pushed/popped automatically
SP+44: r3            0               |   by Cortex-M3 on exception
SP+48: r12           0               |   entry/return)
SP+52: LR            kernelThreadExit|
SP+56: PC            thread entry    |
SP+60: xPSR          0x01000000     /
```

16 words = 64 bytes = naturally 8-byte aligned (ARM AAPCS requirement).
EXC_RETURN (0xFFFFFFFD) is hardcoded in the PendSV/SVC handlers, not stored
on the stack, since Cortex-M3 has no FPU and the value never varies.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| kMaxThreads | 8 | Including idle thread |
| kDefaultTimeSlice | 10 | 10 ms at 1 kHz SysTick |
| kIdleThreadId | 0 | Always the first thread created |
| kInvalidThreadId | 0xFF | Sentinel for errors |

## Scheduler Algorithm

**Round-robin with time-slicing.**

The scheduler maintains a circular FIFO ready queue. The idle thread is NOT
in the queue -- it runs only when the queue is empty.

### Context Switch Flow

```
SysTick_Handler fires (every 1 ms)
  |
  +-- Increment tick count
  |
  +-- scheduler.tick()
  |     |
  |     +-- Decrement current thread's m_timeSliceRemaining
  |     |
  |     +-- If expired: reset slice, pend PendSV via SCB->ICSR
  |     |
  |     +-- Return true if switch needed
  |
  +-- If switch needed: scheduler.switchContext()
  |     |
  |     +-- Move current (Running) thread to back of ready queue
  |     +-- Dequeue front of queue as next thread
  |     +-- Mark new thread as Running
  |     +-- Update g_nextTcb
  |
  +-- Return from SysTick
        |
        v
PendSV_Handler fires (lowest priority, after all ISRs)
  |
  +-- CPSID I (disable interrupts)
  +-- Save r4-r11 to outgoing thread's stack (PSP)
  +-- Store PSP in g_currentTcb->m_stackPointer
  +-- g_currentTcb = g_nextTcb
  +-- Load g_nextTcb->m_stackPointer
  +-- Restore r4-r11 from incoming thread's stack
  +-- Set PSP to incoming stack
  +-- CPSIE I (re-enable interrupts)
  +-- BX 0xFFFFFFFD (return to thread mode using PSP)
```

### First Thread Launch (SVC)

```
kernel::startScheduler()
  |
  +-- Set interrupt priorities (PendSV=0xFF, SysTick=0xFE)
  +-- scheduler.switchContext() to pick first thread
  +-- Set g_currentTcb and g_nextTcb
  +-- Configure SysTick for 1 ms interrupts
  +-- SVC 0  (triggers SVC_Handler)
        |
        v
SVC_Handler
  +-- Load g_currentTcb->m_stackPointer
  +-- Restore r4-r11 from initial stack frame
  +-- Set PSP to hardware frame
  +-- Set CONTROL.SPSEL = 1 (use PSP in thread mode)
  +-- ISB
  +-- BX 0xFFFFFFFD (return to first thread)
```

## Thread Exit Safety

The initial stack frame's LR is set to `kernelThreadExit`. If a thread function
returns, it calls `kernelThreadExit` which:
1. Removes the thread from the scheduler
2. Triggers a context switch via PendSV
3. Spins forever (should never execute past the trigger)

## File Layout

```
kernel/
  inc/kernel/
    Thread.h           TCB struct, ThreadState, threadCreate()
    Scheduler.h        Scheduler class with round-robin queue
    Kernel.h           kernel::init(), startScheduler(), yield(), tickCount()
    CortexM.h          Arch interface + g_currentTcb/g_nextTcb externs
  src/core/
    Thread.cpp         TCB pool, threadCreate with stack frame builder
    Scheduler.cpp      Round-robin ready queue, tick, yield, switchContext
    Kernel.cpp         Kernel init, SysTick_Handler, idle thread, kernelThreadExit
  src/arch/cortex-m3/
    ContextSwitch.s    PendSV_Handler + SVC_Handler
    CortexM.cpp        NVIC priorities, SysTick config, critical sections
  CMakeLists.txt

test/kernel/
  MockKernel.h         Mock state recording
  MockCortexM.cpp      Mock arch layer
  MockKernelGlobals.cpp  Mock g_currentTcb, g_nextTcb, kernelThreadExit
  ThreadTest.cpp       11 tests for TCB creation and stack frames
  SchedulerTest.cpp    15 tests for round-robin, tick, context switch
  CMakeLists.txt

app/threads/
  main.cpp             LED thread + UART thread demo
  CMakeLists.txt
```

## Build Verification

### Cross-compile (ARM)

```
$ python3 build.py
threads: 3036 bytes FLASH, 23296 bytes SRAM
blinky:  1644 bytes FLASH, 20480 bytes SRAM
```

### Host tests

```
$ python3 build.py -t
100% tests passed, 0 tests failed out of 33
  7 GpioTest  (Phase 0, HAL)
 11 ThreadTest  (TCB creation, stack frame layout)
 15 SchedulerTest  (round-robin, tick, yield, switchContext)
```

## Testing Strategy

Tests run on the host (x86_64) using link-time mock substitution:

- **Thread.cpp** and **Scheduler.cpp** have zero hardware dependencies and
  compile identically on host and ARM.
- **MockCortexM.cpp** provides stubs for `triggerContextSwitch()`,
  `configureSysTick()`, etc. that record calls into global vectors.
- **MockKernelGlobals.cpp** provides `g_currentTcb`, `g_nextTcb`, and
  `kernelThreadExit()` stubs.
- Tests verify scheduler behavior by inspecting TCB state and mock call records.

### Portability Notes

Function/data pointers are 32-bit on ARM but 64-bit on x86_64. The stack frame
builder casts through `std::uintptr_t` then truncates to `uint32_t`. This is
correct on ARM (no truncation) and acceptable on host (tests verify structure,
not pointer values). The `~0x7u` alignment mask uses `~std::uintptr_t{7}` to
avoid truncating the upper 32 bits on 64-bit hosts.
