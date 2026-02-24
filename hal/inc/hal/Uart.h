#pragma once

#include <cstddef>
#include <cstdint>

namespace hal
{
    enum class UartId : std::uint8_t
    {
        Uart0 = 0,      // Zynq PS UART0
        Usart1 = 1,
        Usart2 = 2,
        Usart3 = 3,
        Uart4 = 4,
        Uart5 = 5,
        Usart6 = 6
    };

    struct UartConfig
    {
        UartId id;
        std::uint32_t baudRate = 115200;
        std::uint8_t wordLength = 8;
        std::uint8_t stopBits = 1;
        bool parityEnable = false;
    };

    void uartInit(const UartConfig &config);
    void uartPutChar(UartId id, char c);
    void uartWrite(UartId id, const char *data, std::size_t length);
    void uartWriteString(UartId id, const char *str);

    // Receive: poll for a character (blocking)
    char uartGetChar(UartId id);

    // Receive: try to read a character (non-blocking).
    // Returns true if a character was available.
    bool uartTryGetChar(UartId id, char *c);

    // RX interrupt callback type.
    // Called from ISR context when one or more bytes are available.
    using UartRxNotifyFn = void (*)(void *arg);

    // Enable UART RX interrupt. Incoming bytes are buffered in a 64-byte ring buffer.
    // notifyFn is called from ISR context each time a byte arrives.
    void uartRxInterruptEnable(UartId id, UartRxNotifyFn notifyFn, void *arg);

    // Disable UART RX interrupt. Buffer contents are preserved.
    void uartRxInterruptDisable(UartId id);

    // Return number of bytes currently in the RX ring buffer.
    std::uint8_t uartRxBufferCount(UartId id);

}  // namespace hal
