// Mock kernel globals for host-side testing.
// Provides g_currentTcb, g_nextTcb, kernelThreadExit, tickCount, and the
// internal::scheduler() accessor that Mutex.cpp/Semaphore.cpp need.

#include "kernel/Arch.h"
#include "kernel/Kernel.h"
#include "kernel/Ipc.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"
#include "MockKernel.h"

namespace kernel
{
    ThreadControlBlock *g_currentTcb = nullptr;
    ThreadControlBlock *g_nextTcb = nullptr;

    void kernelThreadExit()
    {
        ThreadId currentId = internal::scheduler().currentThreadId();
        arch::enterCritical();
        internal::scheduler().removeThread(currentId);
        ipcResetMailbox(currentId);
        threadDestroy(currentId);
        arch::exitCritical();
        arch::triggerContextSwitch();
    }

    std::uint32_t tickCount()
    {
        return test::g_tickCount;
    }

    ThreadId createThread(ThreadFunction function, void *arg, const char *name,
                          std::uint32_t *stack, std::uint32_t stackSize,
                          std::uint8_t priority, std::uint32_t timeSlice,
                          bool privileged)
    {
        ThreadConfig config{};
        config.function = function;
        config.arg = arg;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.priority = priority;
        config.timeSlice = timeSlice;
        config.privileged = privileged;

        ThreadId id = threadCreate(config);
        if (id == kInvalidThreadId)
        {
            return kInvalidThreadId;
        }

        internal::scheduler().addThread(id);
        return id;
    }

    void yield()
    {
        arch::enterCritical();
        internal::scheduler().yield();
        internal::scheduler().switchContext();
        arch::exitCritical();
        arch::triggerContextSwitch();
    }

    void sleep(std::uint32_t ticks)
    {
        // Match real Kernel.cpp: sleep is a no-op from ISR context
        if (arch::inIsrContext())
        {
            return;
        }

        if (ticks == 0)
        {
            yield();
            return;
        }

        arch::enterCritical();

        ThreadControlBlock *tcb = threadGetTcb(internal::scheduler().currentThreadId());
        if (tcb != nullptr)
        {
            tcb->wakeupTick = test::g_tickCount + ticks;
        }

        internal::scheduler().blockCurrentThread();
        internal::scheduler().switchContext();

        arch::exitCritical();
        arch::triggerContextSwitch();
    }

    bool destroyThread(ThreadId id)
    {
        if (id == internal::scheduler().idleThreadId() || id >= kMaxThreads)
        {
            return false;
        }

        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr || tcb->state == ThreadState::Inactive)
        {
            return false;
        }

        arch::enterCritical();
        internal::scheduler().removeThread(id);
        ipcResetMailbox(id);
        threadDestroy(id);
        arch::exitCritical();
        return true;
    }

    void watchdogStart(std::uint16_t /* reloadValue */, std::uint8_t /* prescaler */)
    {
        test::g_watchdogRunning = true;
    }

    bool watchdogRunning()
    {
        return test::g_watchdogRunning;
    }

    namespace internal
    {
        static Scheduler s_testScheduler;

        Scheduler &scheduler()
        {
            return s_testScheduler;
        }
    }  // namespace internal

}  // namespace kernel
