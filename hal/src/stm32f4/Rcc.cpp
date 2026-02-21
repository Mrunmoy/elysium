#include "hal/Rcc.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kRccAhb1enr = 0x30;
    constexpr std::uint32_t kRccApb1enr = 0x40;
    constexpr std::uint32_t kRccApb2enr = 0x44;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }
}  // namespace

namespace hal
{
    void rccEnableGpioClock(Port port)
    {
        // GPIO clocks are on AHB1, bits 0..8 for GPIOA..GPIOI
        reg(kRccBase + kRccAhb1enr) |= (1U << static_cast<std::uint8_t>(port));
    }

    void rccEnableUartClock(UartId id)
    {
        switch (id)
        {
            case UartId::Usart1:
                reg(kRccBase + kRccApb2enr) |= (1U << 4);
                break;
            case UartId::Usart2:
                reg(kRccBase + kRccApb1enr) |= (1U << 17);
                break;
            case UartId::Usart3:
                reg(kRccBase + kRccApb1enr) |= (1U << 18);
                break;
            case UartId::Uart4:
                reg(kRccBase + kRccApb1enr) |= (1U << 19);
                break;
            case UartId::Uart5:
                reg(kRccBase + kRccApb1enr) |= (1U << 20);
                break;
            case UartId::Usart6:
                reg(kRccBase + kRccApb2enr) |= (1U << 5);
                break;
            default:
                break;
        }
    }
}  // namespace hal
