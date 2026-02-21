// Multi-threaded demo: LED blink + UART print running concurrently.
//
// Demonstrates ms-os kernel context switching on STM32F207ZGT6.
// LED thread toggles PC13 every ~500ms (50 ticks at 10ms time slice).
// UART thread prints "tick N" every ~1s (100 ticks at 10ms time slice).

#include "kernel/Kernel.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>

namespace
{
    // Thread stacks (statically allocated, aligned to stack size for MPU)
    alignas(1024) std::uint32_t g_ledStack[256];    // 1024 bytes
    alignas(1024) std::uint32_t g_uartStack[256];   // 1024 bytes

    // Simple integer-to-string for tick count (no sprintf in freestanding)
    void uintToStr(std::uint32_t val, char *buf, std::size_t bufSize)
    {
        if (bufSize == 0)
        {
            return;
        }

        // Handle zero
        if (val == 0)
        {
            if (bufSize >= 2)
            {
                buf[0] = '0';
                buf[1] = '\0';
            }
            return;
        }

        // Build digits in reverse
        char tmp[11];  // max 10 digits for uint32_t
        std::size_t len = 0;
        while (val > 0 && len < sizeof(tmp))
        {
            tmp[len++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }

        // Copy reversed
        std::size_t i = 0;
        while (len > 0 && i < bufSize - 1)
        {
            buf[i++] = tmp[--len];
        }
        buf[i] = '\0';
    }

    void ledThread(void *)
    {
        std::uint32_t lastToggle = 0;
        while (true)
        {
            std::uint32_t now = kernel::tickCount();
            if (now - lastToggle >= 500)
            {
                hal::gpioToggle(hal::Port::C, 13);
                lastToggle = now;
            }
            kernel::yield();
        }
    }

    void uartThread(void *)
    {
        std::uint32_t counter = 0;
        std::uint32_t lastPrint = 0;
        while (true)
        {
            std::uint32_t now = kernel::tickCount();
            if (now - lastPrint >= 1000)
            {
                char buf[16];
                hal::uartWriteString(hal::UartId::Usart1, "tick ");
                uintToStr(counter, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, "\r\n");
                ++counter;
                lastPrint = now;
            }
            kernel::yield();
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

    hal::uartWriteString(hal::UartId::Usart1, "ms-os kernel starting\r\n");

    // Initialize kernel
    kernel::init();

    // Create application threads with explicit priorities (lower number = higher priority)
    kernel::createThread(ledThread, nullptr, "led",
                         g_ledStack, sizeof(g_ledStack), 10);

    kernel::createThread(uartThread, nullptr, "uart",
                         g_uartStack, sizeof(g_uartStack), 10);

    // Start scheduler -- does not return
    kernel::startScheduler();

    // Should never reach here
    while (true)
    {
    }
}
