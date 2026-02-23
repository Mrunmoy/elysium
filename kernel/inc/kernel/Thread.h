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
        std::uint32_t *stackPointer;    // [offset 0] PSP -- assembly reads this
        ThreadState state;
        ThreadId id;
        std::uint8_t basePriority;      // Assigned priority (0=highest, 31=lowest)
        std::uint8_t currentPriority;   // Effective priority (may be boosted by inheritance)
        const char *name;
        std::uint32_t *stackBase;       // Bottom of stack (for overflow detection)
        std::uint32_t stackSize;        // In bytes
        std::uint32_t timeSliceRemaining;
        std::uint32_t timeSlice;

        // Linked list pointers
        ThreadId nextReady;             // Next in per-priority ready list
        ThreadId nextWait;              // Next in mutex/semaphore wait queue

        // Sleep / timeout
        std::uint32_t wakeupTick;       // Tick at which to wake (0 = not sleeping)

        // MPU stack region (pre-computed for fast context switch)
        std::uint32_t mpuStackRbar;     // RBAR value for this thread's stack region
        std::uint32_t mpuStackRasr;     // RASR value for this thread's stack region

        // Privilege level: true = privileged (Handler + Thread), false = unprivileged
        // Assembly reads this at offset 44 for CONTROL register setup.
        bool privileged;
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
        bool privileged;           // true = runs in privileged mode (default)
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
