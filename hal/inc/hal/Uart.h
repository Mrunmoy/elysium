#pragma once

#include <cstddef>
#include <cstdint>

namespace hal
{
    enum class UartId : std::uint8_t
    {
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

}  // namespace hal
