#pragma once

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    static constexpr std::uint8_t kMaxMutexes = 8;
    using MutexId = std::uint8_t;
    static constexpr MutexId kInvalidMutexId = 0xFF;

    struct MutexControlBlock
    {
        bool m_active;             // Whether this slot is allocated
        ThreadId m_owner;          // Thread holding the mutex (kInvalidThreadId = unlocked)
        std::uint8_t m_lockCount;  // Recursive lock depth
        ThreadId m_waitHead;       // Head of priority-sorted wait queue
        std::uint8_t m_waitCount;  // Number of waiting threads
        const char *m_name;
    };

    // Create a mutex. Returns mutex ID or kInvalidMutexId on failure.
    MutexId mutexCreate(const char *name = nullptr);

    // Destroy a mutex.
    void mutexDestroy(MutexId id);

    // Lock a mutex. Blocks until acquired. Supports recursive locking.
    // Returns true on success. If called from ISR context, returns false.
    bool mutexLock(MutexId id);

    // Try to lock a mutex without blocking.
    // Returns true if locked, false if already held by another thread.
    bool mutexTryLock(MutexId id);

    // Unlock a mutex. Must be called by the owning thread.
    // Returns true on success, false if not the owner.
    bool mutexUnlock(MutexId id);

    // Get mutex control block (for testing)
    MutexControlBlock *mutexGetBlock(MutexId id);

    // Reset mutex subsystem (for testing)
    void mutexReset();

}  // namespace kernel
