# Phase 6: Syscall Interface

## Overview

Adds an SVC-based syscall dispatch layer and unprivileged thread support to
the ms-os kernel. Unprivileged threads execute with CONTROL.nPRIV=1, cannot
access peripheral or kernel SRAM directly, and must issue all kernel requests
through SVC instructions. The existing kernel API remains fully available to
privileged threads via direct function calls, maintaining backward
compatibility.

Scope: Cortex-M3 and Cortex-M4 only. Cortex-A9 is not updated in this phase.

## Design

### Execution Model

```
+--------------------+      +-------------------+      +------------------+
| Unprivileged       |      | SVC_Handler       |      | Kernel           |
| Thread             |      | (Handler mode)    |      | Functions        |
|                    |      |                   |      |                  |
|  kernel::user::    | SVC  | Extract SVC#      | call | kernel::sleep()  |
|    sleep(100)   ----+--->  | from instruction  +----> | kernel::yield()  |
|                    |      | Call svcDispatch() |      | kernel::mutex..  |
|  <-- return value  | <----+ Write result to   | <----+ return value     |
|  in r0             |      | stacked r0        |      |                  |
+--------------------+      +-------------------+      +------------------+

+--------------------+
| Privileged         |      No SVC -- calls kernel functions directly
| Thread             |
|  kernel::sleep(100)+----> kernel::sleep()
+--------------------+
```

### Context Switch with Privilege Switching

```
PendSV_Handler:
  cpsid i
  save r4-r11 to outgoing PSP
  g_currentTcb->stackPointer = PSP
  g_currentTcb = g_nextTcb
  update MPU stack region (RBAR/RASR)
  restore r4-r11 from incoming PSP
  set PSP
  +----------------------------------------------+
  | ldrb r0, [tcb, #44]   ; TCB.privileged       |
  | cmp  r0, #0                                  |
  | ite  eq                                      |
  | moveq r0, #3          ; nPRIV=1 | SPSEL=1   |
  | movne r0, #2          ; nPRIV=0 | SPSEL=1   |
  | msr  control, r0                             |
  | isb                                          |
  +----------------------------------------------+
  cpsie i
  bx 0xFFFFFFFD           ; EXC_RETURN -> thread mode, PSP
```

### SVC Handler Flow

```
SVC_Handler:
  tst lr, #4              ; check EXC_RETURN bit 2
  ite eq
  mrseq r0, msp           ; caller on MSP (first thread launch)
  mrsne r0, psp           ; caller on PSP (normal syscall)
  ldr  r1, [r0, #24]      ; stacked PC
  ldrb r1, [r1, #-2]      ; SVC immediate byte

  cmp  r1, #0
  beq  start_first_thread  ; SVC 0: special path

  ; SVC 1+: general dispatch
  push {r0, lr}
  svcDispatch(svcNum, frame)
  pop  {r1, lr}
  str  r0, [r1, #0]       ; write return value to stacked r0
  bx   lr
```

### The g_inSyscall Problem

SVC_Handler runs in handler mode. The ICSR.VECTACTIVE field is non-zero,
so `inIsrContext()` returns true. Blocking kernel functions (sleep,
mutexLock, semaphoreWait, messageSend, messageReceive) guard against ISR
calls by checking `inIsrContext()` and returning immediately as no-ops.

This creates a conflict: the SVC handler is in handler mode but is
semantically acting on behalf of a thread. The thread expects the
blocking call to actually block.

Solution: `svcDispatch()` sets `g_inSyscall = true` before dispatching and
clears it after. `inIsrContext()` checks this flag first:

```cpp
bool inIsrContext()
{
    if (g_inSyscall)
        return false;           // treat as thread context
    return (ICSR & 0x1FF) != 0; // real ISR check
}
```

This ensures kernel functions block correctly during SVC dispatch while
still rejecting direct calls from true ISR context (SysTick, peripheral
interrupts).

## TCB Extension

The `ThreadControlBlock` struct gains a `privileged` field at offset 44
(immediately after `mpuStackRasr` at offset 40):

