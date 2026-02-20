// Priority-sorted wait queue for mutex and semaphore wait lists.
// Highest priority (lowest currentPriority value) at the head.

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

        tcb->nextWait = kInvalidThreadId;

        // Empty queue: insert at head
        if (head == kInvalidThreadId)
        {
            head = id;
            return;
        }

        // Insert before head if higher priority
        ThreadControlBlock *headTcb = threadGetTcb(head);
        if (headTcb != nullptr && tcb->currentPriority < headTcb->currentPriority)
        {
            tcb->nextWait = head;
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

            ThreadId next = prevTcb->nextWait;
            if (next == kInvalidThreadId)
            {
                // Append at end
                prevTcb->nextWait = id;
                return;
            }

            ThreadControlBlock *nextTcb = threadGetTcb(next);
            if (nextTcb != nullptr && tcb->currentPriority < nextTcb->currentPriority)
            {
                // Insert between prev and next
                tcb->nextWait = next;
                prevTcb->nextWait = id;
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
            head = tcb->nextWait;
            tcb->nextWait = kInvalidThreadId;
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

            if (prevTcb->nextWait == id)
            {
                ThreadControlBlock *tcb = threadGetTcb(id);
                if (tcb != nullptr)
                {
                    prevTcb->nextWait = tcb->nextWait;
                    tcb->nextWait = kInvalidThreadId;
                }
                return;
            }

            prev = prevTcb->nextWait;
        }
    }

    bool waitQueueEmpty(ThreadId head)
    {
        return head == kInvalidThreadId;
    }

}  // namespace kernel
