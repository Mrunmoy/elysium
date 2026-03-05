#pragma once

#include "kernel/Thread.h"
#include "msos/ErrorCode.h"

#include <cstdint>

namespace kernel
{
    static constexpr std::uint8_t kMaxMutexes = 8;
    using MutexId = std::uint8_t;
    static constexpr MutexId kInvalidMutexId = 0xFF;
    using MutexStatus = std::int32_t;

    constexpr MutexStatus kMutexOk = msos::error::kOk;
    constexpr MutexStatus kMutexErrInvalid = msos::error::kInvalid;
    constexpr MutexStatus kMutexErrNoMem = msos::error::kNoMem;
    constexpr MutexStatus kMutexErrBusy = msos::error::kBusy;
    constexpr MutexStatus kMutexErrPerm = msos::error::kPerm;

    struct MutexControlBlock
    {
        bool active;             // Whether this slot is allocated
        ThreadId owner;          // Thread holding the mutex (kInvalidThreadId = unlocked)
        std::uint8_t lockCount;  // Recursive lock depth
        ThreadId waitHead;       // Head of priority-sorted wait queue
        std::uint8_t waitCount;  // Number of waiting threads
        const char *name;
    };

    // Create a mutex. Returns mutex ID or kInvalidMutexId on failure.
    MutexId mutexCreate(const char *name = nullptr);
    // Create a mutex and report canonical status.
    MutexStatus mutexCreateStatus(MutexId *outId, const char *name = nullptr);

    // Destroy a mutex.
    void mutexDestroy(MutexId id);
    // Destroy a mutex with status reporting.
    MutexStatus mutexDestroyStatus(MutexId id);

    // Lock a mutex. Blocks until acquired. Supports recursive locking.
    // Returns true on success. If called from ISR context, returns false.
    bool mutexLock(MutexId id);
    // Lock a mutex and return canonical status.
    MutexStatus mutexLockStatus(MutexId id);

    // Try to lock a mutex without blocking.
    // Returns true if locked, false if already held by another thread.
    bool mutexTryLock(MutexId id);
    // Try-lock with canonical status.
    MutexStatus mutexTryLockStatus(MutexId id);

    // Unlock a mutex. Must be called by the owning thread.
    // Returns true on success, false if not the owner.
    bool mutexUnlock(MutexId id);
    // Unlock with canonical status.
    MutexStatus mutexUnlockStatus(MutexId id);

    // Get mutex control block (for testing)
    MutexControlBlock *mutexGetBlock(MutexId id);

    // Reset mutex subsystem (for testing)
    void mutexReset();

}  // namespace kernel
