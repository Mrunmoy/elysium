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

        // Add a thread to the ready queue
        bool addThread(ThreadId id);

        // Remove a thread from the ready queue (thread terminated)
        void removeThread(ThreadId id);

        // Pick the next thread to run (round-robin), does NOT dequeue.
        // Returns kInvalidThreadId if no ready threads (idle should run).
        ThreadId pickNext();

        // Called every SysTick: decrement time slice, trigger switch if expired.
        // Returns true if a context switch was triggered.
        bool tick();

        // Current thread voluntarily yields. Triggers context switch.
        void yield();

        // Perform the actual context switch bookkeeping:
        // - Move current running thread back to ready queue (if still active)
        // - Dequeue the next thread from the front
        // - Mark it as Running, mark old one as Ready
        // Returns the new thread ID (or idle thread if queue empty)
        ThreadId switchContext();

        // Get the currently running thread ID
        ThreadId currentThreadId() const;

        // Set the currently running thread (used during startup)
        void setCurrentThread(ThreadId id);

        // Set the idle thread ID
        void setIdleThread(ThreadId id);

        // Get count of threads in ready queue
        std::uint8_t readyCount() const;

    private:
        ThreadId dequeue();
        void enqueue(ThreadId id);

        // Circular array-based ready queue
        ThreadId m_readyQueue[kMaxThreads];
        std::uint8_t m_readyCount;
        std::uint8_t m_readyHead;    // Next to dequeue
        ThreadId m_currentThreadId;
        ThreadId m_idleThreadId;
    };

}  // namespace kernel