```cpp
struct ThreadControlBlock
{
    std::uint32_t *stackPointer;    // [offset  0]
    ThreadState state;              // [offset  4]
    ThreadId id;                    // [offset  5]
    std::uint8_t basePriority;      // [offset  6]
    std::uint8_t currentPriority;   // [offset  7]
    const char *name;               // [offset  8]
    std::uint32_t *stackBase;       // [offset 12]
    std::uint32_t stackSize;        // [offset 16]
    std::uint32_t timeSliceRemaining; // [offset 20]
    std::uint32_t timeSlice;        // [offset 24]
    ThreadId nextReady;             // [offset 28]
    ThreadId nextWait;              // [offset 29]
    std::uint32_t wakeupTick;       // [offset 32]
    std::uint32_t mpuStackRbar;     // [offset 36]
    std::uint32_t mpuStackRasr;     // [offset 40]
    bool privileged;                // [offset 44] -- NEW
};
```

Assembly code uses `.equ OFFSET_PRIVILEGED, 44` to read this field during
context switch and first-thread launch.

## Syscall Numbers (kernel/inc/kernel/Syscall.h)

24 syscalls numbered 0-23:

| SVC # | Constant | Kernel Function | Args (r0-r3) | Return |
|-------|----------|-----------------|---------------|--------|
| 0 | kStartFirstThread | (assembly only) | -- | -- |
| 1 | kYield | yield() | -- | -- |
| 2 | kSleep | sleep(ticks) | r0=ticks | -- |
| 3 | kTickCount | tickCount() | -- | tick count |
| 4 | kMutexCreate | mutexCreate(name) | r0=name | mutex ID |
| 5 | kMutexDestroy | mutexDestroy(id) | r0=id | -- |
| 6 | kMutexLock | mutexLock(id) | r0=id | 1=ok, 0=fail |
| 7 | kMutexTryLock | mutexTryLock(id) | r0=id | 1=ok, 0=fail |
| 8 | kMutexUnlock | mutexUnlock(id) | r0=id | 1=ok, 0=fail |
| 9 | kSemaphoreCreate | semaphoreCreate(..) | r0=init, r1=max, r2=name | sem ID |
| 10 | kSemaphoreDestroy | semaphoreDestroy(id) | r0=id | -- |
| 11 | kSemaphoreWait | semaphoreWait(id) | r0=id | 1=ok, 0=fail |
| 12 | kSemaphoreTryWait | semaphoreTryWait(id) | r0=id | 1=ok, 0=fail |
| 13 | kSemaphoreSignal | semaphoreSignal(id) | r0=id | 1=ok, 0=fail |
| 14 | kMessageSend | messageSend(dest, msg, reply) | r0=dest, r1=msg*, r2=reply* | IPC status |
| 15 | kMessageReceive | messageReceive(msg) | r0=msg* | IPC status |
| 16 | kMessageReply | messageReply(dest, reply) | r0=dest, r1=reply* | IPC status |
| 17 | kMessageTrySend | messageTrySend(dest, msg) | r0=dest, r1=msg* | IPC status |
| 18 | kMessageTryReceive | messageTryReceive(msg) | r0=msg* | IPC status |
| 19 | kMessageNotify | messageNotify(dest, bits) | r0=dest, r1=bits | IPC status |
| 20 | kMessageCheckNotify | messageCheckNotify() | -- | bitmask |
| 21 | kHeapAlloc | heapAlloc(size) | r0=size | pointer |
| 22 | kHeapFree | heapFree(ptr) | r0=ptr | -- |
| 23 | kHeapGetStats | heapGetStats(stats) | r0=stats* | -- |

SVC 0 is handled entirely in assembly (first-thread launch). SVC 1-23
go through the C function `svcDispatch()`.

## SVC Dispatch (kernel/src/core/SvcDispatch.cpp)

```cpp
extern "C" std::uint32_t svcDispatch(std::uint8_t svcNum, std::uint32_t *frame)
{
    kernel::g_inSyscall = true;

    std::uint32_t result = 0;
    switch (svcNum)
    {
    case kYield:    kernel::yield();       break;
    case kSleep:    kernel::sleep(frame[0]); break;
    case kTickCount: result = kernel::tickCount(); break;
    // ... mutex, semaphore, IPC, heap cases ...
    default: break;
    }

    kernel::g_inSyscall = false;
    return result;
}
```

The exception stack frame layout `[r0, r1, r2, r3, r12, lr, pc, xpsr]` is
used to extract arguments from `frame[0]`-`frame[3]` and the return value is
written back to `frame[0]` (stacked r0) by the assembly caller.

## User-Space Wrappers (kernel::user namespace)

