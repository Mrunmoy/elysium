#pragma once

#include <cstdint>

namespace hal
{
    enum class WatchdogPrescaler : std::uint8_t
    {
        Div4 = 0,
        Div8 = 1,
        Div16 = 2,
        Div32 = 3,
        Div64 = 4,
        Div128 = 5,
        Div256 = 6
    };

    struct WatchdogConfig
    {
        WatchdogPrescaler prescaler;
        std::uint16_t reloadValue;      // 0-4095 (12-bit)
    };

    void watchdogInit(const WatchdogConfig &config);
    void watchdogFeed();

}  // namespace hal
