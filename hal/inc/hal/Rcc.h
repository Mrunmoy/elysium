#pragma once

#include "hal/Dma.h"
#include "hal/Gpio.h"
#include "hal/I2c.h"
#include "hal/Spi.h"
#include "hal/Uart.h"

namespace hal
{
    void rccEnableGpioClock(Port port);
    void rccEnableUartClock(UartId id);
    void rccEnableSpiClock(SpiId id);
    void rccEnableI2cClock(I2cId id);
    void rccEnableDmaClock(DmaController controller);

    void rccDisableGpioClock(Port port);
    void rccDisableUartClock(UartId id);
    void rccDisableSpiClock(SpiId id);
    void rccDisableI2cClock(I2cId id);
    void rccDisableDmaClock(DmaController controller);

}  // namespace hal
