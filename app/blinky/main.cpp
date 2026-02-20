// Blinky -- proof-of-life application for ms-os on STM32F207ZGT6
//
// Toggles the on-board LED and prints "ms-os alive" over UART.
// Board: LED on PC13, USART1 TX on PA9 (AF7).

#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"
#include "startup/SystemClock.h"

namespace
{
    // SysTick-based busy-wait delay (approximate)
    constexpr std::uint32_t kSysTickCtrl = 0xE000E010;
    constexpr std::uint32_t kSysTickLoad = 0xE000E014;
    constexpr std::uint32_t kSysTickVal = 0xE000E018;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    void delayMs(std::uint32_t ms)
    {
        // Configure SysTick for 1 ms ticks (processor clock / 1000)
        reg(kSysTickLoad) = (SystemCoreClock / 1000) - 1;
        reg(kSysTickVal) = 0;
        reg(kSysTickCtrl) = 0x5;  // Enable, processor clock, no interrupt

        for (std::uint32_t i = 0; i < ms; ++i)
        {
            while ((reg(kSysTickCtrl) & (1U << 16)) == 0)
            {
            }
        }
    }
}  // namespace

int main()
{
    // Enable peripheral clocks
    hal::rccEnableGpioClock(hal::Port::C);
    hal::rccEnableGpioClock(hal::Port::A);
    hal::rccEnableUartClock(hal::UartId::Usart1);

    // Configure LED pin: PC13, push-pull output
    hal::GpioConfig ledConfig{};
    ledConfig.port = hal::Port::C;
    ledConfig.pin = 13;
    ledConfig.mode = hal::PinMode::Output;
    ledConfig.speed = hal::OutputSpeed::Low;
    ledConfig.outputType = hal::OutputType::PushPull;
    hal::gpioInit(ledConfig);

    // Configure USART1 TX pin: PA9, alternate function 7
    hal::GpioConfig txConfig{};
    txConfig.port = hal::Port::A;
    txConfig.pin = 9;
    txConfig.mode = hal::PinMode::AlternateFunction;
    txConfig.speed = hal::OutputSpeed::VeryHigh;
    txConfig.alternateFunction = 7;
    hal::gpioInit(txConfig);

    // Initialize UART: 115200 8N1
    hal::UartConfig uartConfig{};
    uartConfig.id = hal::UartId::Usart1;
    uartConfig.baudRate = 115200;
    hal::uartInit(uartConfig);

    hal::uartWriteString(hal::UartId::Usart1, "ms-os alive\r\n");

    while (true)
    {
        hal::gpioToggle(hal::Port::C, 13);
        delayMs(500);
    }
}
