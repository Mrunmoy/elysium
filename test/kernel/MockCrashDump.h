// Mock state for crash dump host testing.
//
// Provides a captured UART output buffer and mock register values
// so we can test the crash dump formatting logic without hardware.

#pragma once

#include <string>
#include <cstdint>

namespace test
{
    // Captured output from faultPrint calls
    extern std::string g_crashOutput;

    // Mock SCB register values (set before calling faultHandlerC)
    extern std::uint32_t g_mockCfsr;
    extern std::uint32_t g_mockHfsr;
    extern std::uint32_t g_mockMmfar;
    extern std::uint32_t g_mockBfar;
    extern std::uint32_t g_mockShcsr;
    extern std::uint32_t g_mockCcr;

    // Mock USART1 registers
    extern std::uint32_t g_mockUsart1Cr1;

    // Mock RCC registers
    extern std::uint32_t g_mockRccAhb1enr;
    extern std::uint32_t g_mockRccApb2enr;

    void resetCrashDumpMockState();

}  // namespace test
