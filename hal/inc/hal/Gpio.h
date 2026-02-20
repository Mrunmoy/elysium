#pragma once

#include <cstdint>

namespace hal
{
    enum class Port : std::uint8_t
    {
        A = 0,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I
    };

    enum class PinMode : std::uint8_t
    {
        Input = 0,
        Output,
        AlternateFunction,
        Analog
    };

    enum class PullMode : std::uint8_t
    {
        None = 0,
        Up,
        Down
    };

    enum class OutputSpeed : std::uint8_t
    {
        Low = 0,
        Medium,
        High,
        VeryHigh
    };

    enum class OutputType : std::uint8_t
    {
        PushPull = 0,
        OpenDrain
    };

    struct GpioConfig
    {
        Port port;
        std::uint8_t pin;
        PinMode mode;
        PullMode pull = PullMode::None;
        OutputSpeed speed = OutputSpeed::Low;
        OutputType outputType = OutputType::PushPull;
        std::uint8_t alternateFunction = 0;
    };

    void gpioInit(const GpioConfig &config);
    void gpioSet(Port port, std::uint8_t pin);
    void gpioClear(Port port, std::uint8_t pin);
    void gpioToggle(Port port, std::uint8_t pin);
    bool gpioRead(Port port, std::uint8_t pin);

}  // namespace hal
