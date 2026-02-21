// SystemInit -- Zynq-7020 PS initialization for PYNQ-Z2
//
// For JTAG development, u-boot has already configured all PLLs, DDR, and
// peripheral clocks. SystemInit only sets the clock globals so that the
// UART baud rate calculation works correctly.
//
// Called from Startup.s before main().

#include <cstdint>

namespace
{
    // SLCR (System Level Control Registers)
    constexpr std::uint32_t kSlcrBase = 0xF8000000;
    constexpr std::uint32_t kSlcrUnlock = 0x008;
    constexpr std::uint32_t kSlcrUnlockKey = 0x0000DF0D;

    volatile std::uint32_t &reg(std::uint32_t base, std::uint32_t offset)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(base + offset);
    }
}  // namespace

// Global clock variables (declared in startup/inc/startup/SystemClock.h)
// PYNQ-Z2: CPU 650 MHz (CPU_3x2x), UART ref clock 100 MHz (IO PLL)
std::uint32_t SystemCoreClock = 650000000;
std::uint32_t g_apb1Clock = 100000000;     // UART reference clock
std::uint32_t g_apb2Clock = 100000000;

extern "C" void SystemInit()
{
    // Unlock SLCR for register access
    reg(kSlcrBase, kSlcrUnlock) = kSlcrUnlockKey;

    // All PLLs and clocks are already configured by u-boot.
    // Clock globals are set above as compile-time constants.
    // Future: read actual frequencies from SLCR PLL status registers.
}
