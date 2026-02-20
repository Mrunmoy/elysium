#pragma once

#include "kernel/Thread.h"

namespace kernel
{
    // Priority-sorted wait queue operations using the nextWait linked list
    // in each TCB.  Highest priority (lowest number) is at the head.

    // Insert thread into wait queue, maintaining priority sort order.
    void waitQueueInsert(ThreadId &head, ThreadId id);

    // Remove and return the highest-priority thread from the wait queue.
    // Returns kInvalidThreadId if the queue is empty.
    ThreadId waitQueueRemoveHead(ThreadId &head);

    // Remove a specific thread from the wait queue (e.g. timeout cancellation).
    void waitQueueRemove(ThreadId &head, ThreadId id);

    // Check if wait queue is empty.
    bool waitQueueEmpty(ThreadId head);

}  // namespace kernel
