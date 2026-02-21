# Phase 2: Scheduling -- Design Document

## Overview

Phase 2 adds preemptive priority-based scheduling to ms-os, including thread
management, context switching, synchronization primitives (mutex, semaphore),
and sleep/delay. The scheduler supports 32 priority levels with O(1) thread
selection and round-robin within each priority level.

---

## High-Level Architecture

```
 Application Threads
        |
        v
 +------------------+      +-----------+     +-------------+
 | Kernel API       |----->| Scheduler |---->| Context     |
 | (Kernel.h)       |      | (O(1)     |     | Switch      |
 |  createThread()  |      |  bitmap)  |     | (PendSV/IRQ)|
 |  sleep/yield()   |      +-----------+     +-------------+
 |  startScheduler()|            |                  |
 +------------------+            v                  v
        |              +------------------+  +------------+
        v              | Sync Primitives  |  | Arch Layer |
 +-------------+       |  Mutex (PI)      |  | (Cortex-M, |
 | Thread Pool |       |  Semaphore       |  |  Cortex-A9)|
 | (8 TCBs)    |       |  WaitQueue       |  +------------+
 +-------------+       +------------------+
```

### Key Design Points

- **32 priority levels** (0 = highest, 31 = lowest), matching Cortex-M convention
- **Bitmap + per-priority lists** for O(1) highest-ready thread selection
- **Preemptive**: higher-priority thread runs immediately when unblocked
- **Round-robin** within same priority level (configurable time slice)
- **Static TCB pool**: max 8 threads (including idle), no dynamic allocation
- **Mandatory priority inheritance** on mutex contention

---

## Thread Management

### Thread Control Block (TCB)

```
Offset  Field                Size    Description
------  -------------------  ------  ------------------------------------
  0     stackPointer         4       Current SP (read by assembly)
  4     state                1       Inactive/Ready/Running/Blocked
  5     id                   1       Thread ID (0-7)
  6     basePriority         1       Assigned priority (immutable)
  7     currentPriority      1       Effective priority (may be boosted)
  8     name                 4       Debug name pointer
 12     stackBase            4       Bottom of stack allocation
 16     stackSize            4       Stack size in bytes
 20     timeSliceRemaining   4       Ticks left in current slice
 24     timeSlice            4       Total ticks per slice
 28     nextReady            1       Linked list: next in ready queue
 29     nextWait             1       Linked list: next in wait queue
 30     (padding)            2
 32     wakeupTick           4       Absolute tick for sleep wakeup
 36     mpuStackRbar         4       Pre-computed MPU RBAR (Phase 3)
 40     mpuStackRasr         4       Pre-computed MPU RASR (Phase 3)
```

The `stackPointer` field is at offset 0 so the context switch assembly can
load/store it with a simple `[TCB, #0]` addressing.

### Thread States

```
                 createThread()
    Inactive ----------------------> Ready
                                      |  ^
                            pickNext()|  | tick()/yield()
                                      v  |
                                    Running
                                      |  ^
                   mutex/sem/sleep()  |  | unblock/wakeup
                                      v  |
                                    Blocked
```

### Initial Stack Frame (16 words, 64 bytes)

Built by `threadCreate()` to look like a context that was interrupted mid-execution:

```
High address (top of stack):
  [SP+15]  xPSR / CPSR     arch::initialStatusRegister()
  [SP+14]  PC               thread entry function
  [SP+13]  LR               kernelThreadExit (cleanup on return)
  [SP+12]  r12              0
  [SP+11]  r3               0
  [SP+10]  r2               0
  [SP+9]   r1               0
  [SP+8]   r0               thread argument
  ---- hardware/exception frame above, software-saved below ----
  [SP+7]   r11              0
  [SP+6]   r10              0
  [SP+5]   r9               0
  [SP+4]   r8               0
  [SP+3]   r7               0
  [SP+2]   r6               0
  [SP+1]   r5               0
  [SP+0]   r4               0   <-- stackPointer stored in TCB
Low address:
```

Words 8-15 are restored automatically by exception return (Cortex-M) or by
`rfeia` (Cortex-A9). Words 0-7 are restored by the software context switch.

---

## Scheduler

### Data Structures

```cpp
std::uint32_t m_readyBitmap;              // Bit N set = priority N has threads
ThreadId m_readyHead[32];                 // Per-priority FIFO: head
ThreadId m_readyTail[32];                 // Per-priority FIFO: tail
ThreadId m_currentThreadId;               // Running thread
ThreadId m_idleThreadId;                  // Fallback (priority 31)
```

### Algorithm: O(1) Highest-Priority Selection

```
highestReadyPriority = __builtin_ctz(m_readyBitmap)
```

`__builtin_ctz` (Count Trailing Zeros) maps to ARM `RBIT + CLZ`, giving the
lowest-numbered (highest-priority) bit in a single instruction.

### Operations