The `kernel::user::` namespace in `Syscall.h` provides typed inline
wrappers. On ARM (`__arm__` defined), each wrapper issues an SVC with
register constraints. On the host build, each wrapper calls the
corresponding kernel function directly (no SVC).

ARM example:

```cpp
namespace kernel::user
{
    inline void sleep(std::uint32_t ticks)
    {
        register std::uint32_t r0 __asm("r0") = ticks;
        __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSleep) : "memory");
    }

    inline std::uint32_t tickCount()
    {
        register std::uint32_t r0 __asm("r0");
        __asm volatile("svc %1" : "=r"(r0) : "I"(syscall::kTickCount) : "memory");
        return r0;
    }
}
```

Host fallback:

```cpp
namespace kernel::user
{
    inline void sleep(std::uint32_t ticks) { kernel::sleep(ticks); }
    inline std::uint32_t tickCount() { return kernel::tickCount(); }
}
```

This dual-compilation approach allows the same application code to run on
hardware (SVC path) and on the host (direct call path) for testing.

## Thread Creation API

The `createThread()` function gains an optional `privileged` parameter
(default: `true`):

```cpp
ThreadId createThread(ThreadFunction function, void *arg, const char *name,
                      std::uint32_t *stack, std::uint32_t stackSize,
                      std::uint8_t priority = kDefaultPriority,
                      std::uint32_t timeSlice = 0,
                      bool privileged = true);
```

All existing code continues to create privileged threads without changes.
Unprivileged threads are created by passing `false`:

```cpp
kernel::createThread(echoClientThread,
                     reinterpret_cast<void *>(serverTid),
                     "echo-cli", stack, sizeof(stack), 10, 0, false);
```

## MPU Interaction

Unprivileged threads (CONTROL.nPRIV=1) are subject to the MPU region
configuration from Phase 3:

- **Flash** (region 0): read-only for all privilege levels
- **KernelSram** (region 1): privileged-only read/write -- blocks
  unprivileged access to kernel globals (.data, .bss)
- **Peripherals** (region 2): privileged-only read/write -- blocks
  unprivileged access to UART, GPIO, SPI, etc.
- **SystemControl** (region 3): privileged-only -- SCB, NVIC, SysTick
- **ThreadStack** (region 4): read/write for all -- per-thread, updated on
  context switch
- **Heap** (region 5): read/write for all -- shared heap region

This means unprivileged threads:
- Cannot read/write kernel globals (pass data via thread argument or stack)
- Cannot access hardware peripherals directly (must use privileged server threads)
- Can access their own stack and the heap

## Application Pattern

The ipc-demo app demonstrates the intended privilege separation model:

```
+---------------------------+       +---------------------------+
| echo-srv (privileged)     |       | echo-cli (unprivileged)   |
| priority 8                |       | priority 10               |
|                           |       |                           |
| kernel::messageReceive()  | <---- | kernel::user::            |
| hal::uartWriteString()    |       |   messageSend(srv, req)   |
| kernel::messageReply()    | ----> | kernel::user::sleep(2000) |
+---------------------------+       +---------------------------+
```

The server thread is privileged and can access UART directly. The client
thread is unprivileged and uses `kernel::user::` SVC wrappers for sleep
and IPC. The server TID is passed via the thread argument since the
client cannot read the global `g_serverTid` (MPU blocks SRAM access).

## Implementation Notes

### Assembly Changes (ContextSwitch.s)

Both Cortex-M3 and Cortex-M4 ContextSwitch.s files are updated
identically. The changes are:

1. **PendSV_Handler**: After restoring the incoming thread's context,
   reads `TCB.privileged` (offset 44) and writes CONTROL register.
   CONTROL=2 for privileged (nPRIV=0, SPSEL=1), CONTROL=3 for
   unprivileged (nPRIV=1, SPSEL=1). An ISB follows the MSR.

2. **SVC_Handler**: Split into two paths:
   - SVC 0: first-thread launch (loads TCB, restores context, sets
     CONTROL, returns to thread mode). Same privilege logic as PendSV.
   - SVC 1+: pushes frame pointer and EXC_RETURN, calls `svcDispatch()`,
     writes return value to stacked r0, returns via `bx lr`.

### Overhead

Total kernel overhead for the syscall layer:

| Component | Size |
|-----------|------|
| TCB.privileged field (per thread) | 1 byte (padded to 4 by struct alignment) |
| SVC_Handler assembly (dispatch path) | ~80 bytes |
| PendSV_Handler CONTROL logic | ~20 bytes |
| SvcDispatch.cpp (switch table) | ~500 bytes |
| **Total** | **~650 bytes** |

