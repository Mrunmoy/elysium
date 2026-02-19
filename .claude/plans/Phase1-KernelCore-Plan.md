# Phase 1: Kernel Core -- Context Switching, Scheduler, Thread Management

## Context

Phase 0 is complete (commit f566d13 on master). The firmware boots on STM32F207ZGT6
at 120 MHz, blinks LED on PC13, and prints to UART on PA9. Host tests pass (7/7).

Phase 1 adds the minimal kernel core that enables multithreading: Thread Control Block,
PendSV-based context switching, SysTick-driven round-robin scheduler, and a thread API.
The deliverable is a demo running 2+ threads concurrently (LED blink + UART print),
verified on hardware via webcam and serial.

**Approach: Test-Driven Development** -- write tests first, then implement to make them
pass. Each step writes tests before implementation code.

## Pre-Implementation: Git Setup

1. Push repo to `git@github.com:Mrunmoy/elysium.git`
2. Create empty `main` branch, push current master as `phase-0`, create `phase-1/kernel-core`

## File Layout

```
kernel/
  inc/kernel/
    Thread.h           -- TCB struct, ThreadState enum, ThreadConfig, threadCreate()
    Scheduler.h        -- Scheduler class: init, addThread, pickNext, tick, yield
    Kernel.h           -- kernel::init(), kernel::startScheduler(), kernel::yield()
    CortexM.h          -- Arch interface: triggerContextSwitch, configureSysTick, etc.
  src/core/
    Thread.cpp         -- threadCreate(), initial stack frame builder
    Scheduler.cpp      -- Round-robin ready queue, tick handler, thread termination
    Kernel.cpp         -- Kernel init, SysTick_Handler, idle thread, kernelThreadExit
  src/arch/cortex-m3/
    ContextSwitch.s    -- PendSV_Handler (context switch), SVC_Handler (first thread)
    CortexM.cpp        -- NVIC/SysTick register config, critical sections
  CMakeLists.txt       -- kernel static library (cross-compile only)

test/kernel/
  MockKernel.h         -- Mock state recording (like test/hal/MockRegisters.h)
  MockCortexM.cpp      -- Mock arch layer (records calls, no hardware)
  ThreadTest.cpp       -- Tests for TCB creation, stack frame layout
  SchedulerTest.cpp    -- Tests for round-robin, tick, preemption, termination
  CMakeLists.txt       -- kernel_tests executable (links real core + mock arch)

app/threads/
  main.cpp             -- Multi-threaded demo (LED + UART threads)
  CMakeLists.txt       -- Links kernel + hal + startup
```

## Key Data Structures

### Thread Control Block (Thread.h)

```cpp
struct ThreadControlBlock
{
    std::uint32_t *m_stackPointer;       // [offset 0] PSP -- assembly loads/stores here
    ThreadState m_state;                  // Inactive, Ready, Running, Suspended
    ThreadId m_id;
    std::uint8_t m_priority;             // Reserved for Phase 2
    const char *m_name;
    std::uint32_t *m_stackBase;          // Stack overflow detection
    std::uint32_t m_stackSize;
    std::uint32_t m_timeSliceRemaining;
    std::uint32_t m_timeSlice;
};
```

`m_stackPointer` MUST be first field (offset 0) -- PendSV assembly uses `ldr r0, [tcb, #0]`.

### Initial Stack Frame (17 words = 68 bytes, built by threadCreate)

```
SP+0:  EXC_RETURN    0xFFFFFFFD   (thread mode, PSP, no FPU)
SP+4:  r4            0
SP+8:  r5            0
SP+12: r6            0
SP+16: r7            0
SP+20: r8            0
SP+24: r9            0
SP+28: r10           0
SP+32: r11           0
--- hardware-saved frame (popped automatically on exception return) ---
SP+36: r0            arg pointer
SP+40: r1            0
SP+44: r2            0
SP+48: r3            0
SP+52: r12           0
SP+56: LR            kernelThreadExit address
SP+60: PC            thread entry function
SP+64: xPSR          0x01000000 (Thumb bit)
```

### Constants

- kMaxThreads = 8 (including idle)
- kDefaultTimeSlice = 10 ticks (10 ms at 1 kHz)
- kIdleThreadId = 0
- Thread stacks: static uint32_t arrays, 512 bytes default

## Implementation Steps (TDD Order)

### Step 1: Test infrastructure + Thread creation tests (RED)

Write first, expect compilation/link failures:

- `test/kernel/MockKernel.h` -- mock state recording structs
- `test/kernel/MockCortexM.cpp` -- stub arch functions
- `test/kernel/ThreadTest.cpp`:
  - CreateThread_AssignsValidId
  - CreateThread_SetsStateToReady
  - CreateThread_InitializesStackFrame (check xPSR=0x01000000, PC=entry, LR=threadExit, EXC_RETURN=0xFFFFFFFD, r0=arg)
  - CreateThread_StackAligned8Bytes
  - CreateThread_MaxThreadsReturnsInvalid
  - CreateThread_ArgumentPassedInR0
- `test/kernel/CMakeLists.txt`
- Update `test/CMakeLists.txt` to add kernel subdirectory

### Step 2: Thread.h + Thread.cpp (GREEN)

Implement just enough to make ThreadTest pass:

