// Round-robin scheduler with time-slicing.
// This module has zero hardware dependencies -- fully testable on host.

#include "kernel/Scheduler.h"
#include "kernel/CortexM.h"

#include <cstring>

namespace kernel
{
    void Scheduler::init()
    {
        m_readyCount = 0;
        m_readyHead = 0;
        m_currentThreadId = kInvalidThreadId;
        m_idleThreadId = kInvalidThreadId;
        std::memset(m_readyQueue, kInvalidThreadId, sizeof(m_readyQueue));
    }

    void Scheduler::enqueue(ThreadId id)
    {
        if (m_readyCount >= kMaxThreads)
        {
            return;
        }
        std::uint8_t tail = (m_readyHead + m_readyCount) % kMaxThreads;
        m_readyQueue[tail] = id;
        ++m_readyCount;
    }

    ThreadId Scheduler::dequeue()
    {
        if (m_readyCount == 0)
        {
            return kInvalidThreadId;
        }
        ThreadId id = m_readyQueue[m_readyHead];
        m_readyHead = (m_readyHead + 1) % kMaxThreads;
        --m_readyCount;
        return id;
    }

    bool Scheduler::addThread(ThreadId id)
    {
        if (m_readyCount >= kMaxThreads)
        {
            return false;
        }
        enqueue(id);
        return true;
    }

    void Scheduler::removeThread(ThreadId id)
    {
        // Find and remove from ready queue, compacting
        for (std::uint8_t i = 0; i < m_readyCount; ++i)
        {
            std::uint8_t idx = (m_readyHead + i) % kMaxThreads;
            if (m_readyQueue[idx] == id)
            {
                // Shift remaining entries forward
                for (std::uint8_t j = i; j < m_readyCount - 1; ++j)
                {
                    std::uint8_t from = (m_readyHead + j + 1) % kMaxThreads;
                    std::uint8_t to = (m_readyHead + j) % kMaxThreads;
                    m_readyQueue[to] = m_readyQueue[from];
                }
                --m_readyCount;

                // Mark TCB as inactive
                ThreadControlBlock *tcb = threadGetTcb(id);
                if (tcb != nullptr)
                {
                    tcb->m_state = ThreadState::Inactive;
                }
                return;
            }
        }

        // Also check if it's the currently running thread
        if (m_currentThreadId == id)
        {
            ThreadControlBlock *tcb = threadGetTcb(id);
            if (tcb != nullptr)
            {
                tcb->m_state = ThreadState::Inactive;
            }
            m_currentThreadId = kInvalidThreadId;
        }
    }

    ThreadId Scheduler::pickNext()
    {
        if (m_readyCount == 0)
        {
            return m_idleThreadId;
        }
        return m_readyQueue[m_readyHead];
    }

    bool Scheduler::tick()
    {
        if (m_currentThreadId == kInvalidThreadId)
        {
            return false;
        }

        // If running the idle thread, check if real threads are ready
        if (m_currentThreadId == m_idleThreadId)
        {
            if (m_readyCount > 0)
            {
                arch::triggerContextSwitch();
                return true;
            }
            return false;
        }

        ThreadControlBlock *tcb = threadGetTcb(m_currentThreadId);
        if (tcb == nullptr)
        {
            return false;
        }

        if (tcb->m_timeSliceRemaining > 0)
        {
            --tcb->m_timeSliceRemaining;
        }

        if (tcb->m_timeSliceRemaining == 0)
        {
            // Time slice expired: trigger context switch
            tcb->m_timeSliceRemaining = tcb->m_timeSlice;
            arch::triggerContextSwitch();
            return true;
        }

        return false;
    }

    void Scheduler::yield()
    {
        if (m_currentThreadId == kInvalidThreadId)
        {
            return;
        }

        ThreadControlBlock *tcb = threadGetTcb(m_currentThreadId);
        if (tcb != nullptr)
        {
            tcb->m_timeSliceRemaining = tcb->m_timeSlice;
        }

        arch::triggerContextSwitch();
    }

    ThreadId Scheduler::switchContext()
    {
        ThreadId oldId = m_currentThreadId;
        ThreadControlBlock *oldTcb = threadGetTcb(oldId);

        // Put the outgoing thread back in the ready queue (if still active)
        if (oldTcb != nullptr && oldTcb->m_state == ThreadState::Running)
        {
            oldTcb->m_state = ThreadState::Ready;
            enqueue(oldId);
        }

        // Dequeue the next thread
        ThreadId nextId = dequeue();
        if (nextId == kInvalidThreadId)
        {
            // No ready threads -- run idle
            nextId = m_idleThreadId;
        }

        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            nextTcb->m_state = ThreadState::Running;
        }

        m_currentThreadId = nextId;
        return nextId;
    }

    ThreadId Scheduler::currentThreadId() const
    {
        return m_currentThreadId;
    }

    void Scheduler::setCurrentThread(ThreadId id)
    {
        m_currentThreadId = id;
    }

    void Scheduler::setIdleThread(ThreadId id)
    {
        m_idleThreadId = id;
    }

    std::uint8_t Scheduler::readyCount() const
    {
        return m_readyCount;
    }

}  // namespace kernel
