// STM32F4 True Random Number Generator (TRNG) driver.
//
// The RNG uses analog noise sources to produce 32-bit random numbers.
// It requires PLL48CLK (48 MHz) as its clock source, gated via RCC AHB2ENR.
//
// Per RM0090 section 24.3.4: the first random number generated after
// setting the RNGEN bit should not be used (seed initialization).

#include "hal/Rng.h"

#include "msos/ErrorCode.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kRngBase = 0x50060800;

    // Register offsets
    constexpr std::uint32_t kRngCr = 0x00;   // Control register
    constexpr std::uint32_t kRngSr = 0x04;   // Status register
    constexpr std::uint32_t kRngDr = 0x08;   // Data register

    // CR bits
    constexpr std::uint32_t kCrRngen = (1U << 2);   // RNG enable

    // SR bits
    constexpr std::uint32_t kSrDrdy = (1U << 0);    // Data ready
    constexpr std::uint32_t kSrCecs = (1U << 1);     // Clock error current status
    constexpr std::uint32_t kSrSecs = (1U << 2);     // Seed error current status

    // Timeout for DRDY polling (iteration count)
    constexpr std::uint32_t kDrdyTimeout = 1000000;

    volatile std::uint32_t &reg(std::uint32_t offset)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRngBase + offset);
    }

    // Read-and-discard helper for volatile registers (avoids -Werror on (void)reg()).
    void readDiscard(std::uint32_t offset)
    {
        volatile std::uint32_t discard = reg(offset);
        (void)discard;
    }

    // Wait for DRDY with timeout, check for errors.
    std::int32_t waitDrdy()
    {
        for (std::uint32_t i = 0; i < kDrdyTimeout; ++i)
        {
            std::uint32_t sr = reg(kRngSr);

            if ((sr & kSrCecs) != 0 || (sr & kSrSecs) != 0)
            {
                return msos::error::kIo;
            }

            if ((sr & kSrDrdy) != 0)
            {
                return msos::error::kOk;
            }
        }

        return msos::error::kTimedOut;
    }
}  // namespace

namespace hal
{
    std::int32_t rngInit()
    {
        // Enable the RNG peripheral
        reg(kRngCr) |= kCrRngen;

        // Discard first random number (seed initialization, per RM0090 24.3.4)
        std::int32_t status = waitDrdy();
        if (status != msos::error::kOk)
        {
            return status;
        }
        readDiscard(kRngDr);

        return msos::error::kOk;
    }

    std::int32_t rngRead(std::uint32_t &value)
    {
        std::int32_t status = waitDrdy();
        if (status != msos::error::kOk)
        {
            return status;
        }

        // Final error check before reading
        std::uint32_t sr = reg(kRngSr);
        if ((sr & (kSrCecs | kSrSecs)) != 0)
        {
            return msos::error::kIo;
        }

        value = reg(kRngDr);
        return msos::error::kOk;
    }

    void rngDeinit()
    {
        reg(kRngCr) &= ~kCrRngen;
    }
}  // namespace hal