| Operation           | Complexity | Description                              |
|---------------------|------------|------------------------------------------|
| `addThread(id)`     | O(1)       | Append to tail of priority queue         |
| `pickNext()`        | O(1)       | CTZ bitmap, return head (no dequeue)     |
| `switchContext()`   | O(1)       | Dequeue head, enqueue old, update state  |
| `blockCurrentThread()` | O(1)   | Remove from ready, set Blocked           |
| `unblockThread(id)` | O(1)      | Add to ready queue, check preemption     |
| `tick()`            | O(1)       | Decrement time slice, check preemption   |
| `setThreadPriority()` | O(n)    | Remove from old queue, insert in new     |

### Preemption

`tick()` returns `true` (context switch needed) when:
1. A higher-priority thread became ready
2. Current thread's time slice expired and same-priority peers exist
3. Current is idle but user threads are ready

### Time Slicing

Each thread has a configurable `timeSlice` (default from `kDefaultTimeSlice`).
Every `tick()` decrements `timeSliceRemaining`. On expiry, if peers exist at
the same priority, the current thread moves to the tail of its priority queue
(round-robin rotation).

---

## Context Switching

### Cortex-M (PendSV)

Trigger: `arch::triggerContextSwitch()` sets PendSV pending bit.
PendSV runs at lowest exception priority, ensuring it fires after all ISRs.

```
PendSV_Handler:
  1. cpsid i                    -- disable interrupts
  2. mrs r0, psp                -- get thread stack pointer
  3. stmdb r0!, {r4-r11}       -- push software-saved registers
  4. str r0, [g_currentTcb]     -- save SP in outgoing TCB
  5. g_currentTcb = g_nextTcb   -- switch TCB pointer
  6. Write MPU RBAR/RASR        -- update thread stack region (Phase 3)
  7. ldr r0, [g_nextTcb]        -- load incoming SP
  8. ldmia r0!, {r4-r11}        -- pop software-saved registers
  9. msr psp, r0                -- set new PSP
 10. cpsie i                    -- re-enable interrupts
 11. bx 0xFFFFFFFD              -- EXC_RETURN to thread mode, PSP
```

### Cortex-A9 (IRQ Epilogue)

No PendSV on Cortex-A9. Context switch happens at the end of the IRQ handler
when `g_currentTcb != g_nextTcb`. Trigger: SGI #0 (Software Generated Interrupt).

```
IRQ_Handler:
  1. sub lr, lr, #4             -- adjust return address (ARM pipeline)
  2. srsdb sp!, #0x1F           -- save {PC, CPSR} to SYS mode stack
  3. cps #0x1F                  -- switch to SYS mode
  4. push {r0-r3, r12, lr}     -- save exception frame
  5. Read GIC ICCIAR            -- acknowledge interrupt
  6. Dispatch (timer/SGI/other)
  7. Write GIC ICCEOIR          -- end of interrupt
  8. if g_currentTcb != g_nextTcb:
       push {r4-r11}           -- save software context
       save SP to outgoing TCB
       g_currentTcb = g_nextTcb
       load SP from incoming TCB
       pop {r4-r11}            -- restore software context
  9. pop {r0-r3, r12, lr}      -- restore exception frame
 10. rfeia sp!                  -- restore PC + CPSR atomically
```

### SVC_Handler (First Thread Launch)

Called once by `startScheduler()` to launch the first thread. No outgoing
context to save -- just loads the initial stack frame and returns to thread mode.

---

## Tick and Timer

```
SysTick_Handler() / IRQ timer handler (every 1 ms):
  1. ++tickCount
  2. checkSleepingThreads()     -- wake threads whose wakeupTick <= now
  3. scheduler.tick()           -- check preemption / time slice
  4. if switch needed:
       switchContext()          -- dequeue next thread
       triggerContextSwitch()   -- pend PendSV or SGI #0
```

**Configuration:**
- Cortex-M: SysTick reload = `SystemCoreClock / 1000` (1 ms tick)
- Cortex-A9: SCU Private Timer, load = `(SystemCoreClock / 2) / 1000 - 1`
  (PERIPHCLK = CPU_CLK / 2)

**Interrupt priorities (Cortex-M only):**
- SysTick: priority 0xFE (second-lowest)
- PendSV: priority 0xFF (lowest, runs after all other ISRs)

---

## Synchronization Primitives

### Mutex (Priority-Inheriting, Recursive)

```
MutexControlBlock:
  owner        -- ThreadId holding the lock (kInvalidThreadId if free)
  lockCount    -- Recursive depth (supports same-thread re-locking)
  waitHead     -- Priority-sorted wait queue
  waitCount    -- Number of waiting threads
```

**Lock algorithm:**
1. If unowned: acquire, set owner, lockCount = 1
2. If owned by caller: recursive lock, ++lockCount
3. If owned by other thread:
   a. If caller has higher priority than owner, boost owner (priority inheritance)
   b. Insert caller into priority-sorted wait queue
   c. Block caller, switch context
   d. When resumed: caller holds the mutex

**Unlock algorithm:**
1. Verify caller is owner
2. --lockCount; if still > 0, return (recursive)
3. Restore owner to basePriority (undo any boost)
4. If waiters: transfer ownership to highest-priority waiter, unblock it
5. If unblocked waiter preempts current thread, trigger context switch

**Properties:**
- Recursive: same thread can lock N times, must unlock N times
- Priority inheritance: mandatory, prevents unbounded priority inversion
- Max 8 mutexes (static pool)

