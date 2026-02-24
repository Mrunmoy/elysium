// Mock UART implementation for host-side testing.
// Replaces hal/src/stm32f4/Uart.cpp at link time.

#include "hal/Uart.h"

#include "MockRegisters.h"

#include <cstring>

namespace hal
{
    void uartInit(const UartConfig &config)
    {
        test::g_uartInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            config.baudRate,
        });
    }

    void uartPutChar(UartId id, char c)
    {
        test::g_uartPutCharCalls.push_back({
            static_cast<std::uint8_t>(id),
            c,
        });
    }

    void uartWrite(UartId id, const char *data, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            uartPutChar(id, data[i]);
        }
    }

    void uartWriteString(UartId id, const char *str)
    {
        uartWrite(id, str, std::strlen(str));
    }

    char uartGetChar(UartId id)
    {
        (void)id;
        if (test::g_uartRxReadPos < test::g_uartRxBuffer.size())
        {
            return test::g_uartRxBuffer[test::g_uartRxReadPos++];
        }
        return '\0';
    }

    bool uartTryGetChar(UartId id, char *c)
    {
        (void)id;
        if (test::g_uartRxReadPos < test::g_uartRxBuffer.size())
        {
            *c = test::g_uartRxBuffer[test::g_uartRxReadPos++];
            return true;
        }
        return false;
    }

    void uartRxInterruptEnable(UartId id, UartRxNotifyFn notifyFn, void *arg)
    {
        test::g_uartRxEnableCalls.push_back({
            static_cast<std::uint8_t>(id),
            reinterpret_cast<void *>(notifyFn),
            arg,
        });
        test::g_uartRxInterruptEnabled = true;
    }

    void uartRxInterruptDisable(UartId id)
    {
        (void)id;
        test::g_uartRxInterruptEnabled = false;
        ++test::g_uartRxInterruptDisableCount;
    }

    std::uint8_t uartRxBufferCount(UartId id)
    {
        (void)id;
        if (test::g_uartRxReadPos >= test::g_uartRxBuffer.size())
        {
            return 0;
        }
        return static_cast<std::uint8_t>(test::g_uartRxBuffer.size() - test::g_uartRxReadPos);
    }
}  // namespace hal
