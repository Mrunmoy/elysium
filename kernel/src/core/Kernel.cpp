// Kernel core: initialization, scheduler startup, tick handler, thread exit.
//
// Defines the global TCB pointers used by the PendSV assembly handler
// and the SysTick_Handler ISR that drives the scheduler tick.

#include "kernel/Kernel.h"
#include "kernel/Scheduler.h"
#include "kernel/CortexM.h"
#include "kernel/CrashDump.h"
#include "kernel/Heap.h"
#include "kernel/Mpu.h"
#include "startup/SystemClock.h"

#include <cstdint>

// Linker-provided heap region symbols (defined in Linker.ld)
extern "C" std::uint8_t _heap_start;
extern "C" std::uint8_t _heap_end;

namespace kernel
{
    // Global TCB pointers read/written by ContextSwitch.s (PendSV_Handler)
    // Must be extern "C" for assembly linkage (declared in CortexM.h)
    ThreadControlBlock *g_currentTcb = nullptr;
    ThreadControlBlock *g_nextTcb = nullptr;

    // Kernel-internal state
    static Scheduler s_scheduler;
    static volatile std::uint32_t s_tickCount = 0;

    namespace internal
    {
        Scheduler &scheduler() { return s_scheduler; }
    }  // namespace internal

    // Idle thread -- runs when no other threads are ready
    alignas(512) static std::uint32_t s_idleStack[128];  // 512 bytes

    static void idleThreadFunc(void *)
    {
        while (true)
        {
            // WFI would go here on real hardware to save power
        }
    }

    // Check sleeping threads and wake any whose timeout has expired.
    // Called from SysTick_Handler.
    static void checkSleepingThreads()
    {
        std::uint32_t now = s_tickCount;
        for (ThreadId i = 0; i < kMaxThreads; ++i)
        {
            ThreadControlBlock *tcb = threadGetTcb(i);
            if (tcb == nullptr)
            {
                continue;
            }
            if (tcb->state == ThreadState::Blocked && tcb->wakeupTick != 0)
            {
                if (now >= tcb->wakeupTick)
                {
                    tcb->wakeupTick = 0;
                    s_scheduler.unblockThread(i);
                }
            }
        }
    }

    void init()
    {
        crashDumpInit();
        heapInit(&_heap_start, &_heap_end);
        mpuInit();
        threadReset();
        s_scheduler.init();
        s_tickCount = 0;
        g_currentTcb = nullptr;
        g_nextTcb = nullptr;

        // Create idle thread (always ID 0 = kIdleThreadId)
        ThreadConfig idleConfig{};
        idleConfig.function = idleThreadFunc;
        idleConfig.arg = nullptr;
        idleConfig.name = "idle";
        idleConfig.stack = s_idleStack;
        idleConfig.stackSize = sizeof(s_idleStack);
        idleConfig.priority = kIdlePriority;
        idleConfig.timeSlice = 1;

        ThreadId idleId = threadCreate(idleConfig);
        s_scheduler.setIdleThread(idleId);
        // Idle thread is NOT in the ready queue -- scheduler falls back to it
    }

    ThreadId createThread(ThreadFunction function, void *arg, const char *name,
                          std::uint32_t *stack, std::uint32_t stackSize,
                          std::uint8_t priority, std::uint32_t timeSlice)
    {
        ThreadConfig config{};
        config.function = function;
        config.arg = arg;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.priority = priority;
        config.timeSlice = timeSlice;

        ThreadId id = threadCreate(config);
        if (id == kInvalidThreadId)
        {
            return kInvalidThreadId;
        }

        s_scheduler.addThread(id);
        return id;
    }

    void startScheduler()
    {
        // Configure interrupt priorities: PendSV lowest, SysTick next-lowest
        arch::setInterruptPriorities();

        // Use switchContext to dequeue the first thread.
        // Since m_currentThreadId is kInvalidThreadId (no thread running),
        // switchContext will just dequeue the front thread and mark it Running.
        ThreadId firstId = s_scheduler.switchContext();
        ThreadControlBlock *firstTcb = threadGetTcb(firstId);
        g_currentTcb = firstTcb;
        g_nextTcb = firstTcb;

        // Configure SysTick for 1 ms tick (SystemCoreClock / 1000)
        arch::configureSysTick(SystemCoreClock / 1000);

        // Launch the first thread via SVC -- does not return
        arch::startFirstThread();
    }

    void yield()
    {
        arch::enterCritical();

        // Reset time slice and rotate the ready queue
        s_scheduler.yield();
        ThreadId nextId = s_scheduler.switchContext();
        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            g_nextTcb = nextTcb;
        }

        arch::exitCritical();
        arch::triggerContextSwitch();
    }

    void sleep(std::uint32_t ticks)
    {
        if (ticks == 0)
        {
            yield();
            return;
        }

        arch::enterCritical();

        ThreadControlBlock *tcb = threadGetTcb(s_scheduler.currentThreadId());
        if (tcb != nullptr)
        {
            tcb->wakeupTick = s_tickCount + ticks;
        }

        s_scheduler.blockCurrentThread();

        ThreadId nextId = s_scheduler.switchContext();
        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            g_nextTcb = nextTcb;
        }

        arch::exitCritical();
        arch::triggerContextSwitch();
    }

    std::uint32_t tickCount()
    {
        return s_tickCount;
    }

    // Called when a thread function returns (placed in LR of initial stack frame)
    void kernelThreadExit()
    {
        // Remove the current thread from the scheduler
        ThreadId currentId = s_scheduler.currentThreadId();
        s_scheduler.removeThread(currentId);

        // Trigger context switch to next thread
        arch::triggerContextSwitch();

        // Should never reach here
        while (true)
        {
        }
    }

}  // namespace kernel

// SysTick ISR -- called every 1 ms by hardware
extern "C" void SysTick_Handler()
{
    ++kernel::s_tickCount;

    // Wake threads whose sleep timer has expired
    kernel::checkSleepingThreads();

    // Run scheduler tick (check preemption, time-slice expiry)
    bool switchNeeded = kernel::s_scheduler.tick();

    if (switchNeeded)
    {
        // Perform context switch bookkeeping:
        // Move current to back of ready queue, dequeue highest-priority ready thread
        kernel::ThreadId nextId = kernel::s_scheduler.switchContext();
        kernel::ThreadControlBlock *nextTcb = kernel::threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            kernel::g_nextTcb = nextTcb;
        }
        kernel::arch::triggerContextSwitch();
    }
}
