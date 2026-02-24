# Phase 11: Dynamic Threads

## Overview

Adds thread lifecycle management so TCB slots can be reclaimed after a
thread terminates and reused by subsequent `createThread` calls. Prior
to this phase, thread IDs were monotonically increasing and slots were
never freed, capping the system at `kMaxThreads` total threads over the
entire lifetime of the kernel.

## Design

### Thread Lifecycle State Diagram

```
                threadCreate()
                     |
                     v
  +----------+   addThread()   +----------+    tick() /     +----------+
  | Inactive | -------------> |  Ready   | <------------- | Running  |
  +----------+                +----------+    preempt      +----------+
       ^                           |                            |
       |                           | switchContext()            |
       |                           +--------------------------->|
       |                                                        |
       |                      +----------+    sleep() /         |
       |                      | Blocked  | <-- mutex/sem/ipc    |
       |                      +----------+                      |
       |                           |                            |
       |                           | wakeup / unblock           |
       |                           +--> Ready --->  ...         |
       |                                                        |
       |          destroyThread()                               |
       |<----------- or -------- kernelThreadExit() <-----------+
       |                          (thread function returns)
       |
  threadDestroy()
  (slot available for reuse)
```

The key addition is the `Inactive --> Ready --> ... --> Inactive` cycle.
A destroyed thread's TCB slot is zeroed and its ID becomes available for
the next `threadCreate` call.

### Slot Allocation Change

**Before (monotonic):** `threadCreate` used a static counter
`s_nextId++`. Once a slot was used it was gone forever, even if the
thread terminated.

**After (scan):** `threadCreate` scans `s_tcbPool[0..kMaxThreads-1]`
for the first slot with `state == Inactive`. This means thread IDs are
indices into the pool and may be reused after destruction.

```cpp
ThreadId threadCreate(const ThreadConfig &config)
{
    // Scan for a free (Inactive) slot
    ThreadId id = kInvalidThreadId;
    for (ThreadId i = 0; i < kMaxThreads; ++i)
    {
        if (s_tcbPool[i].state == ThreadState::Inactive)
        {
            id = i;
            break;
        }
    }
    if (id == kInvalidThreadId)
    {
        return kInvalidThreadId;
    }
    // ... initialize TCB fields and build initial stack frame ...
}
```

Scan is O(N) where N = `kMaxThreads` (8). At 8 slots this is a trivial
cost, and creation is not a hot path.

## API

### kernel::destroyThread (kernel/inc/kernel/Kernel.h)

```cpp
// Destroy a thread: remove from scheduler, clean up IPC, free TCB slot.
// The thread must not be the currently running thread (use threadExit
// or let the thread function return for self-termination).
// Returns true on success, false if the ID is invalid or is the idle thread.
bool destroyThread(ThreadId id);
```

Called by another thread (or the kernel) to destroy a thread externally.
Cannot be used to destroy the currently running thread -- use
`kernelThreadExit` for self-termination.

### kernel::threadDestroy (kernel/inc/kernel/Thread.h)

```cpp
// Mark a thread's TCB slot as Inactive so the ID can be reused.
// Caller must first remove the thread from the scheduler and clean
// up any IPC state (see kernel::destroyThread for the full sequence).
void threadDestroy(ThreadId id);
```

Low-level TCB cleanup. Not intended to be called directly by
applications -- use `destroyThread` or let the thread return naturally.

### kernel::ipcResetMailbox (kernel/inc/kernel/Ipc.h)

```cpp
// Reset a single thread's mailbox (called during thread destruction)
void ipcResetMailbox(ThreadId id);
```

### kernel::kernelThreadExit (kernel/inc/kernel/Arch.h)

```cpp
// Called by kernel when a thread function returns
void kernelThreadExit();
```

Placed in the initial stack frame's LR register so it is called
automatically when a thread's entry function returns.

## Implementation Details

### Destruction Sequence

