#pragma once

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    static constexpr std::uint8_t kMaxSemaphores = 8;
    using SemaphoreId = std::uint8_t;
    static constexpr SemaphoreId kInvalidSemaphoreId = 0xFF;

    struct SemaphoreControlBlock
    {
        bool active;              // Whether this slot is allocated
        std::uint32_t count;      // Current count
        std::uint32_t maxCount;   // Maximum count
        ThreadId waitHead;        // Head of priority-sorted wait queue
        std::uint8_t waitCount;   // Number of waiting threads
        const char *name;
    };

    // Create a counting semaphore.
    // Returns semaphore ID or kInvalidSemaphoreId on failure.
    SemaphoreId semaphoreCreate(std::uint32_t initialCount, std::uint32_t maxCount,
                                const char *name = nullptr);

    // Destroy a semaphore.
    void semaphoreDestroy(SemaphoreId id);

    // Wait (P/down/acquire). Blocks if count is 0.
    // Returns true on success.
    bool semaphoreWait(SemaphoreId id);

    // Try to acquire without blocking.
    // Returns true if acquired, false if count was 0.
    bool semaphoreTryWait(SemaphoreId id);

    // Signal (V/up/release). Wakes highest-priority waiter or increments count.
    // Returns true on success, false if count would exceed maxCount.
    bool semaphoreSignal(SemaphoreId id);

    // Get semaphore control block (for testing)
    SemaphoreControlBlock *semaphoreGetBlock(SemaphoreId id);

    // Reset semaphore subsystem (for testing)
    void semaphoreReset();

}  // namespace kernel
