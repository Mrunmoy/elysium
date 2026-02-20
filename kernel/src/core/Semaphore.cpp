// Counting semaphore with priority-sorted wait queue.
//
// Binary semaphore: create with maxCount=1.
// Counting semaphore: create with maxCount>1 to manage a pool of resources.

#include "kernel/Semaphore.h"
#include "kernel/Scheduler.h"
#include "kernel/CortexM.h"
#include "WaitQueue.h"

#include <cstring>

namespace kernel
{
    extern ThreadControlBlock *g_currentTcb;
    extern ThreadControlBlock *g_nextTcb;

    namespace internal
    {
        Scheduler &scheduler();
    }

    static SemaphoreControlBlock s_semPool[kMaxSemaphores];
    static SemaphoreId s_nextSemId = 0;

    void semaphoreReset()
    {
        std::memset(s_semPool, 0, sizeof(s_semPool));
        s_nextSemId = 0;
        for (std::uint8_t i = 0; i < kMaxSemaphores; ++i)
        {
            s_semPool[i].active = false;
            s_semPool[i].waitHead = kInvalidThreadId;
        }
    }

    SemaphoreId semaphoreCreate(std::uint32_t initialCount, std::uint32_t maxCount,
                                const char *name)
    {
        if (s_nextSemId >= kMaxSemaphores || initialCount > maxCount)
        {
            return kInvalidSemaphoreId;
        }

        SemaphoreId id = s_nextSemId++;
        SemaphoreControlBlock &scb = s_semPool[id];
        scb.active = true;
        scb.count = initialCount;
        scb.maxCount = maxCount;
        scb.waitHead = kInvalidThreadId;
        scb.waitCount = 0;
        scb.name = name;

        return id;
    }

    void semaphoreDestroy(SemaphoreId id)
    {
        if (id >= kMaxSemaphores)
        {
            return;
        }
        s_semPool[id].active = false;
    }

    bool semaphoreWait(SemaphoreId id)
    {
        if (id >= kMaxSemaphores || !s_semPool[id].active)
        {
            return false;
        }

        arch::enterCritical();

        SemaphoreControlBlock &scb = s_semPool[id];

        if (scb.count > 0)
        {
            --scb.count;
            arch::exitCritical();
            return true;
        }

        // Count is 0 -- block the current thread
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        waitQueueInsert(scb.waitHead, currentId);
        ++scb.waitCount;

        sched.blockCurrentThread();

        ThreadId nextId = sched.switchContext();
        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            g_nextTcb = nextTcb;
        }

        arch::exitCritical();
        arch::triggerContextSwitch();

        // When we resume here, the semaphore was signaled for us
        return true;
    }

    bool semaphoreTryWait(SemaphoreId id)
    {
        if (id >= kMaxSemaphores || !s_semPool[id].active)
        {
            return false;
        }

        arch::enterCritical();

        SemaphoreControlBlock &scb = s_semPool[id];
        if (scb.count > 0)
        {
            --scb.count;
            arch::exitCritical();
            return true;
        }

        arch::exitCritical();
        return false;
    }

    bool semaphoreSignal(SemaphoreId id)
    {
        if (id >= kMaxSemaphores || !s_semPool[id].active)
        {
            return false;
        }

        arch::enterCritical();

        SemaphoreControlBlock &scb = s_semPool[id];

        // If threads are waiting, wake the highest-priority one
        if (!waitQueueEmpty(scb.waitHead))
        {
            ThreadId waiterId = waitQueueRemoveHead(scb.waitHead);
            --scb.waitCount;

            Scheduler &sched = internal::scheduler();
            bool preempt = sched.unblockThread(waiterId);

            if (preempt)
            {
                ThreadId nextId = sched.switchContext();
                ThreadControlBlock *nextTcb = threadGetTcb(nextId);
                if (nextTcb != nullptr)
                {
                    g_nextTcb = nextTcb;
                }
                arch::exitCritical();
                arch::triggerContextSwitch();
            }
            else
            {
                arch::exitCritical();
            }
            return true;
        }

        // No waiters -- increment count if below max
        if (scb.count >= scb.maxCount)
        {
            arch::exitCritical();
            return false;
        }

        ++scb.count;
        arch::exitCritical();
        return true;
    }

    SemaphoreControlBlock *semaphoreGetBlock(SemaphoreId id)
    {
        if (id >= kMaxSemaphores)
        {
            return nullptr;
        }
        return &s_semPool[id];
    }

}  // namespace kernel
