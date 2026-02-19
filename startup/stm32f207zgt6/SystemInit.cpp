// SystemInit -- Clock configuration for STM32F207ZGT6
//
// Configures: flash latency, HSE, PLL for 120 MHz SYSCLK,
// AHB/APB1/APB2 prescalers.
//
// Called from Reset_Handler before main().

#include <cstdint>

namespace
{
    // Register base addresses
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kFlashBase = 0x40023C00;

    // RCC register offsets
    constexpr std::uint32_t kRccCr = 0x00;
    constexpr std::uint32_t kRccPllcfgr = 0x04;
    constexpr std::uint32_t kRccCfgr = 0x08;

    // FLASH register offsets
    constexpr std::uint32_t kFlashAcr = 0x00;

    // Bit definitions
    constexpr std::uint32_t kRccCrHseon = 1U << 16;
    constexpr std::uint32_t kRccCrHserdy = 1U << 17;
    constexpr std::uint32_t kRccCrPllon = 1U << 24;
    constexpr std::uint32_t kRccCrPllrdy = 1U << 25;

    constexpr std::uint32_t kRccCfgrSw = 0x3U;
    constexpr std::uint32_t kRccCfgrSwPll = 0x2U;
    constexpr std::uint32_t kRccCfgrSws = 0x3U << 2;
    constexpr std::uint32_t kRccCfgrSwsPll = 0x2U << 2;

    // PLL configuration for 120 MHz from 25 MHz HSE
    // SYSCLK = HSE / M * N / P = 25 / 25 * 240 / 2 = 120 MHz
    // USB CLK = HSE / M * N / Q = 25 / 25 * 240 / 5 = 48 MHz
    constexpr std::uint32_t kPllM = 25;
    constexpr std::uint32_t kPllN = 240;
    constexpr std::uint32_t kPllP = 0;  // P=2 is encoded as 0
    constexpr std::uint32_t kPllQ = 5;
    constexpr std::uint32_t kPllSrcHse = 1U << 22;

    // Flash latency: 3 wait states for 120 MHz at 3.3V
    constexpr std::uint32_t kFlashLatency3Ws = 3U;
    constexpr std::uint32_t kFlashPrefetchEn = 1U << 8;
    constexpr std::uint32_t kFlashIcacheEn = 1U << 9;
    constexpr std::uint32_t kFlashDcacheEn = 1U << 10;

    // AHB prescaler /1, APB1 prescaler /4, APB2 prescaler /2
    // HCLK = 120 MHz, APB1 = 30 MHz, APB2 = 60 MHz
    constexpr std::uint32_t kCfgrHpreDiv1 = 0x0U << 4;
    constexpr std::uint32_t kCfgrPpre1Div4 = 0x5U << 10;
    constexpr std::uint32_t kCfgrPpre2Div2 = 0x4U << 13;

    volatile std::uint32_t &reg(std::uint32_t base, std::uint32_t offset)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(base + offset);
    }
}  // namespace

// Global variable expected by CMSIS/HAL
std::uint32_t SystemCoreClock = 120000000;

extern "C" void SystemInit()
{
    // Enable HSE oscillator and wait until ready
    reg(kRccBase, kRccCr) |= kRccCrHseon;
    while ((reg(kRccBase, kRccCr) & kRccCrHserdy) == 0)
    {
    }

    // Configure flash: 3 wait states, prefetch, instruction cache, data cache
    reg(kFlashBase, kFlashAcr) =
        kFlashLatency3Ws | kFlashPrefetchEn | kFlashIcacheEn | kFlashDcacheEn;

    // Configure bus prescalers: AHB /1, APB1 /4, APB2 /2
    std::uint32_t cfgr = reg(kRccBase, kRccCfgr);
    cfgr &= ~((0xFU << 4) | (0x7U << 10) | (0x7U << 13));
    cfgr |= kCfgrHpreDiv1 | kCfgrPpre1Div4 | kCfgrPpre2Div2;
    reg(kRccBase, kRccCfgr) = cfgr;

    // Configure PLL: source HSE, M=25, N=240, P=2, Q=5
    reg(kRccBase, kRccPllcfgr) =
        kPllM | (kPllN << 6) | (kPllP << 16) | kPllSrcHse | (kPllQ << 24);

    // Enable PLL and wait until ready
    reg(kRccBase, kRccCr) |= kRccCrPllon;
    while ((reg(kRccBase, kRccCr) & kRccCrPllrdy) == 0)
    {
    }

    // Switch system clock to PLL
    cfgr = reg(kRccBase, kRccCfgr);
    cfgr &= ~kRccCfgrSw;
    cfgr |= kRccCfgrSwPll;
    reg(kRccBase, kRccCfgr) = cfgr;

    // Wait until PLL is the active clock source
    while ((reg(kRccBase, kRccCfgr) & kRccCfgrSws) != kRccCfgrSwsPll)
    {
    }
}
