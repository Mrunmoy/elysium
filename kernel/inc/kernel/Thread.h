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

    enum class ThreadState : std::uint8_t
    {
        Inactive = 0,
        Ready,
        Running,
        Suspended
    };

    struct ThreadControlBlock
    {
        std::uint32_t *m_stackPointer;  // [offset 0] PSP -- assembly reads this
        ThreadState m_state;
        ThreadId m_id;
        std::uint8_t m_priority;  // Reserved for Phase 2
        const char *m_name;
        std::uint32_t *m_stackBase;  // Bottom of stack (for overflow detection)
        std::uint32_t m_stackSize;   // In bytes
        std::uint32_t m_timeSliceRemaining;
        std::uint32_t m_timeSlice;
    };

    struct ThreadConfig
    {
        ThreadFunction function;
        void *arg;
        const char *name;
        std::uint32_t *stack;
        std::uint32_t stackSize;  // In bytes
        std::uint8_t priority;    // Reserved for Phase 2
        std::uint32_t timeSlice;  // In ticks (0 = default)
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
