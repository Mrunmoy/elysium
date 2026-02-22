// Mutex with priority inheritance.
//
// When a high-priority thread blocks on a mutex held by a lower-priority
// thread, the holder's effective priority is temporarily boosted to prevent
// unbounded priority inversion.  On unlock, the holder's priority is restored.
//
// Recursive locking is supported: the same thread can lock a mutex multiple
// times and must unlock it the same number of times.

#include "kernel/Mutex.h"
#include "kernel/Scheduler.h"
#include "kernel/Arch.h"
#include "WaitQueue.h"

#include <cstring>

namespace kernel
{
    // Forward declarations for kernel-internal scheduler access
    // (defined in Kernel.cpp, not in any public header)
    extern ThreadControlBlock *g_currentTcb;
    extern ThreadControlBlock *g_nextTcb;

    // The scheduler instance is local to Kernel.cpp.  Mutex operations need
    // to call the scheduler, so we declare an accessor here.
    // Alternative: make the scheduler accessible via a kernel-internal header.
    // For now, we declare the functions we need and link against Kernel.cpp.

    // We need access to the scheduler.  Rather than exposing the static instance,
    // we use the public kernel API (yield, etc.) and provide scheduler access
    // through a small internal interface.

}  // namespace kernel

// The scheduler is a static local in Kernel.cpp.  To avoid tight coupling,
// Mutex uses the arch layer for critical sections and context switches, and
// the Scheduler is accessed via a friend-style internal header.
// For simplicity in this RTOS, we expose a scheduler accessor.

namespace kernel
{
    // Internal scheduler accessor (defined in Kernel.cpp via a separate mechanism).
    // To keep things clean, we'll define a minimal internal interface.
    namespace internal
    {
        Scheduler &scheduler();
    }

    static MutexControlBlock s_mutexPool[kMaxMutexes];
    static MutexId s_nextMutexId = 0;

    void mutexReset()
    {
        std::memset(s_mutexPool, 0, sizeof(s_mutexPool));
        s_nextMutexId = 0;
        for (std::uint8_t i = 0; i < kMaxMutexes; ++i)
        {
            s_mutexPool[i].active = false;
            s_mutexPool[i].owner = kInvalidThreadId;
            s_mutexPool[i].waitHead = kInvalidThreadId;
        }
    }

    MutexId mutexCreate(const char *name)
    {
        if (s_nextMutexId >= kMaxMutexes)
        {
            return kInvalidMutexId;
        }

        MutexId id = s_nextMutexId++;
        MutexControlBlock &mcb = s_mutexPool[id];
        mcb.active = true;
        mcb.owner = kInvalidThreadId;
        mcb.lockCount = 0;
        mcb.waitHead = kInvalidThreadId;
        mcb.waitCount = 0;
        mcb.name = name;

        return id;
    }

    void mutexDestroy(MutexId id)
    {
        if (id >= kMaxMutexes)
        {
            return;
        }
        s_mutexPool[id].active = false;
    }

    bool mutexLock(MutexId id)
    {
        if (id >= kMaxMutexes || !s_mutexPool[id].active)
        {
            return false;
        }

        if (arch::inIsrContext())
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        if (mcb.owner == kInvalidThreadId)
        {
            // Mutex is free -- acquire it
            mcb.owner = currentId;
            mcb.lockCount = 1;
            arch::exitCritical();
            return true;
        }

        if (mcb.owner == currentId)
        {
            // Recursive lock by same thread
            ++mcb.lockCount;
            arch::exitCritical();
            return true;
        }

        // Mutex held by another thread -- block and apply priority inheritance
        ThreadControlBlock *currentTcb = threadGetTcb(currentId);
        ThreadControlBlock *ownerTcb = threadGetTcb(mcb.owner);

        // Priority inheritance: boost the owner if we have higher priority
        if (currentTcb != nullptr && ownerTcb != nullptr)
        {
            if (currentTcb->currentPriority < ownerTcb->currentPriority)
            {
                sched.setThreadPriority(mcb.owner, currentTcb->currentPriority);
            }
        }

        // Add current thread to the mutex wait queue
        waitQueueInsert(mcb.waitHead, currentId);
        ++mcb.waitCount;

        // Block the current thread
        sched.blockCurrentThread();

        // Switch to the next ready thread
        ThreadId nextId = sched.switchContext();
        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            g_nextTcb = nextTcb;
        }

        arch::exitCritical();
        arch::triggerContextSwitch();

        // When we resume here, we hold the mutex
        return true;
    }

    bool mutexTryLock(MutexId id)
    {
        if (id >= kMaxMutexes || !s_mutexPool[id].active)
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        if (mcb.owner == kInvalidThreadId)
        {
            mcb.owner = currentId;
            mcb.lockCount = 1;
            arch::exitCritical();
            return true;
        }

        if (mcb.owner == currentId)
        {
            ++mcb.lockCount;
            arch::exitCritical();
            return true;
        }

        arch::exitCritical();
        return false;
    }

    bool mutexUnlock(MutexId id)
    {
        if (id >= kMaxMutexes || !s_mutexPool[id].active)
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        // Only the owner can unlock
        if (mcb.owner != currentId)
        {
            arch::exitCritical();
            return false;
        }

        --mcb.lockCount;
        if (mcb.lockCount > 0)
        {
            // Still recursively locked
            arch::exitCritical();
            return true;
        }

        // Restore priority: scan all other mutexes still held by this thread
        // and keep the highest inherited priority (lowest numeric value).
        ThreadControlBlock *ownerTcb = threadGetTcb(currentId);
        if (ownerTcb != nullptr &&
            ownerTcb->currentPriority != ownerTcb->basePriority)
        {
            std::uint8_t restorePriority = ownerTcb->basePriority;
            for (MutexId m = 0; m < kMaxMutexes; ++m)
            {
                if (m == id)
                {
                    continue;  // Skip the mutex being released
                }
                if (!s_mutexPool[m].active || s_mutexPool[m].owner != currentId)
                {
                    continue;
                }
                // Check the highest-priority waiter on this other held mutex
                ThreadId wid = s_mutexPool[m].waitHead;
                while (wid != kInvalidThreadId)
                {
                    ThreadControlBlock *wtcb = threadGetTcb(wid);
                    if (wtcb != nullptr && wtcb->currentPriority < restorePriority)
                    {
                        restorePriority = wtcb->currentPriority;
                    }
                    wid = wtcb->nextWait;
                }
            }
            sched.setThreadPriority(currentId, restorePriority);
        }

        // Release the mutex
        mcb.owner = kInvalidThreadId;

        // Wake the highest-priority waiter, if any
        bool preempt = false;
        if (!waitQueueEmpty(mcb.waitHead))
        {
            ThreadId waiterId = waitQueueRemoveHead(mcb.waitHead);
            --mcb.waitCount;

            // Transfer ownership to the waiter
            mcb.owner = waiterId;
            mcb.lockCount = 1;

            // Unblock the waiter
            preempt = sched.unblockThread(waiterId);
        }

        if (preempt)
        {
            // Woken thread has higher priority -- switch
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

    MutexControlBlock *mutexGetBlock(MutexId id)
    {
        if (id >= kMaxMutexes)
        {
            return nullptr;
        }
        return &s_mutexPool[id];
    }

}  // namespace kernel
