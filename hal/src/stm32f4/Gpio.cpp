#include "hal/Gpio.h"

#include <cstdint>

namespace
{
    // GPIO peripheral base addresses (each port is 0x400 apart)
    constexpr std::uint32_t kGpioBaseA = 0x40020000;
    constexpr std::uint32_t kGpioPortStride = 0x0400;

    // GPIO register offsets
    constexpr std::uint32_t kModer = 0x00;
    constexpr std::uint32_t kOtyper = 0x04;
    constexpr std::uint32_t kOspeedr = 0x08;
    constexpr std::uint32_t kPupdr = 0x0C;
    constexpr std::uint32_t kIdr = 0x10;
    constexpr std::uint32_t kOdr = 0x14;
    constexpr std::uint32_t kBsrr = 0x18;
    constexpr std::uint32_t kAfrl = 0x20;
    constexpr std::uint32_t kAfrh = 0x24;

    std::uint32_t portBase(hal::Port port)
    {
        return kGpioBaseA + static_cast<std::uint8_t>(port) * kGpioPortStride;
    }

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Interrupt guard: save PRIMASK and disable IRQs
    std::uint32_t disableIrq()
    {
        std::uint32_t primask;
        __asm volatile("mrs %0, primask" : "=r"(primask));
        __asm volatile("cpsid i" ::: "memory");
        return primask;
    }

    void restoreIrq(std::uint32_t primask)
    {
        __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    }
}  // namespace

namespace hal
{
    void gpioInit(const GpioConfig &config)
    {
        std::uint32_t base = portBase(config.port);
        std::uint32_t pin = config.pin;

        std::uint32_t saved = disableIrq();

        // Mode (2 bits per pin)
        std::uint32_t tmp = reg(base + kModer);
        tmp &= ~(0x3U << (pin * 2));
        tmp |= (static_cast<std::uint32_t>(config.mode) << (pin * 2));
        reg(base + kModer) = tmp;

        // Output type (1 bit per pin)
        tmp = reg(base + kOtyper);
        tmp &= ~(1U << pin);
        tmp |= (static_cast<std::uint32_t>(config.outputType) << pin);
        reg(base + kOtyper) = tmp;

        // Speed (2 bits per pin)
        tmp = reg(base + kOspeedr);
        tmp &= ~(0x3U << (pin * 2));
        tmp |= (static_cast<std::uint32_t>(config.speed) << (pin * 2));
        reg(base + kOspeedr) = tmp;

        // Pull-up/pull-down (2 bits per pin)
        tmp = reg(base + kPupdr);
        tmp &= ~(0x3U << (pin * 2));
        tmp |= (static_cast<std::uint32_t>(config.pull) << (pin * 2));
        reg(base + kPupdr) = tmp;

        // Alternate function (4 bits per pin, split across AFRL and AFRH)
        if (config.mode == PinMode::AlternateFunction)
        {
            std::uint32_t afrOffset = (pin < 8) ? kAfrl : kAfrh;
            std::uint32_t shift = (pin % 8) * 4;
            tmp = reg(base + afrOffset);
            tmp &= ~(0xFU << shift);
            tmp |= (static_cast<std::uint32_t>(config.alternateFunction) << shift);
            reg(base + afrOffset) = tmp;
        }

        restoreIrq(saved);
    }

    void gpioSet(Port port, std::uint8_t pin)
    {
        reg(portBase(port) + kBsrr) = (1U << pin);
    }

    void gpioClear(Port port, std::uint8_t pin)
    {
        reg(portBase(port) + kBsrr) = (1U << (pin + 16));
    }

    void gpioToggle(Port port, std::uint8_t pin)
    {
        std::uint32_t base = portBase(port);
        std::uint32_t saved = disableIrq();
        reg(base + kOdr) ^= (1U << pin);
        restoreIrq(saved);
    }

    bool gpioRead(Port port, std::uint8_t pin)
    {
        return (reg(portBase(port) + kIdr) & (1U << pin)) != 0;
    }
}  // namespace hal
