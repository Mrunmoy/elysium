// Runtime board configuration populated from DTB at boot.
//
// Replaces the old compile-time constexpr BoardConfig.h per board.
// Call board::configInit() once at boot before accessing board::config().
//
// Thread safety: configInit() must be called once before any config() calls.
// After initialization, config() is read-only and safe to call concurrently.

#pragma once

#include <cstdint>

namespace hal
{
    enum class UartId : std::uint8_t;
}

namespace board
{
    struct MemoryRegion
    {
        const char *name;
        std::uint32_t base;
        std::uint32_t size;
    };

    struct PinConfig
    {
        char port;           // 'A'-'Z'
        std::uint8_t pin;
        std::uint8_t af;
    };

    static constexpr std::uint8_t kMaxMemoryRegions = 4;

    struct BoardConfig
    {
        // Identity
        const char *boardName;
        const char *mcu;
        const char *arch;

        // Clocks
        std::uint32_t systemClock;
        std::uint32_t apb1Clock;
        std::uint32_t apb2Clock;
        std::uint32_t hseClock;    // 0 if not present

        // Memory
        MemoryRegion memoryRegions[kMaxMemoryRegions];
        std::uint8_t memoryRegionCount;

        // Console
        const char *consoleUart;   // "usart1", "uart0", etc.
        std::uint32_t consoleBaud;
        bool hasConsoleTx;
        PinConfig consoleTx;
        bool hasConsoleRx;
        PinConfig consoleRx;

        // LED
        bool hasLed;
        PinConfig led;

        // Features
        bool hasFpu;
    };

    // Initialize board config from DTB blob.
    // Call once at boot before main() hardware setup.
    void configInit(const std::uint8_t *dtb, std::uint32_t dtbSize);

    // Global accessor (returns reference to static BoardConfig).
    const BoardConfig &config();

    // Map console UART string to hal::UartId enum.
    hal::UartId consoleUartId();

    // Reset board config (for testing only).
    void configReset();

}  // namespace board
