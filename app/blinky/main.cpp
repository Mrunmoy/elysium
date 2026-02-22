// Blinky -- proof-of-life application for ms-os on STM32F207ZGT6
//
// Toggles the on-board LED and prints "ms-os alive" over UART.
// Board: LED on PC13, USART1 TX on PA9 (AF7).

#include "BoardConfig.h"
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
    // Enable peripheral clocks and configure pins from board config
    if constexpr (board::kHasLed)
    {
        hal::rccEnableGpioClock(board::kLedPort);

        hal::GpioConfig ledConfig{};
        ledConfig.port = board::kLedPort;
        ledConfig.pin = board::kLedPin;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        ledConfig.outputType = hal::OutputType::PushPull;
        hal::gpioInit(ledConfig);
    }

    if constexpr (board::kHasConsoleTx)
    {
        hal::rccEnableGpioClock(board::kConsoleTxPort);

        hal::GpioConfig txConfig{};
        txConfig.port = board::kConsoleTxPort;
        txConfig.pin = board::kConsoleTxPin;
        txConfig.mode = hal::PinMode::AlternateFunction;
        txConfig.speed = hal::OutputSpeed::VeryHigh;
        txConfig.alternateFunction = board::kConsoleTxAf;
        hal::gpioInit(txConfig);
    }

    hal::rccEnableUartClock(board::kConsoleUart);

    hal::UartConfig uartConfig{};
    uartConfig.id = board::kConsoleUart;
    uartConfig.baudRate = board::kConsoleBaud;
    hal::uartInit(uartConfig);

    hal::uartWriteString(board::kConsoleUart, "ms-os alive\r\n");

    while (true)
    {
        hal::gpioToggle(board::kLedPort, board::kLedPin);
        delayMs(500);
    }
}
