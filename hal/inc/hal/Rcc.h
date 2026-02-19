#ifndef HAL_RCC_H
#define HAL_RCC_H

#include "hal/Gpio.h"
#include "hal/Uart.h"

namespace hal
{
    void rccEnableGpioClock(Port port);
    void rccEnableUartClock(UartId id);

}  // namespace hal

#endif  // HAL_RCC_H
