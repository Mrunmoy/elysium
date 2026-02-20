#pragma once

#include <cstdint>

namespace kernel
{
    using ThreadId = std::uint8_t;
    using ThreadFunction = void (*)(void *);

    static constexpr ThreadId kInvalidThreadId = 0xFF;
    static constexpr ThreadId kMaxThreads = 8;
    static constexpr std::uint32_t kDefaultTimeSlice = 10;
    static constexpr std::uint32_t kDefaultStackSize = 512;
    static constexpr ThreadId kIdleThreadId = 0;

    // Priority: 0 = highest, 31 = lowest.  Matches Cortex-M hardware convention.
    static constexpr std::uint8_t kMaxPriorities = 32;
    static constexpr std::uint8_t kHighestPriority = 0;
    static constexpr std::uint8_t kLowestPriority = 31;
    static constexpr std::uint8_t kDefaultPriority = 16;
    static constexpr std::uint8_t kIdlePriority = kLowestPriority;

    enum class ThreadState : std::uint8_t
    {
        Inactive = 0,
        Ready,
        Running,
        Blocked  // Waiting on mutex, semaphore, or sleep
    };

    struct ThreadControlBlock
    {
        std::uint32_t *m_stackPointer;  // [offset 0] PSP -- assembly reads this
        ThreadState m_state;
        ThreadId m_id;
        std::uint8_t m_basePriority;       // Assigned priority (0=highest, 31=lowest)
        std::uint8_t m_currentPriority;    // Effective priority (may be boosted by inheritance)
        const char *m_name;
        std::uint32_t *m_stackBase;        // Bottom of stack (for overflow detection)
        std::uint32_t m_stackSize;         // In bytes
        std::uint32_t m_timeSliceRemaining;
        std::uint32_t m_timeSlice;

        // Linked list pointers
        ThreadId m_nextReady;              // Next in per-priority ready list
        ThreadId m_nextWait;               // Next in mutex/semaphore wait queue

        // Sleep / timeout
        std::uint32_t m_wakeupTick;        // Tick at which to wake (0 = not sleeping)
    };

    struct ThreadConfig
    {
        ThreadFunction function;
        void *arg;
        const char *name;
        std::uint32_t *stack;
        std::uint32_t stackSize;   // In bytes
        std::uint8_t priority;     // 0=highest, 31=lowest
        std::uint32_t timeSlice;   // In ticks (0 = default)
    };

    // Create a new thread. Returns thread ID or kInvalidThreadId on failure.
    ThreadId threadCreate(const ThreadConfig &config);

    // Get pointer to a TCB by ID. Returns nullptr if invalid.
    ThreadControlBlock *threadGetTcb(ThreadId id);

    // Get the TCB array (for scheduler access)
    ThreadControlBlock *threadGetTcbArray();

    // Reset thread subsystem (for testing)
    void threadReset();

}  // namespace kernel
