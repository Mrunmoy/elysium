// Zynq-7000 watchdog stub.
//
// The Zynq PS has a System Watchdog Timer (SWDT) at 0xF8005000, but
// it requires SLCR configuration and is not used in the current PYNQ-Z2
// port.  This stub satisfies the linker for cross-compilation.

#include "hal/Watchdog.h"

namespace hal
{
    void watchdogInit(const WatchdogConfig & /* config */)
    {
        // No-op on Zynq
    }

    void watchdogFeed()
    {
        // No-op on Zynq
    }
}  // namespace hal
