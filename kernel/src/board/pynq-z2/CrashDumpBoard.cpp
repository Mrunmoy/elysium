// PYNQ-Z2 board-specific crash dump output.
//
// Provides polled UART (PS UART0 on MIO 14/15) for crash dump output.
// Uses direct register access -- no HAL, no RTOS, no interrupts.
// Safe to call from any fault context.
//
// The PYNQ-Z2 has no directly-accessible LEDs (they are on PL fabric),
// so boardFaultBlink() simply spins in a WFE loop.

#include "kernel/CrashDumpBoard.h"
#include "startup/SystemClock.h"

#include <cstdint>

namespace
{
    // Zynq PS UART0 registers (Cadence UART IP)
    constexpr std::uint32_t kUart0Base = 0xE0000000;
    constexpr std::uint32_t kUartCr   = kUart0Base + 0x00;  // Control register
    constexpr std::uint32_t kUartMr   = kUart0Base + 0x04;  // Mode register
    constexpr std::uint32_t kUartBrgr = kUart0Base + 0x18;  // Baud rate generator
    constexpr std::uint32_t kUartSr   = kUart0Base + 0x2C;  // Channel status
    constexpr std::uint32_t kUartFifo = kUart0Base + 0x30;  // TX/RX FIFO
    constexpr std::uint32_t kUartBdiv = kUart0Base + 0x34;  // Baud rate divider

    // Channel status bits
    constexpr std::uint32_t kSrTxFull  = 1u << 4;
    constexpr std::uint32_t kSrTxEmpty = 1u << 3;

    // Control register bits
    constexpr std::uint32_t kCrTxEn  = 1u << 4;  // TX enable
    constexpr std::uint32_t kCrRxEn  = 1u << 2;  // RX enable
    constexpr std::uint32_t kCrTxDis = 1u << 5;  // TX disable
    constexpr std::uint32_t kCrRxDis = 1u << 3;  // RX disable
    constexpr std::uint32_t kCrTxRst = 1u << 1;  // TX reset
    constexpr std::uint32_t kCrRxRst = 1u << 0;  // RX reset

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

}  // namespace

namespace kernel
{
    void boardEnsureOutput()
    {
        // Check if UART0 TX is already enabled
        std::uint32_t cr = reg(kUartCr);
        if (cr & kCrTxEn)
        {
            return;
        }

        // Disable TX/RX and reset FIFOs
        reg(kUartCr) = kCrTxDis | kCrRxDis | kCrTxRst | kCrRxRst;

        // Configure baud rate: 115200
        // baud = uart_ref_clk / (BRGR * (BDIV + 1))
        // With BDIV=15: BRGR = 100MHz / (115200 * 16) = ~54
        constexpr std::uint32_t kBdiv = 15;
        std::uint32_t cd = g_apb1Clock / (115200 * (kBdiv + 1));
        if (cd == 0)
        {
            cd = 1;
        }

        reg(kUartBrgr) = cd;
        reg(kUartBdiv) = kBdiv;

        // 8N1 mode (normal, 8-bit, no parity, 1 stop)
        reg(kUartMr) = 0x20;

        // Enable TX
        reg(kUartCr) = kCrTxEn | kCrRxDis;
    }

    void boardFaultPutChar(char c)
    {
        // Wait until TX FIFO is not full
        while (reg(kUartSr) & kSrTxFull)
        {
        }
        reg(kUartFifo) = static_cast<std::uint32_t>(c);
    }

    void boardFaultFlush()
    {
        // Wait until TX FIFO is empty
        while (!(reg(kUartSr) & kSrTxEmpty))
        {
        }
    }

    [[noreturn]] void boardFaultBlink()
    {
        // PYNQ-Z2 has no directly-accessible LEDs (they are on PL fabric).
        // Spin in a low-power WFE loop.
        while (true)
        {
            __asm volatile("wfe");
        }
    }

}  // namespace kernel
