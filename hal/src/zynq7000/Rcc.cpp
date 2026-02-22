// Zynq-7000 SLCR clock enables
//
// The Zynq PS uses SLCR (System Level Control Registers) instead of an RCC
// peripheral. Clock gating for AMBA peripherals is controlled through
// APER_CLK_CTRL at SLCR + 0x12C.

#include "hal/Rcc.h"

#include <cstdint>

namespace
{
    // SLCR base and registers
    constexpr std::uint32_t kSlcrBase = 0xF8000000;
    constexpr std::uint32_t kSlcrUnlock = 0x008;
    constexpr std::uint32_t kSlcrLock = 0x004;
    constexpr std::uint32_t kSlcrUnlockKey = 0x0000DF0D;
    constexpr std::uint32_t kSlcrLockKey = 0x0000767B;

    // AMBA Peripheral Clock Control
    constexpr std::uint32_t kAperClkCtrl = 0x12C;

    // APER_CLK_CTRL bit positions (per Zynq TRM, Table 25-10)
    constexpr std::uint32_t kUart0ClkBit = 20;   // Bit 20: UART0
    constexpr std::uint32_t kUart1ClkBit = 21;   // Bit 21: UART1
    constexpr std::uint32_t kGpioClkBit = 22;    // Bit 22: GPIO

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    void slcrUnlock()
    {
        reg(kSlcrBase + kSlcrUnlock) = kSlcrUnlockKey;
    }
}  // namespace

namespace hal
{
    void rccEnableGpioClock(Port /* port */)
    {
        slcrUnlock();
        reg(kSlcrBase + kAperClkCtrl) |= (1U << kGpioClkBit);
    }

    void rccEnableUartClock(UartId id)
    {
        slcrUnlock();
        switch (id)
        {
            case UartId::Uart0:
            case UartId::Usart1:    // Map STM32 "primary serial" to Zynq UART0
                reg(kSlcrBase + kAperClkCtrl) |= (1U << kUart0ClkBit);
                break;
            default:
                reg(kSlcrBase + kAperClkCtrl) |= (1U << kUart1ClkBit);
                break;
        }
    }
}  // namespace hal
