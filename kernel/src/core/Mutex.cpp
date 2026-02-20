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
#include "kernel/CortexM.h"
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
            s_mutexPool[i].m_active = false;
            s_mutexPool[i].m_owner = kInvalidThreadId;
            s_mutexPool[i].m_waitHead = kInvalidThreadId;
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
        mcb.m_active = true;
        mcb.m_owner = kInvalidThreadId;
        mcb.m_lockCount = 0;
        mcb.m_waitHead = kInvalidThreadId;
        mcb.m_waitCount = 0;
        mcb.m_name = name;

        return id;
    }

    void mutexDestroy(MutexId id)
    {
        if (id >= kMaxMutexes)
        {
            return;
        }
        s_mutexPool[id].m_active = false;
    }

    bool mutexLock(MutexId id)
    {
        if (id >= kMaxMutexes || !s_mutexPool[id].m_active)
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        if (mcb.m_owner == kInvalidThreadId)
        {
            // Mutex is free -- acquire it
            mcb.m_owner = currentId;
            mcb.m_lockCount = 1;
            arch::exitCritical();
            return true;
        }

        if (mcb.m_owner == currentId)
        {
            // Recursive lock by same thread
            ++mcb.m_lockCount;
            arch::exitCritical();
            return true;
        }

        // Mutex held by another thread -- block and apply priority inheritance
        ThreadControlBlock *currentTcb = threadGetTcb(currentId);
        ThreadControlBlock *ownerTcb = threadGetTcb(mcb.m_owner);

        // Priority inheritance: boost the owner if we have higher priority
        if (currentTcb != nullptr && ownerTcb != nullptr)
        {
            if (currentTcb->m_currentPriority < ownerTcb->m_currentPriority)
            {
                sched.setThreadPriority(mcb.m_owner, currentTcb->m_currentPriority);
            }
        }

        // Add current thread to the mutex wait queue
        waitQueueInsert(mcb.m_waitHead, currentId);
        ++mcb.m_waitCount;

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
        if (id >= kMaxMutexes || !s_mutexPool[id].m_active)
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        if (mcb.m_owner == kInvalidThreadId)
        {
            mcb.m_owner = currentId;
            mcb.m_lockCount = 1;
            arch::exitCritical();
            return true;
        }

        if (mcb.m_owner == currentId)
        {
            ++mcb.m_lockCount;
            arch::exitCritical();
            return true;
        }

        arch::exitCritical();
        return false;
    }

    bool mutexUnlock(MutexId id)
    {
        if (id >= kMaxMutexes || !s_mutexPool[id].m_active)
        {
            return false;
        }

        arch::enterCritical();

        MutexControlBlock &mcb = s_mutexPool[id];
        Scheduler &sched = internal::scheduler();
        ThreadId currentId = sched.currentThreadId();

        // Only the owner can unlock
        if (mcb.m_owner != currentId)
        {
            arch::exitCritical();
            return false;
        }

        --mcb.m_lockCount;
        if (mcb.m_lockCount > 0)
        {
            // Still recursively locked
            arch::exitCritical();
            return true;
        }

        // Restore the owner's base priority (undo inheritance)
        ThreadControlBlock *ownerTcb = threadGetTcb(currentId);
        if (ownerTcb != nullptr &&
            ownerTcb->m_currentPriority != ownerTcb->m_basePriority)
        {
            sched.setThreadPriority(currentId, ownerTcb->m_basePriority);
        }

        // Release the mutex
        mcb.m_owner = kInvalidThreadId;

        // Wake the highest-priority waiter, if any
        bool preempt = false;
        if (!waitQueueEmpty(mcb.m_waitHead))
        {
            ThreadId waiterId = waitQueueRemoveHead(mcb.m_waitHead);
            --mcb.m_waitCount;

            // Transfer ownership to the waiter
            mcb.m_owner = waiterId;
            mcb.m_lockCount = 1;

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
