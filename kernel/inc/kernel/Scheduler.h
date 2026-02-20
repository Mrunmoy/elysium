#pragma once

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    class Scheduler
    {
    public:
        // Initialize the scheduler
        void init();

        // Add a thread to the ready queue (at its priority level)
        bool addThread(ThreadId id);

        // Remove a thread from all scheduler lists (thread terminated)
        void removeThread(ThreadId id);

        // Return the highest-priority ready thread (does NOT dequeue)
        ThreadId pickNext();

        // Called every SysTick: check for preemption and time slice expiry.
        // Returns true if a context switch is needed.
        bool tick();

        // Current thread voluntarily yields CPU to same-priority peers
        void yield();

        // Select the next thread and update bookkeeping.
        // Re-enqueues the current thread if still Running, dequeues the
        // highest-priority ready thread, marks it Running.
        // Returns the new thread ID (or idle thread if queue empty).
        ThreadId switchContext();

        // Block the current thread (move from Running to Blocked).
        // Does NOT switch context -- caller must call switchContext() after.
        void blockCurrentThread();

        // Unblock a thread (move from Blocked to Ready, enqueue).
        // Returns true if the unblocked thread has higher priority than current.
        bool unblockThread(ThreadId id);

        // Change a thread's effective priority (for priority inheritance).
        // If the thread is in the ready queue, repositions it.
        void setThreadPriority(ThreadId id, std::uint8_t newPriority);

        // Get the currently running thread ID
        ThreadId currentThreadId() const;

        // Set the currently running thread (used during startup)
        void setCurrentThread(ThreadId id);

        // Set the idle thread ID (fallback when ready queue is empty)
        void setIdleThread(ThreadId id);

        // Get count of threads across all ready lists
        std::uint8_t readyCount() const;

    private:
        void enqueueReady(ThreadId id);
        ThreadId dequeueReady(std::uint8_t priority);
        void removeFromReadyList(ThreadId id, std::uint8_t priority);
        std::uint8_t highestReadyPriority() const;

        // Bitmap: bit N set when priority N has at least one ready thread
        std::uint32_t m_readyBitmap;

        // Per-priority singly-linked lists (head/tail for O(1) append)
        ThreadId m_readyHead[kMaxPriorities];
        ThreadId m_readyTail[kMaxPriorities];

        ThreadId m_currentThreadId;
        ThreadId m_idleThreadId;
        std::uint8_t m_readyCount;
    };

}  // namespace kernel
