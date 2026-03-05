// Mock HAL watchdog for kernel integration tests.

#include "hal/Watchdog.h"
#include "MockKernel.h"

namespace hal
{
    void watchdogInit(const WatchdogConfig &config)
    {
        test::g_watchdogInitCalls.push_back({
            config.prescaler,
            config.reloadValue,
        });
    }

    void watchdogFeed()
    {
        ++test::g_watchdogFeedCount;
    }
}  // namespace hal
