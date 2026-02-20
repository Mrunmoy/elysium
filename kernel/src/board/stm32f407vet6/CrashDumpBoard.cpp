// STM32F407VET6 board-specific crash dump output.
//
// Provides polled UART (USART1 on PA9) and LED blink (PC13) for
// crash dump output. Uses direct register access -- no HAL, no RTOS,
// no interrupts. Safe to call from any fault context.
//
// The STM32F407 shares the same UART and GPIO peripheral IP as the
// STM32F207. If boards diverge in the future, edit independently.

#include "kernel/CrashDumpBoard.h"
#include "startup/SystemClock.h"

#include <cstdint>

namespace
{
    // USART1 registers
    constexpr std::uint32_t kUsart1Base = 0x40011000;
    constexpr std::uint32_t kUsart1Sr = kUsart1Base + 0x00;
    constexpr std::uint32_t kUsart1Dr = kUsart1Base + 0x04;
    constexpr std::uint32_t kUsart1Brr = kUsart1Base + 0x08;
    constexpr std::uint32_t kUsart1Cr1 = kUsart1Base + 0x0C;

    // USART1 CR1 bits
    constexpr std::uint32_t kUsartUe = 1U << 13;
    constexpr std::uint32_t kUsartTe = 1U << 3;

    // USART1 SR bits
    constexpr std::uint32_t kUsartTxe = 1U << 7;
    constexpr std::uint32_t kUsartTc = 1U << 6;

    // RCC registers
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kRccAhb1enr = kRccBase + 0x30;
    constexpr std::uint32_t kRccApb2enr = kRccBase + 0x44;

    // GPIOA registers for PA9 AF7 config
    constexpr std::uint32_t kGpioaBase = 0x40020000;
    constexpr std::uint32_t kGpioaModer = kGpioaBase + 0x00;
    constexpr std::uint32_t kGpioaOspeedr = kGpioaBase + 0x08;
    constexpr std::uint32_t kGpioaAfrh = kGpioaBase + 0x24;

    // GPIOC registers for LED on PC13
    constexpr std::uint32_t kGpiocBase = 0x40020800;
    constexpr std::uint32_t kGpiocModer = kGpiocBase + 0x00;
    constexpr std::uint32_t kGpiocOdr = kGpiocBase + 0x14;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Simple busy-wait delay (approximate).
    void faultDelayMs(std::uint32_t ms)
    {
        // Inner loop ~4 cycles per iteration.
        std::uint32_t itersPerMs = SystemCoreClock / 4000;
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            for (volatile std::uint32_t j = 0; j < itersPerMs; ++j)
            {
            }
        }
    }

}  // namespace

namespace kernel
{
    void boardEnsureOutput()
    {
        // Check if USART1 is already enabled
        if (reg(kUsart1Cr1) & kUsartUe)
        {
            return;
        }

        // Enable GPIOA clock (bit 0 of AHB1ENR)
        reg(kRccAhb1enr) |= (1U << 0);

        // Enable USART1 clock (bit 4 of APB2ENR)
        reg(kRccApb2enr) |= (1U << 4);

        // Configure PA9 as AF7 (USART1_TX)
        // MODER: bits 19:18 = 10 (alternate function)
        std::uint32_t moder = reg(kGpioaModer);
        moder &= ~(3U << 18);
        moder |= (2U << 18);
        reg(kGpioaModer) = moder;

        // OSPEEDR: bits 19:18 = 11 (very high speed)
        std::uint32_t ospeedr = reg(kGpioaOspeedr);
        ospeedr |= (3U << 18);
        reg(kGpioaOspeedr) = ospeedr;

        // AFRH: bits 7:4 = 0111 (AF7 for PA9, pin 9 is AFRH bit group 1)
        std::uint32_t afrh = reg(kGpioaAfrh);
        afrh &= ~(0xFU << 4);
        afrh |= (7U << 4);
        reg(kGpioaAfrh) = afrh;

        // Configure USART1: 115200 baud using runtime APB2 clock
        reg(kUsart1Cr1) = 0;
        reg(kUsart1Brr) = g_apb2Clock / 115200U;
        reg(kUsart1Cr1) = kUsartUe | kUsartTe;
    }

    void boardFaultPutChar(char c)
    {
        while (!(reg(kUsart1Sr) & kUsartTxe))
        {
        }
        reg(kUsart1Dr) = static_cast<std::uint32_t>(c);
    }

    void boardFaultFlush()
    {
        while (!(reg(kUsart1Sr) & kUsartTc))
        {
        }
    }

    [[noreturn]] void boardFaultBlink()
    {
        // Ensure GPIOC clock is enabled
        reg(kRccAhb1enr) |= (1U << 2);

        // Configure PC13 as output
        std::uint32_t moder = reg(kGpiocModer);
        moder &= ~(3U << 26);
        moder |= (1U << 26);
        reg(kGpiocModer) = moder;

        // Blink indefinitely (fast blink = fault indicator)
        while (true)
        {
            reg(kGpiocOdr) ^= (1U << 13);
            faultDelayMs(100);
        }
    }

}  // namespace kernel
