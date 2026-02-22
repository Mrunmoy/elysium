// Zynq-7000 PS UART driver (Cadence UART IP)
//
// Implements the hal::uart* interface for Zynq PS UART0/UART1.
// UART0 base: 0xE0000000, UART1 base: 0xE0001000.
//
// Baud rate: baud = uart_ref_clk / (CD * (BDIV + 1))
// where CD is BRGR register value and BDIV is baud rate divider.

#include "hal/Uart.h"
#include "startup/SystemClock.h"

#include <cstdint>
#include <cstring>

namespace
{
    // PS UART base addresses
    constexpr std::uint32_t kUart0Base = 0xE0000000;
    constexpr std::uint32_t kUart1Base = 0xE0001000;

    // Register offsets
    constexpr std::uint32_t kCr   = 0x00;    // Control register
    constexpr std::uint32_t kMr   = 0x04;    // Mode register
    constexpr std::uint32_t kBrgr = 0x18;    // Baud rate generator (CD)
    constexpr std::uint32_t kSr   = 0x2C;    // Channel status
    constexpr std::uint32_t kFifo = 0x30;    // TX/RX data FIFO
    constexpr std::uint32_t kBdiv = 0x34;    // Baud rate divider

    // CR bits (per Zynq TRM, UART Control Register, Table 19-1)
    constexpr std::uint32_t kCrRxRes = 1U << 0;     // RX logic reset
    constexpr std::uint32_t kCrTxRes = 1U << 1;     // TX logic reset
    constexpr std::uint32_t kCrRxEn  = 1U << 2;     // RX enable
    constexpr std::uint32_t kCrRxDis = 1U << 3;     // RX disable
    constexpr std::uint32_t kCrTxEn  = 1U << 4;     // TX enable
    constexpr std::uint32_t kCrTxDis = 1U << 5;     // TX disable

    // MR bits for 8N1
    // Bits 1:0 = CLKS (0 = uart_ref_clk)
    // Bits 2:1 = CHRL (0x = 8 bit)
    // Bits 5:3 = PAR  (100 = no parity)
    // Bits 7:6 = NBSTOP (00 = 1 stop bit)
    // Bits 9:8 = CHMODE (00 = normal)
    constexpr std::uint32_t kMr8n1 = (0x4U << 3);   // PAR=100 (no parity), rest 0

    // SR bits
    constexpr std::uint32_t kSrRxEmpty = 1U << 1;    // RX FIFO empty
    constexpr std::uint32_t kSrTxFull  = 1U << 4;    // TX FIFO full
    constexpr std::uint32_t kSrTxEmpty = 1U << 3;     // TX FIFO empty

    // Default baud rate divider (BDIV + 1 divides the CD output)
    constexpr std::uint32_t kDefaultBdiv = 4;        // BDIV register value

    std::uint32_t uartBase(hal::UartId id)
    {
        switch (id)
        {
            case hal::UartId::Uart0:
            case hal::UartId::Usart1:   // Map STM32 "primary serial" to Zynq UART0
                return kUart0Base;
            default:
                break;
        }
        return kUart1Base;
    }

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Interrupt guard for thread-safe UART output.
    // Saves CPSR and disables IRQs; restore re-enables only if they were enabled before.
    std::uint32_t disableIrq()
    {
        std::uint32_t cpsr;
        __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
        __asm volatile("cpsid i" ::: "memory");
        return cpsr;
    }

    void restoreIrq(std::uint32_t cpsr)
    {
        __asm volatile("msr cpsr_c, %0" ::"r"(cpsr) : "memory");
    }
}  // namespace

namespace hal
{
    void uartInit(const UartConfig &config)
    {
        std::uint32_t base = uartBase(config.id);

        // Disable TX and RX
        reg(base + kCr) = kCrTxDis | kCrRxDis;

        // Reset TX and RX logic
        reg(base + kCr) = kCrTxRes | kCrRxRes;

        // Configure baud rate
        // baud = uart_ref_clk / (CD * (BDIV + 1))
        // CD = uart_ref_clk / (baud * (BDIV + 1))
        std::uint32_t refClk = g_apb1Clock;    // UART reference clock
        std::uint32_t bdiv = kDefaultBdiv;
        std::uint32_t cd = refClk / (config.baudRate * (bdiv + 1));
        if (cd < 1)
        {
            cd = 1;
        }

        reg(base + kBdiv) = bdiv;
        reg(base + kBrgr) = cd;

        // Configure mode: 8N1, normal mode, uart_ref_clk
        reg(base + kMr) = kMr8n1;

        // Enable TX and RX
        reg(base + kCr) = kCrTxEn | kCrRxEn;
    }

    void uartPutChar(UartId id, char c)
    {
        std::uint32_t base = uartBase(id);

        // Wait until TX FIFO is not full
        while ((reg(base + kSr) & kSrTxFull) != 0)
        {
        }
        reg(base + kFifo) = static_cast<std::uint32_t>(c);
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
        while ((reg(base + kSr) & kSrRxEmpty) != 0)
        {
        }
        return static_cast<char>(reg(base + kFifo) & 0xFFU);
    }

    bool uartTryGetChar(UartId id, char *c)
    {
        std::uint32_t base = uartBase(id);
        if ((reg(base + kSr) & kSrRxEmpty) != 0)
        {
            return false;
        }
        *c = static_cast<char>(reg(base + kFifo) & 0xFFU);
        return true;
    }
}  // namespace hal
