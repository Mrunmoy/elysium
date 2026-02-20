#pragma once

#include "hal/Gpio.h"
#include "hal/Uart.h"

namespace hal
{
    void rccEnableGpioClock(Port port);
    void rccEnableUartClock(UartId id);

}  // namespace hal
