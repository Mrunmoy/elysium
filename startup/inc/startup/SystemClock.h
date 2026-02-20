#pragma once

#include <cstdint>

// Global clock variables set by SystemInit() at boot.
// SystemCoreClock follows the CMSIS convention.
extern "C"
{
    extern std::uint32_t SystemCoreClock;
    extern std::uint32_t g_apb1Clock;
    extern std::uint32_t g_apb2Clock;
}