### Gotchas

1. **SVC handler runs in handler mode.** Kernel functions that check
   `inIsrContext()` will early-return as no-ops unless `g_inSyscall` is
   set. Forgetting this flag causes sleep, mutex lock, semaphore wait,
   and IPC send/receive to silently fail.

2. **Unprivileged threads cannot access globals.** The MPU KernelSram
   region is privileged-only. An unprivileged thread reading a global
   variable triggers a MemManage fault. Pass data via the thread arg
   parameter or on the stack.

3. **No nested SVC.** Kernel functions called from `svcDispatch()` must
   never issue an SVC instruction. All kernel APIs use direct function
   calls internally, so this is naturally satisfied.

4. **`extern "C" volatile bool` warning.** Defining a volatile variable
   with both `extern` and an initializer triggers a GCC warning under
   `-Werror`. The fix: declare `extern "C"` in the header, define without
   `extern` in the .cpp file.

5. **Cortex-A9 not updated.** The Cortex-A9 port does not implement SVC
   dispatch or unprivileged threads. All A9 threads remain in SYS mode.

## Test Coverage

28 tests across two test files:

### SvcDispatchTest (23 tests in test/kernel/SvcDispatchTest.cpp)

Tests call `svcDispatch()` directly with a simulated exception stack frame
(`uint32_t frame[8]`) and verify each syscall routes to the correct
kernel function.

- Yield: triggers context switch
- Sleep: blocks current thread, sets wakeupTick
- TickCount: returns current tick value
- MutexCreate/Destroy/Lock/TryLock/Unlock: full mutex lifecycle
- SemaphoreCreate/Destroy/Wait/TryWait/Signal: full semaphore lifecycle
- MessageTryReceive: returns kIpcErrEmpty on empty mailbox
- MessageCheckNotify: returns 0 when no notifications pending
- HeapAlloc: returns non-null pointer
- HeapFree: frees allocated block
- HeapGetStats: fills stats struct
- InvalidSvcNumber: returns 0 for unknown SVC number
- Handler-mode tests (g_isrContext=true):
  - Sleep via svcDispatch blocks thread (g_inSyscall overrides ISR check)
  - Sleep direct call is no-op in ISR context
  - MutexLock via svcDispatch succeeds in handler mode
  - SemaphoreWait via svcDispatch succeeds in handler mode
  - g_inSyscall is cleared after dispatch returns

### SyscallTest (5 tests in test/kernel/SyscallTest.cpp)

Tests verify the `privileged` field in the TCB:

- Default ThreadConfig creates privileged thread
- Explicit privileged=false creates unprivileged thread
- createThread() with default parameter creates privileged thread
- createThread() with explicit false creates unprivileged thread
- createThread() with explicit true creates privileged thread

## Files

| File | Purpose |
|------|---------|
| `kernel/inc/kernel/Syscall.h` | SVC number constants + kernel::user:: wrappers |
| `kernel/inc/kernel/Arch.h` | g_inSyscall declaration |
| `kernel/inc/kernel/Thread.h` | TCB privileged field, ThreadConfig privileged field |
| `kernel/inc/kernel/Kernel.h` | createThread() with privileged parameter |
| `kernel/src/core/SvcDispatch.cpp` | svcDispatch() switch table (~150 lines) |
| `kernel/src/core/Kernel.cpp` | createThread() implementation, g_inSyscall definition |
| `kernel/src/arch/cortex-m3/ContextSwitch.s` | SVC_Handler + PendSV CONTROL logic |
| `kernel/src/arch/cortex-m3/Arch.cpp` | g_inSyscall definition, inIsrContext() check |
| `kernel/src/arch/cortex-m4/ContextSwitch.s` | SVC_Handler + PendSV CONTROL logic |
| `kernel/src/arch/cortex-m4/Arch.cpp` | g_inSyscall definition, inIsrContext() check |
| `app/ipc-demo/main.cpp` | Demo: privileged server + unprivileged client |
| `test/kernel/SvcDispatchTest.cpp` | 23 dispatch tests |
| `test/kernel/SyscallTest.cpp` | 5 privilege field tests |
| `test/kernel/MockArch.cpp` | Mock inIsrContext() with g_inSyscall support |
| `test/kernel/MockKernel.h` | g_isrContext + resetKernelMockState() |
