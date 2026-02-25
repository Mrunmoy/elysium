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

    // Interrupt guard: save CPSR and disable IRQs
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
    void rccEnableGpioClock(Port /* port */)
    {
        std::uint32_t saved = disableIrq();
        slcrUnlock();
        reg(kSlcrBase + kAperClkCtrl) |= (1U << kGpioClkBit);
        restoreIrq(saved);
    }

    void rccEnableUartClock(UartId id)
    {
        std::uint32_t saved = disableIrq();
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
        restoreIrq(saved);
    }

    void rccDisableGpioClock(Port /* port */)
    {
        std::uint32_t saved = disableIrq();
        slcrUnlock();
        reg(kSlcrBase + kAperClkCtrl) &= ~(1U << kGpioClkBit);
        restoreIrq(saved);
    }

    void rccDisableUartClock(UartId id)
    {
        std::uint32_t saved = disableIrq();
        slcrUnlock();
        switch (id)
        {
            case UartId::Uart0:
            case UartId::Usart1:
                reg(kSlcrBase + kAperClkCtrl) &= ~(1U << kUart0ClkBit);
                break;
            default:
                reg(kSlcrBase + kAperClkCtrl) &= ~(1U << kUart1ClkBit);
                break;
        }
        restoreIrq(saved);
    }

    // Zynq PS has SPI0/SPI1 on APER_CLK_CTRL but not used in current port
    void rccEnableSpiClock(SpiId /* id */) {}
    void rccDisableSpiClock(SpiId /* id */) {}

    // Zynq PS has I2C0/I2C1 on APER_CLK_CTRL but not used in current port
    void rccEnableI2cClock(I2cId /* id */) {}
    void rccDisableI2cClock(I2cId /* id */) {}

    // Zynq PS DMA (DMAC) clock is always on
    void rccEnableDmaClock(DmaController /* controller */) {}
    void rccDisableDmaClock(DmaController /* controller */) {}
}  // namespace hal