Both `destroyThread` (external) and `kernelThreadExit` (self) follow the
same cleanup chain with slight differences in critical section handling:

```
1. enterCritical()           -- disable interrupts
2. removeThread(id)          -- remove from scheduler ready/blocked lists
3. ipcResetMailbox(id)       -- clear mailbox ring buffer, wait queues, notify bits
4. threadDestroy(id)         -- zero TCB fields, mark Inactive
5. exitCritical()            -- re-enable interrupts
6. [if self] triggerContextSwitch()  -- yield CPU to next thread
```

#### destroyThread (external destruction)

```cpp
bool destroyThread(ThreadId id)
{
    if (id == s_scheduler.idleThreadId() || id >= kMaxThreads)
        return false;

    ThreadControlBlock *tcb = threadGetTcb(id);
    if (tcb == nullptr || tcb->state == ThreadState::Inactive)
        return false;

    arch::enterCritical();
    s_scheduler.removeThread(id);
    ipcResetMailbox(id);
    threadDestroy(id);
    arch::exitCritical();
    return true;
}
```

#### kernelThreadExit (self-termination)

```cpp
void kernelThreadExit()
{
    ThreadId currentId = s_scheduler.currentThreadId();
    arch::enterCritical();
    s_scheduler.removeThread(currentId);
    ipcResetMailbox(currentId);
    threadDestroy(currentId);
    arch::exitCritical();
    arch::triggerContextSwitch();
    while (true) {}   // unreachable
}
```

The thread's entry function returns into `kernelThreadExit` because the
initial stack frame sets LR to its address. After cleanup, a context
switch to the next ready thread is triggered. The infinite loop is a
safety net in case the context switch fails.

### Scheduler::removeThread

Removes a thread from all scheduler data structures:

```cpp
void Scheduler::removeThread(ThreadId id)
{
    ThreadControlBlock *tcb = threadGetTcb(id);
    if (tcb == nullptr)
        return;

    if (tcb->state == ThreadState::Ready)
        removeFromReadyList(id, tcb->currentPriority);

    tcb->state = ThreadState::Inactive;

    if (m_currentThreadId == id)
        m_currentThreadId = kInvalidThreadId;
}
```

Handles three cases:
- **Ready thread:** unlinked from its priority-level ready list, bitmap
  updated if the list becomes empty.
- **Running thread (self-termination):** `m_currentThreadId` is cleared
  to `kInvalidThreadId` so `switchContext` does not re-enqueue it.
- **Blocked thread:** already not in the ready queue; state is simply
  set to Inactive.

### threadDestroy (TCB cleanup)

```cpp
void threadDestroy(ThreadId id)
{
    if (id >= kMaxThreads)
        return;

    ThreadControlBlock &tcb = s_tcbPool[id];
    tcb.state = ThreadState::Inactive;
    tcb.id = kInvalidThreadId;
    tcb.stackPointer = nullptr;
    tcb.name = nullptr;
    tcb.stackBase = nullptr;
    tcb.stackSize = 0;
    tcb.nextReady = kInvalidThreadId;
    tcb.nextWait = kInvalidThreadId;
    tcb.wakeupTick = 0;
    tcb.mpuStackRbar = 0;
    tcb.mpuStackRasr = 0;
}
```

Every field is explicitly reset. Setting `state = Inactive` is what
makes the slot discoverable by the next `threadCreate` scan.

### ipcResetMailbox

```cpp
void ipcResetMailbox(ThreadId id)
{
    if (id >= kMaxThreads)
        return;

    ThreadMailbox &box = s_mailboxes[id];
    std::memset(&box, 0, sizeof(box));
    box.senderWaitHead = kInvalidThreadId;
    box.receiverWaitHead = kInvalidThreadId;
    box.blockReason = IpcBlockReason::None;
    box.replySlot = nullptr;
}
```

