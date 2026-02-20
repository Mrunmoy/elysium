// Priority-sorted wait queue for mutex and semaphore wait lists.
// Highest priority (lowest m_currentPriority value) at the head.

#include "WaitQueue.h"

namespace kernel
{
    void waitQueueInsert(ThreadId &head, ThreadId id)
    {
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb == nullptr)
        {
            return;
        }

        tcb->m_nextWait = kInvalidThreadId;

        // Empty queue: insert at head
        if (head == kInvalidThreadId)
        {
            head = id;
            return;
        }

        // Insert before head if higher priority
        ThreadControlBlock *headTcb = threadGetTcb(head);
        if (headTcb != nullptr && tcb->m_currentPriority < headTcb->m_currentPriority)
        {
            tcb->m_nextWait = head;
            head = id;
            return;
        }

        // Walk the list to find insertion point (sorted ascending by priority number)
        ThreadId prev = head;
        while (prev != kInvalidThreadId)
        {
            ThreadControlBlock *prevTcb = threadGetTcb(prev);
            if (prevTcb == nullptr)
            {
                break;
            }

            ThreadId next = prevTcb->m_nextWait;
            if (next == kInvalidThreadId)
            {
                // Append at end
                prevTcb->m_nextWait = id;
                return;
            }

            ThreadControlBlock *nextTcb = threadGetTcb(next);
            if (nextTcb != nullptr && tcb->m_currentPriority < nextTcb->m_currentPriority)
            {
                // Insert between prev and next
                tcb->m_nextWait = next;
                prevTcb->m_nextWait = id;
                return;
            }

            prev = next;
        }
    }

    ThreadId waitQueueRemoveHead(ThreadId &head)
    {
        if (head == kInvalidThreadId)
        {
            return kInvalidThreadId;
        }

        ThreadId id = head;
        ThreadControlBlock *tcb = threadGetTcb(id);
        if (tcb != nullptr)
        {
            head = tcb->m_nextWait;
            tcb->m_nextWait = kInvalidThreadId;
        }
        else
        {
            head = kInvalidThreadId;
        }

        return id;
    }

    void waitQueueRemove(ThreadId &head, ThreadId id)
    {
        if (head == kInvalidThreadId)
        {
            return;
        }

        // Special case: removing the head
        if (head == id)
        {
            waitQueueRemoveHead(head);
            return;
        }

        // Walk the list to find the predecessor
        ThreadId prev = head;
        while (prev != kInvalidThreadId)
        {
            ThreadControlBlock *prevTcb = threadGetTcb(prev);
            if (prevTcb == nullptr)
            {
                break;
            }

            if (prevTcb->m_nextWait == id)
            {
                ThreadControlBlock *tcb = threadGetTcb(id);
                if (tcb != nullptr)
                {
                    prevTcb->m_nextWait = tcb->m_nextWait;
                    tcb->m_nextWait = kInvalidThreadId;
                }
                return;
            }

            prev = prevTcb->m_nextWait;
        }
    }

    bool waitQueueEmpty(ThreadId head)
    {
        return head == kInvalidThreadId;
    }

}  // namespace kernel