- `kernel/inc/kernel/Thread.h` -- TCB struct, enums, threadCreate() declaration
- `kernel/inc/kernel/CortexM.h` -- arch interface declaration (needed by Thread.cpp for kernelThreadExit address)
- `kernel/src/core/Thread.cpp` -- threadCreate() with initial stack frame builder

### Step 3: Scheduler tests (RED)

- `test/kernel/SchedulerTest.cpp`:
  - Init_StartsWithNoThreads
  - AddThread_AppearsInReadyQueue
  - PickNext_RoundRobinTwoThreads
  - PickNext_RoundRobinThreeThreads
  - Tick_DecrementsTimeSlice
  - Tick_PendsContextSwitchOnExpiry (verify mock records triggerContextSwitch)
  - TerminateThread_RemovesFromQueue
  - IdleThread_RunsWhenQueueEmpty
  - IdleThread_SkippedWhenOthersReady
  - Yield_TriggersContextSwitch

### Step 4: Scheduler.h + Scheduler.cpp (GREEN)

Implement round-robin scheduler to make SchedulerTest pass:

- `kernel/inc/kernel/Scheduler.h` -- Scheduler class
- `kernel/src/core/Scheduler.cpp` -- ready queue, pickNext, tick, terminate

### Step 5: Kernel.h + Kernel.cpp (GREEN)

- `kernel/inc/kernel/Kernel.h` -- init, startScheduler, yield, tick
- `kernel/src/core/Kernel.cpp` -- idle thread, SysTick_Handler, kernelThreadExit, g_currentTcb/g_nextTcb globals

### Step 6: Arch layer (cross-compile only)

- `kernel/src/arch/cortex-m3/ContextSwitch.s` -- PendSV_Handler, SVC_Handler
- `kernel/src/arch/cortex-m3/CortexM.cpp` -- NVIC priorities, SysTick config, triggerContextSwitch
- `kernel/CMakeLists.txt`
- Update root `CMakeLists.txt` to add kernel subdirectory

### Step 7: Demo app

- `app/threads/main.cpp` -- LED thread (toggle PC13 ~500ms), UART thread (print "tick N" ~1s)
- `app/threads/CMakeLists.txt`
- Update root `CMakeLists.txt` to add app/threads

### Step 8: Cross-compile + flash + on-target verification

- `python3 build.py` -- verify firmware builds
- `python3 build.py -f` -- flash
- Verify via webcam: LED blinking
- Verify via UART: "ms-os kernel starting" + periodic "tick N" messages
- Both concurrent = context switching works

### Step 9: Documentation

- `doc/design/Phase1-KernelCore.md`

## Architecture Notes

- **PendSV priority 0xFF** (lowest) -- context switch only after all ISRs complete
- **SysTick priority 0xFE** -- ticks fire, pend PendSV, PendSV runs after SysTick returns
- **MSP for kernel/ISRs, PSP for threads** -- CONTROL.SPSEL set during SVC first-launch
- **SVC for first context switch** -- no outgoing thread to save, clean entry into first thread
- **Static allocation only** -- TCBs, stacks, ready queue all statically allocated
- **Thread exit safety** -- kernelThreadExit in LR catches thread function returns
- **Portable core** -- Thread.cpp, Scheduler.cpp have zero hardware deps, fully testable on host
- **Arch isolation** -- all Cortex-M3 specifics in kernel/src/arch/cortex-m3/, mock-substituted for tests

## CMake Integration

Cross-compile graph adds: `kernel` library + `app/threads` executable.
Host test graph adds: `test/kernel/` which compiles real Thread.cpp + Scheduler.cpp + MockCortexM.cpp.

## On-Target Verification

| Test | Method | Expected |
|------|--------|----------|
| LED blinks | Webcam capture 6 frames | Alternating brightness at ~500ms |
| UART boot msg | pyserial /dev/ttyUSB0 | "ms-os kernel starting\r\n" |
| UART tick msgs | pyserial /dev/ttyUSB0 | "tick 0", "tick 1", ... at ~1s intervals |
| Concurrent | Both above | LED + UART active simultaneously |
| Stability | Run 5 min | No crash, no garbled output |

## Files to Modify (Existing)

- `CMakeLists.txt` (root) -- add kernel + app/threads subdirectories
- `test/CMakeLists.txt` -- add kernel test subdirectory
- `startup/stm32f207zgt6/Startup.s` -- PendSV/SVC/SysTick weak handlers already exist (will be overridden by kernel strong symbols)

## Files to Create (New)

- `kernel/inc/kernel/Thread.h`
- `kernel/inc/kernel/Scheduler.h`
- `kernel/inc/kernel/Kernel.h`
- `kernel/inc/kernel/CortexM.h`
- `kernel/src/core/Thread.cpp`
- `kernel/src/core/Scheduler.cpp`
- `kernel/src/core/Kernel.cpp`
- `kernel/src/arch/cortex-m3/ContextSwitch.s`
- `kernel/src/arch/cortex-m3/CortexM.cpp`
- `kernel/CMakeLists.txt`
- `test/kernel/MockKernel.h`
- `test/kernel/MockCortexM.cpp`
- `test/kernel/ThreadTest.cpp`
- `test/kernel/SchedulerTest.cpp`
- `test/kernel/CMakeLists.txt`
- `app/threads/main.cpp`
- `app/threads/CMakeLists.txt`
- `doc/design/Phase1-KernelCore.md`
