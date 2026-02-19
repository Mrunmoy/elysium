// Mock GPIO implementation for host-side testing.
// Replaces hal/src/stm32f4/Gpio.cpp at link time.

#include "hal/Gpio.h"

#include "MockRegisters.h"

namespace hal
{
    void gpioInit(const GpioConfig &config)
    {
        test::g_gpioInitCalls.push_back({
            static_cast<std::uint8_t>(config.port),
            config.pin,
            static_cast<std::uint8_t>(config.mode),
            static_cast<std::uint8_t>(config.pull),
            static_cast<std::uint8_t>(config.speed),
            static_cast<std::uint8_t>(config.outputType),
            config.alternateFunction,
        });
    }

    void gpioSet(Port port, std::uint8_t pin)
    {
        test::g_gpioPinActions.push_back({
            test::GpioPinAction::Type::Set,
            static_cast<std::uint8_t>(port),
            pin,
        });
    }

    void gpioClear(Port port, std::uint8_t pin)
    {
        test::g_gpioPinActions.push_back({
            test::GpioPinAction::Type::Clear,
            static_cast<std::uint8_t>(port),
            pin,
        });
    }

    void gpioToggle(Port port, std::uint8_t pin)
    {
        test::g_gpioPinActions.push_back({
            test::GpioPinAction::Type::Toggle,
            static_cast<std::uint8_t>(port),
            pin,
        });
    }

    bool gpioRead(Port port, std::uint8_t pin)
    {
        (void)port;
        (void)pin;
        return test::g_gpioReadValue;
    }
}  // namespace hal