Clears all message slots, resets the ring buffer indices, clears
notification bits, and resets wait queue heads. This prevents stale
messages from leaking into a thread that later reuses the same slot.

## Safety

### Idle Thread Protection

`destroyThread` checks `id == s_scheduler.idleThreadId()` before
proceeding and returns `false` if the caller attempts to destroy the
idle thread. The idle thread must always exist for the scheduler to
function.

### Invalid ID Handling

All functions guard against out-of-range IDs (`id >= kMaxThreads`) and
return early or return `false`/`nullptr`. Attempting to destroy an
already-inactive slot also returns `false`.

### Stack Memory

`threadDestroy` does **not** free the stack buffer. Stack memory is
statically allocated by the application and its lifetime is managed
externally. The caller is responsible for ensuring the stack is not
reused while the thread is still running.

### Critical Section

The entire destroy sequence runs inside `enterCritical` /
`exitCritical` to prevent the scheduler tick from observing a
half-dismantled TCB. This is the same pattern used by mutex, semaphore,
and IPC blocking operations.

### Self-Termination

A running thread must not call `destroyThread(myOwnId)` because the
function does not trigger a context switch on return. Instead, a
running thread should either let its entry function return (which
invokes `kernelThreadExit` via LR) or call `kernelThreadExit` directly.

## Test Coverage

13 tests across two fixtures in `test/kernel/ThreadTest.cpp`:

### ThreadTest fixture (low-level threadDestroy)

| Test | Verifies |
|------|----------|
| `DestroyThread_MarksSlotInactive` | TCB state, id, name, stackBase are cleared |
| `DestroyThread_InvalidIdDoesNothing` | `kInvalidThreadId` and `kMaxThreads` do not crash |
| `CreateThread_ReusesDestroyedSlot` | Fill all 8 slots, destroy one, create reuses it |
| `CreateThread_ReusesFirstAvailableSlot` | Destroy slots 0 and 2, next create gets slot 0 |

### DestroyThreadTest fixture (kernel::destroyThread integration)

| Test | Verifies |
|------|----------|
| `DestroyThread_ReturnsTrue` | Successful destruction returns true |
| `DestroyThread_MarksInactive` | TCB state set to Inactive |
| `DestroyThread_RemovesFromScheduler` | readyCount decremented |
| `DestroyThread_CleansUpMailbox` | Mailbox count/head/tail zeroed after messages queued |
| `DestroyThread_IdCanBeReused` | Destroy + create cycle returns same ID with new name |
| `DestroyThread_InvalidIdReturnsFalse` | `kInvalidThreadId` and `kMaxThreads` return false |
| `DestroyThread_InactiveReturnsFalse` | Destroying an already-inactive slot returns false |
| `DestroyThread_IdleThreadReturnsFalse` | Idle thread is protected from destruction |
| `DestroyThread_MultipleCreateDestroyReusesCycles` | 5 consecutive create/destroy cycles on same slot |

## Files

| File | Purpose |
|------|---------|
| `kernel/inc/kernel/Thread.h` | `threadDestroy` declaration, updated `threadCreate` doc |
| `kernel/src/core/Thread.cpp` | Slot scanning in `threadCreate`, `threadDestroy` implementation |
| `kernel/inc/kernel/Kernel.h` | `destroyThread` public API |
| `kernel/src/core/Kernel.cpp` | `destroyThread` + `kernelThreadExit` implementations |
| `kernel/inc/kernel/Ipc.h` | `ipcResetMailbox` declaration |
| `kernel/src/core/Ipc.cpp` | `ipcResetMailbox` implementation |
| `kernel/inc/kernel/Scheduler.h` | `removeThread`, `idleThreadId` declarations |
| `kernel/src/core/Scheduler.cpp` | `removeThread` implementation |
| `test/kernel/ThreadTest.cpp` | 13 dynamic thread tests (4 ThreadTest + 9 DestroyThreadTest) |
