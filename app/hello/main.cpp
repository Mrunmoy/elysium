// Hello -- bare-metal proof-of-life for ms-os on PYNQ-Z2 (Zynq-7020)
//
// Prints "ms-os on PYNQ-Z2" then loops printing "hello N" over PS UART0.
// No RTOS kernel, no interrupts -- just startup + HAL.

#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>

namespace
{
    void uintToStr(std::uint32_t val, char *buf)
    {
        if (val == 0)
        {
            buf[0] = '0';
            buf[1] = '\0';
            return;
        }

        int len = 0;
        char tmp[12];
        while (val > 0)
        {
            tmp[len++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }

        for (int i = 0; i < len; ++i)
        {
            buf[i] = tmp[len - 1 - i];
        }
        buf[len] = '\0';
    }
}  // namespace

int main()
{
    // Enable UART0 peripheral clock
    hal::rccEnableUartClock(hal::UartId::Uart0);

    // Initialize UART0: 115200 8N1
    hal::UartConfig cfg;
    cfg.id = hal::UartId::Uart0;
    cfg.baudRate = 115200;
    hal::uartInit(cfg);

    hal::uartWriteString(hal::UartId::Uart0, "ms-os on PYNQ-Z2\r\n");

    std::uint32_t n = 0;
    while (true)
    {
        // Busy-wait delay (~1 second at 650 MHz, no timer yet)
        for (volatile std::uint32_t i = 0; i < 50000000; ++i)
        {
        }

        char buf[12];
        uintToStr(n, buf);
        hal::uartWriteString(hal::UartId::Uart0, "hello ");
        hal::uartWriteString(hal::UartId::Uart0, buf);
        hal::uartWriteString(hal::UartId::Uart0, "\r\n");
        ++n;
    }
}
