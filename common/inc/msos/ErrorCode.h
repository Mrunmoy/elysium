#pragma once

#include <cstdint>
#include <type_traits>

namespace msos
{
namespace error
{
    // Global status codes shared across kernel/HAL/app layers.
    // Values follow Linux errno-style negative integers.
    constexpr std::int32_t kOk = 0;
    constexpr std::int32_t kPerm = -1;        // EPERM
    constexpr std::int32_t kNoEntry = -2;     // ENOENT
    constexpr std::int32_t kNoThread = -3;    // ESRCH
    constexpr std::int32_t kInterrupted = -4; // EINTR
    constexpr std::int32_t kIo = -5;          // EIO
    constexpr std::int32_t kAgain = -11;      // EAGAIN
    constexpr std::int32_t kNoMem = -12;      // ENOMEM
    constexpr std::int32_t kBusy = -16;       // EBUSY
    constexpr std::int32_t kInvalid = -22;    // EINVAL
    constexpr std::int32_t kNoSys = -38;      // ENOSYS
    constexpr std::int32_t kNoData = -61;     // ENODATA
    constexpr std::int32_t kTimedOut = -110;  // ETIMEDOUT

    // Domain-specific extension for I2C NACK.
    constexpr std::int32_t kNoAck = -2001;

    constexpr bool isOk(std::int32_t code)
    {
        return code == kOk;
    }

    constexpr bool isError(std::int32_t code)
    {
        return code < 0;
    }

    // Canonical status format across layers:
    // - Success is exactly kOk (0)
    // - Failures are negative values
    constexpr bool isCanonicalStatus(std::int32_t code)
    {
        return code == kOk || code < 0;
    }

    // Adapter for legacy boolean APIs during migration to global status codes.
    constexpr std::int32_t boolToStatus(bool ok, std::int32_t errorCode)
    {
        return ok ? kOk : errorCode;
    }

    // Adapter for legacy handle/id APIs that use an invalid sentinel.
    template <typename T>
    constexpr std::int32_t handleToStatus(T handle, T invalidHandle, std::int32_t errorCode)
    {
        static_assert(std::is_integral<T>::value || std::is_enum<T>::value,
                      "handleToStatus expects integral or enum handle types");
        return handle == invalidHandle ? errorCode : kOk;
    }
}  // namespace error
}  // namespace msos
