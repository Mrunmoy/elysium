// STM32F2/F4 Independent Watchdog (IWDG) driver.
//
// The IWDG is clocked from the LSI oscillator (~32 kHz).  Once started,
// it cannot be stopped -- only a reset will disable it.  The application
// must periodically call watchdogFeed() to reload the down-counter
// before it reaches zero (which triggers a system reset).
//
// Timeout formula:
//   timeout_ms = (prescaler_divider * reloadValue) / 32   (approx)

#include "hal/Watchdog.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kIwdgBase = 0x40003000;

    // Register offsets
    constexpr std::uint32_t kIwdgKr  = 0x00;   // Key register
    constexpr std::uint32_t kIwdgPr  = 0x04;   // Prescaler register
    constexpr std::uint32_t kIwdgRlr = 0x08;   // Reload register
    constexpr std::uint32_t kIwdgSr  = 0x0C;   // Status register

    // Key values
    constexpr std::uint32_t kKeyUnlock = 0x5555;    // Enable write to PR/RLR
    constexpr std::uint32_t kKeyReload = 0xAAAA;    // Reload counter
    constexpr std::uint32_t kKeyStart  = 0xCCCC;    // Start watchdog

    // Status register bits
    constexpr std::uint32_t kSrPvu = (1U << 0);     // Prescaler value update
    constexpr std::uint32_t kSrRvu = (1U << 1);     // Reload value update

    volatile std::uint32_t &reg(std::uint32_t offset)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kIwdgBase + offset);
    }
}  // namespace

namespace hal
{
    void watchdogInit(const WatchdogConfig &config)
    {
        // Unlock write access to prescaler and reload registers
        reg(kIwdgKr) = kKeyUnlock;

        // Wait until prescaler register is writable
        while ((reg(kIwdgSr) & kSrPvu) != 0)
        {
        }

        // Set prescaler
        reg(kIwdgPr) = static_cast<std::uint32_t>(config.prescaler);

        // Wait until reload register is writable
        while ((reg(kIwdgSr) & kSrRvu) != 0)
        {
        }

        // Set reload value (12-bit, mask to be safe)
        reg(kIwdgRlr) = config.reloadValue & 0x0FFF;

        // Reload the counter and start the watchdog
        reg(kIwdgKr) = kKeyReload;
        reg(kIwdgKr) = kKeyStart;
    }

    void watchdogFeed()
    {
        reg(kIwdgKr) = kKeyReload;
    }
}  // namespace hal
