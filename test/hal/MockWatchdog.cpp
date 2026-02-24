// Mock Watchdog implementation for host-side testing.
// Replaces hal/src/stm32f4/Watchdog.cpp at link time.

#include "hal/Watchdog.h"

#include "MockRegisters.h"

namespace hal
{
    void watchdogInit(const WatchdogConfig &config)
    {
        test::g_watchdogInitCalls.push_back({
            static_cast<std::uint8_t>(config.prescaler),
            config.reloadValue,
        });
    }

    void watchdogFeed()
    {
        ++test::g_watchdogFeedCount;
    }
}  // namespace hal
