#pragma once

#include "msos/ErrorCode.h"

#include <cstdint>

namespace hal
{
    enum class AdcId : std::uint8_t
    {
        Adc1 = 0,
        Adc2,
        Adc3
    };

    enum class AdcResolution : std::uint8_t
    {
        Bits12 = 0,
        Bits10 = 1,
        Bits8 = 2,
        Bits6 = 3
    };

    enum class AdcAlign : std::uint8_t
    {
        Right = 0,
        Left = 1
    };

    enum class AdcSampleTime : std::uint8_t
    {
        Cycles3 = 0,
        Cycles15 = 1,
        Cycles28 = 2,
        Cycles56 = 3,
        Cycles84 = 4,
        Cycles112 = 5,
        Cycles144 = 6,
        Cycles480 = 7
    };

    struct AdcConfig
    {
        AdcId id;
        AdcResolution resolution = AdcResolution::Bits12;
        AdcAlign align = AdcAlign::Right;
        AdcSampleTime sampleTime = AdcSampleTime::Cycles84;
    };

    // Initialize ADC controller for single conversion mode.
    void adcInit(const AdcConfig &config);

    // Single-shot polled conversion.
    // Returns global status code (kOk / kInvalid / kTimedOut).
    std::int32_t adcRead(AdcId id, std::uint8_t channel, std::uint16_t *outValue,
                         std::uint32_t timeoutLoops);

    constexpr std::int32_t adcRequestToStatus(bool accepted)
    {
        return accepted ? msos::error::kOk : msos::error::kInvalid;
    }

}  // namespace hal
