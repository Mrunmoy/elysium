#include "hal/Uart.h"
#include "startup/SystemClock.h"

#include <cstdint>
#include <cstring>

namespace
{
    // USART base addresses
    constexpr std::uint32_t kUsart1Base = 0x40011000;
    constexpr std::uint32_t kUsart2Base = 0x40004400;
    constexpr std::uint32_t kUsart3Base = 0x40004800;
    constexpr std::uint32_t kUart4Base = 0x40004C00;
    constexpr std::uint32_t kUart5Base = 0x40005000;
    constexpr std::uint32_t kUsart6Base = 0x40011400;

    // USART register offsets
    constexpr std::uint32_t kSr = 0x00;
    constexpr std::uint32_t kDr = 0x04;
    constexpr std::uint32_t kBrr = 0x08;
    constexpr std::uint32_t kCr1 = 0x0C;
    constexpr std::uint32_t kCr2 = 0x10;
    constexpr std::uint32_t kCr3 = 0x14;

    // CR1 bits
    constexpr std::uint32_t kCr1Ue = 1U << 13;   // USART enable
    constexpr std::uint32_t kCr1Te = 1U << 3;     // Transmitter enable
    constexpr std::uint32_t kCr1Re = 1U << 2;     // Receiver enable

    // SR bits
    constexpr std::uint32_t kSrRxne = 1U << 5;    // Read data register not empty
    constexpr std::uint32_t kSrTxe = 1U << 7;     // Transmit data register empty
    constexpr std::uint32_t kSrTc = 1U << 6;      // Transmission complete

    std::uint32_t uartBase(hal::UartId id)
    {
        switch (id)
        {
            case hal::UartId::Usart1:
                return kUsart1Base;
            case hal::UartId::Usart2:
                return kUsart2Base;
            case hal::UartId::Usart3:
                return kUsart3Base;
            case hal::UartId::Uart4:
                return kUart4Base;
            case hal::UartId::Uart5:
                return kUart5Base;
            case hal::UartId::Usart6:
                return kUsart6Base;
            default:
                break;
        }
        return kUsart1Base;
    }

    std::uint32_t uartPeripheralClock(hal::UartId id)
    {
        // USART1 and USART6 are on APB2; others on APB1
        switch (id)
        {
            case hal::UartId::Usart1:
            case hal::UartId::Usart6:
                return g_apb2Clock;
            default:
                return g_apb1Clock;
        }
    }

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Interrupt guard for thread-safe UART output.
    // Saves PRIMASK and disables IRQs; restore re-enables only if they were enabled before.
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
    void uartInit(const UartConfig &config)
    {
        std::uint32_t base = uartBase(config.id);
        std::uint32_t pclk = uartPeripheralClock(config.id);

        // Disable USART before configuration
        reg(base + kCr1) &= ~kCr1Ue;

        // Clear CR1 config bits, then set 8N1 TX+RX
        std::uint32_t cr1 = 0;
        cr1 |= kCr1Te | kCr1Re;
        reg(base + kCr1) = cr1;

        // 1 stop bit (CR2 bits 13:12 = 00)
        reg(base + kCr2) = 0;

        // No flow control (CR3 = 0)
        reg(base + kCr3) = 0;

        // Baud rate: BRR = fck / baud
        // Use fixed-point: mantissa and fraction
        std::uint32_t integerdivider = (25 * pclk) / (4 * config.baudRate);
        std::uint32_t mantissa = integerdivider / 100;
        std::uint32_t fraction = ((integerdivider - (mantissa * 100)) * 16 + 50) / 100;
        reg(base + kBrr) = (mantissa << 4) | (fraction & 0xFU);

        // Enable USART
        reg(base + kCr1) |= kCr1Ue;
    }

    void uartPutChar(UartId id, char c)
    {
        std::uint32_t base = uartBase(id);
        while ((reg(base + kSr) & kSrTxe) == 0)
        {
        }
        reg(base + kDr) = static_cast<std::uint32_t>(c);
    }

    void uartWrite(UartId id, const char *data, std::size_t length)
    {
        std::uint32_t saved = disableIrq();
        for (std::size_t i = 0; i < length; ++i)
        {
            uartPutChar(id, data[i]);
        }
        restoreIrq(saved);
    }

    void uartWriteString(UartId id, const char *str)
    {
        uartWrite(id, str, std::strlen(str));
    }

    char uartGetChar(UartId id)
    {
        std::uint32_t base = uartBase(id);
        while ((reg(base + kSr) & kSrRxne) == 0)
        {
        }
        return static_cast<char>(reg(base + kDr) & 0xFFU);
    }

    bool uartTryGetChar(UartId id, char *c)
    {
        std::uint32_t base = uartBase(id);
        if ((reg(base + kSr) & kSrRxne) == 0)
        {
            return false;
        }
        *c = static_cast<char>(reg(base + kDr) & 0xFFU);
        return true;
    }
}  // namespace hal