### Semaphore (Counting, Priority-Sorted)

```
SemaphoreControlBlock:
  count        -- Current count
  maxCount     -- Upper bound
  waitHead     -- Priority-sorted wait queue
  waitCount    -- Number of waiting threads
```

**Wait (P/down):**
1. If count > 0: decrement, return
2. If count == 0: insert into priority-sorted wait queue, block, switch context

**Signal (V/up):**
1. If waiters: wake highest-priority waiter (no count change)
2. If no waiters: increment count (capped at maxCount)

**Properties:**
- No ownership (any thread can signal)
- Binary semaphore if maxCount = 1
- Max 8 semaphores (static pool)

### Wait Queue (Shared Utility)

Both mutex and semaphore use a priority-sorted singly-linked wait queue:

```cpp
void waitQueueInsert(ThreadId &head, ThreadId id);    // Insert by priority
ThreadId waitQueueRemoveHead(ThreadId &head);          // Pop highest priority
void waitQueueRemove(ThreadId &head, ThreadId id);     // Remove specific
```

Threads with lower `currentPriority` value (higher urgency) are placed
closer to the head.

---

## Sleep / Delay

```cpp
void sleep(std::uint32_t ticks);
```

1. Set `tcb->wakeupTick = tickCount + ticks`
2. Block current thread
3. Switch context

Wakeup is checked every tick in `SysTick_Handler()`:
```cpp
for each thread:
  if state == Blocked && wakeupTick != 0 && now >= wakeupTick:
    wakeupTick = 0
    unblockThread(id)
```

---

## Critical Sections

All kernel operations are protected by interrupt disable/enable:

```cpp
arch::enterCritical();    // cpsid i (disable IRQs)
// ... atomic kernel operation ...
arch::exitCritical();     // cpsie i (enable IRQs)
```

Not re-entrant (no nesting counter). All kernel code assumes interrupts
were enabled on entry.

---

## Kernel API Summary

### Thread Management

```cpp
namespace kernel {
    void init();
    ThreadId createThread(ThreadFunction fn, void *arg, const char *name,
                          std::uint32_t *stack, std::uint32_t stackSize,
                          std::uint8_t priority = 16, std::uint32_t timeSlice = 0);
    void startScheduler();    // Does not return
    void yield();
    void sleep(std::uint32_t ticks);
    std::uint32_t tickCount();
}
```

### Synchronization

```cpp
MutexId     mutexCreate(const char *name = nullptr);
void        mutexDestroy(MutexId id);
bool        mutexLock(MutexId id);
bool        mutexTryLock(MutexId id);
bool        mutexUnlock(MutexId id);

SemaphoreId semaphoreCreate(std::uint32_t initial, std::uint32_t max,
                            const char *name = nullptr);
void        semaphoreDestroy(SemaphoreId id);
bool        semaphoreWait(SemaphoreId id);
bool        semaphoreTryWait(SemaphoreId id);
bool        semaphoreSignal(SemaphoreId id);
```

### Architecture

```cpp
namespace arch {
    void triggerContextSwitch();           // Cortex-M: PendSV; A9: SGI #0
    void configureSysTick(std::uint32_t ticks);
    void enterCritical();
    void exitCritical();
    void startFirstThread();              // SVC
    void setInterruptPriorities();
    std::uint32_t initialStatusRegister(); // xPSR or CPSR for new threads
}
```

---

## Files

### Public Headers
- `kernel/inc/kernel/Kernel.h`
- `kernel/inc/kernel/Scheduler.h`
- `kernel/inc/kernel/Thread.h`
- `kernel/inc/kernel/Mutex.h`
- `kernel/inc/kernel/Semaphore.h`
- `kernel/inc/kernel/Arch.h`

### Core Implementation
- `kernel/src/core/Kernel.cpp` -- init, sleep, yield, SysTick_Handler
- `kernel/src/core/Scheduler.cpp` -- bitmap scheduler, ready queues
- `kernel/src/core/Thread.cpp` -- TCB pool, stack frame init
- `kernel/src/core/Mutex.cpp` -- priority-inheriting mutex
- `kernel/src/core/Semaphore.cpp` -- counting semaphore
- `kernel/src/core/WaitQueue.cpp` -- shared priority-sorted list

### Architecture-Specific
- `kernel/src/arch/cortex-m3/Arch.cpp` -- NVIC, SysTick, critical sections
- `kernel/src/arch/cortex-m3/ContextSwitch.s` -- PendSV, SVC handlers
- `kernel/src/arch/cortex-m4/Arch.cpp` -- same as M3 (different FPU flags)
- `kernel/src/arch/cortex-m4/ContextSwitch.s` -- same as M3
- `kernel/src/arch/cortex-a9/Arch.cpp` -- GIC, private timer, SGI
- `kernel/src/arch/cortex-a9/ContextSwitch.s` -- IRQ-based context switch

### Tests
- `test/kernel/SchedulerTest.cpp`
- `test/kernel/ThreadTest.cpp`
- `test/kernel/MutexTest.cpp`
- `test/kernel/SemaphoreTest.cpp`
