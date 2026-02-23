// Blinky -- proof-of-life application for ms-os on STM32F207ZGT6
//
// Toggles the on-board LED and prints "ms-os alive" over UART.
// Board: LED on PC13, USART1 TX on PA9 (AF7).

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"
#include "startup/SystemClock.h"

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

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
    board::configInit(g_boardDtb, g_boardDtbSize);

    const board::BoardConfig &cfg = board::config();

    // Enable peripheral clocks and configure pins from board config
    if (cfg.hasLed)
    {
        hal::rccEnableGpioClock(hal::Port(cfg.led.port - 'A'));

        hal::GpioConfig ledConfig{};
        ledConfig.port = hal::Port(cfg.led.port - 'A');
        ledConfig.pin = cfg.led.pin;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        ledConfig.outputType = hal::OutputType::PushPull;
        hal::gpioInit(ledConfig);
    }

    if (cfg.hasConsoleTx)
    {
        hal::rccEnableGpioClock(hal::Port(cfg.consoleTx.port - 'A'));

        hal::GpioConfig txConfig{};
        txConfig.port = hal::Port(cfg.consoleTx.port - 'A');
        txConfig.pin = cfg.consoleTx.pin;
        txConfig.mode = hal::PinMode::AlternateFunction;
        txConfig.speed = hal::OutputSpeed::VeryHigh;
        txConfig.alternateFunction = cfg.consoleTx.af;
        hal::gpioInit(txConfig);
    }

    hal::UartId uartId = board::consoleUartId();
    hal::rccEnableUartClock(uartId);

    hal::UartConfig uartConfig{};
    uartConfig.id = uartId;
    uartConfig.baudRate = cfg.consoleBaud;
    hal::uartInit(uartConfig);

    hal::uartWriteString(uartId, "ms-os alive\r\n");

    while (true)
    {
        if (cfg.hasLed)
        {
            hal::gpioToggle(hal::Port(cfg.led.port - 'A'), cfg.led.pin);
        }
        delayMs(500);
    }
}
