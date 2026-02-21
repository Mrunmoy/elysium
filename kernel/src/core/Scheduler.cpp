// Priority-preemptive scheduler with per-priority ready lists and time-slicing.
// This module has zero hardware dependencies -- fully testable on host.

#include "kernel/Scheduler.h"
#include "kernel/Arch.h"

#include <cstring>

namespace kernel
{
    void Scheduler::init()
    {
        m_readyBitmap = 0;
        m_readyCount = 0;
        m_currentThreadId = kInvalidThreadId;
        m_idleThreadId = kInvalidThreadId;
        std::memset(m_readyHead, kInvalidThreadId, sizeof(m_readyHead));
        std::memset(m_readyTail, kInvalidThreadId, sizeof(m_readyTail));
    }

    // ---- Private helpers ----

    std::uint8_t Scheduler::highestReadyPriority() const
    {
        if (m_readyBitmap == 0)
        {
            return kMaxPriorities;  // No ready threads
        }
        // Lowest set bit = highest priority (priority 0 = bit 0)
        return static_cast<std::uint8_t>(__builtin_ctz(m_readyBitmap));
    }

    void Scheduler::enqueueReady(ThreadId id)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr)
        {
            return;
        }

        std::uint8_t pri = tcb->currentPriority;
        tcb->nextReady = kInvalidThreadId;

        if (m_readyHead[pri] == kInvalidThreadId)
        {
            // Empty list at this priority
            m_readyHead[pri] = id;
            m_readyTail[pri] = id;
            m_readyBitmap |= (1U << pri);
        }
        else
        {
            // Append to tail (FIFO within same priority for round-robin)
            ThreadControlBlock *tail = threadGetTcb(m_readyTail[pri]);
            if (tail != nullptr)
            {
                tail->nextReady = id;
            }
            m_readyTail[pri] = id;
        }

        ++m_readyCount;
    }

    ThreadId Scheduler::dequeueReady(std::uint8_t priority)
    {
        if (priority >= kMaxPriorities)
        {
            return kInvalidThreadId;
        }

        ThreadId id = m_readyHead[priority];
        if (id == kInvalidThreadId)
        {
            return kInvalidThreadId;
        }

        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb != nullptr)
        {
            m_readyHead[priority] = tcb->nextReady;
            tcb->nextReady = kInvalidThreadId;
        }
        else
        {
            m_readyHead[priority] = kInvalidThreadId;
        }

        // If list is now empty, clear the tail and bitmap bit
        if (m_readyHead[priority] == kInvalidThreadId)
        {
            m_readyTail[priority] = kInvalidThreadId;
            m_readyBitmap &= ~(1U << priority);
        }

        --m_readyCount;
        return id;
    }

    void Scheduler::removeFromReadyList(ThreadId id, std::uint8_t priority)
    {
        if (priority >= kMaxPriorities || m_readyHead[priority] == kInvalidThreadId)
        {
            return;
        }

        // Special case: removing the head
        if (m_readyHead[priority] == id)
        {
            dequeueReady(priority);
            return;
        }

        // Walk the list to find the predecessor
        ThreadId prev = m_readyHead[priority];
        while (prev != kInvalidThreadId)
        {
            ThreadControlBlock *prevTcb = threadGetTcb(prev);
            if (prevTcb == nullptr)
            {
                break;
            }

            if (prevTcb->nextReady == id)
            {
                // Found it -- unlink
                ThreadControlBlock *tcb = threadGetTcb(id);
                if (tcb != nullptr)
                {
                    prevTcb->nextReady = tcb->nextReady;
                    tcb->nextReady = kInvalidThreadId;
                }

                // Update tail if we removed the last element
                if (m_readyTail[priority] == id)
                {
                    m_readyTail[priority] = prev;
                }

                --m_readyCount;

                // Clear bitmap bit if list is now empty
                if (m_readyHead[priority] == kInvalidThreadId)
                {
                    m_readyTail[priority] = kInvalidThreadId;
                    m_readyBitmap &= ~(1U << priority);
                }
                return;
            }

            prev = prevTcb->nextReady;
        }
    }

    // ---- Public API ----

    bool Scheduler::addThread(ThreadId id)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr)
        {
            return false;
        }

        tcb->state = ThreadState::Ready;
        enqueueReady(id);
        return true;
    }

    void Scheduler::removeThread(ThreadId id)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr)
        {
            return;
        }

        // Remove from ready list if in queue
        if (tcb->state == ThreadState::Ready)
        {
            removeFromReadyList(id, tcb->currentPriority);
        }

        tcb->state = ThreadState::Inactive;

        if (m_currentThreadId == id)
        {
            m_currentThreadId = kInvalidThreadId;
        }
    }

    ThreadId Scheduler::pickNext()
    {
        std::uint8_t pri = highestReadyPriority();
        if (pri >= kMaxPriorities)
        {
            return m_idleThreadId;
        }
        return m_readyHead[pri];
    }

    bool Scheduler::tick()
    {
        if (m_currentThreadId == kInvalidThreadId)
        {
            return false;
        }

        ThreadControlBlock *tcb = threadGetTcb(m_currentThreadId);
        if (tcb == nullptr)
        {
            return false;
        }

        // Check for preemption: higher-priority thread became ready
        std::uint8_t highPri = highestReadyPriority();
        if (highPri < kMaxPriorities && highPri < tcb->currentPriority)
        {
            return true;
        }

        // Idle thread: switch if any real thread is ready
        if (m_currentThreadId == m_idleThreadId)
        {
            return (m_readyCount > 0);
        }

        // Time-slice within same priority
        if (tcb->timeSliceRemaining > 0)
        {
            --tcb->timeSliceRemaining;
        }

        if (tcb->timeSliceRemaining == 0)
        {
            // Check if same-priority peers exist in the ready queue
            if (m_readyBitmap & (1U << tcb->currentPriority))
            {
                return true;
            }
            // No peers; reset slice and continue
            tcb->timeSliceRemaining = tcb->timeSlice;
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
            tcb->timeSliceRemaining = tcb->timeSlice;
        }
    }

    ThreadId Scheduler::switchContext()
    {
        ThreadId oldId = m_currentThreadId;
        ThreadControlBlock *oldTcb = threadGetTcb(oldId);

        // Re-enqueue the outgoing thread if still Running
        if (oldTcb != nullptr && oldTcb->state == ThreadState::Running)
        {
            oldTcb->state = ThreadState::Ready;
            enqueueReady(oldId);
        }

        // Select the highest-priority ready thread
        std::uint8_t pri = highestReadyPriority();
        ThreadId nextId;
        if (pri < kMaxPriorities)
        {
            nextId = dequeueReady(pri);
        }
        else
        {
            nextId = m_idleThreadId;
        }

        ThreadControlBlock *nextTcb = threadGetTcb(nextId);
        if (nextTcb != nullptr)
        {
            nextTcb->state = ThreadState::Running;
            nextTcb->timeSliceRemaining = nextTcb->timeSlice;
        }

        m_currentThreadId = nextId;
        return nextId;
    }

    void Scheduler::blockCurrentThread()
    {
        ThreadControlBlock *tcb = threadGetTcb(m_currentThreadId);
        if (tcb == nullptr)
        {
            return;
        }

        tcb->state = ThreadState::Blocked;
        // Thread is NOT re-enqueued in the ready list -- it's blocked
    }

    bool Scheduler::unblockThread(ThreadId id)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr || tcb->state != ThreadState::Blocked)
        {
            return false;
        }

        tcb->state = ThreadState::Ready;
        enqueueReady(id);

        // Return true if unblocked thread has higher priority than current
        ThreadControlBlock *curTcb = threadGetTcb(m_currentThreadId);
        if (curTcb == nullptr)
        {
            return true;
        }
        return tcb->currentPriority < curTcb->currentPriority;
    }

    void Scheduler::setThreadPriority(ThreadId id, std::uint8_t newPriority)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr || newPriority >= kMaxPriorities)
        {
            return;
        }

        std::uint8_t oldPriority = tcb->currentPriority;
        if (oldPriority == newPriority)
        {
            return;
        }

        // If thread is in the ready queue, reposition it
        if (tcb->state == ThreadState::Ready)
        {
            removeFromReadyList(id, oldPriority);
            tcb->currentPriority = newPriority;
            enqueueReady(id);
        }
        else
        {
            tcb->currentPriority = newPriority;
        }
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
