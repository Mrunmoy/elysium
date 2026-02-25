// Mock RCC implementation for host-side testing.
// Replaces hal/src/stm32f4/Rcc.cpp at link time.

#include "hal/Rcc.h"

#include "MockRegisters.h"

namespace hal
{
    void rccEnableGpioClock(Port port)
    {
        test::g_rccEnableCalls.push_back({"gpio", static_cast<std::uint8_t>(port)});
    }

    void rccDisableGpioClock(Port port)
    {
        test::g_rccDisableCalls.push_back({"gpio", static_cast<std::uint8_t>(port)});
    }

    void rccEnableUartClock(UartId id)
    {
        test::g_rccEnableCalls.push_back({"uart", static_cast<std::uint8_t>(id)});
    }

    void rccDisableUartClock(UartId id)
    {
        test::g_rccDisableCalls.push_back({"uart", static_cast<std::uint8_t>(id)});
    }

    void rccEnableSpiClock(SpiId id)
    {
        test::g_rccEnableCalls.push_back({"spi", static_cast<std::uint8_t>(id)});
    }

    void rccDisableSpiClock(SpiId id)
    {
        test::g_rccDisableCalls.push_back({"spi", static_cast<std::uint8_t>(id)});
    }

    void rccEnableI2cClock(I2cId id)
    {
        test::g_rccEnableCalls.push_back({"i2c", static_cast<std::uint8_t>(id)});
    }

    void rccDisableI2cClock(I2cId id)
    {
        test::g_rccDisableCalls.push_back({"i2c", static_cast<std::uint8_t>(id)});
    }

    void rccEnableDmaClock(DmaController controller)
    {
        test::g_rccEnableCalls.push_back({"dma", static_cast<std::uint8_t>(controller)});
    }

    void rccDisableDmaClock(DmaController controller)
    {
        test::g_rccDisableCalls.push_back({"dma", static_cast<std::uint8_t>(controller)});
    }
}  // namespace hal
