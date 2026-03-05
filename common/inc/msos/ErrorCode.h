#pragma once

#include <cstdint>

namespace msos
{
namespace error
{
    // Global status codes shared across kernel/HAL/app layers.
    // Values follow Linux errno-style negative integers.
    static constexpr std::int32_t kOk = 0;
    static constexpr std::int32_t kPerm = -1;        // EPERM
    static constexpr std::int32_t kNoEntry = -2;     // ENOENT
    static constexpr std::int32_t kNoThread = -3;    // ESRCH
    static constexpr std::int32_t kInterrupted = -4; // EINTR
    static constexpr std::int32_t kIo = -5;          // EIO
    static constexpr std::int32_t kNoMem = -12;      // ENOMEM
    static constexpr std::int32_t kBusy = -16;       // EBUSY
    static constexpr std::int32_t kInvalid = -22;    // EINVAL
    static constexpr std::int32_t kAgain = -11;      // EAGAIN
    static constexpr std::int32_t kNoData = -61;     // ENODATA
    static constexpr std::int32_t kNoSys = -38;      // ENOSYS
    static constexpr std::int32_t kTimedOut = -110;  // ETIMEDOUT

    // Domain-specific extension for I2C NACK.
    static constexpr std::int32_t kNoAck = -2001;

    constexpr bool isOk(std::int32_t code)
    {
        return code == kOk;
    }

    constexpr bool isError(std::int32_t code)
    {
        return code < 0;
    }
}  // namespace error
}  // namespace msos
