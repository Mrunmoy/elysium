#ifndef TEST_MOCK_REGISTERS_H
#define TEST_MOCK_REGISTERS_H

// Mock register file for host-side HAL testing.
//
// The STM32F4 HAL implementation accesses registers via absolute addresses
// (e.g., 0x40020000 for GPIOA). On the host, these addresses are not mapped.
//
// This mock provides a flat byte array that can be mapped to those addresses
// for testing register read/write logic.
//
// Since the HAL uses reinterpret_cast to volatile pointers at compile-time
// constant addresses, true host-side register mocking requires either:
//   (a) a HAL abstraction with injectable register interface, or
//   (b) link-time substitution of HAL functions.
//
// For Phase 0, we test the public HAL API behaviors using approach (b):
// each test provides its own stub implementation of HAL functions that
// record calls and verify parameters.

#include <cstdint>
#include <vector>

namespace test
{
    struct GpioInitCall
    {
        std::uint8_t port;
        std::uint8_t pin;
        std::uint8_t mode;
        std::uint8_t pull;
        std::uint8_t speed;
        std::uint8_t outputType;
        std::uint8_t alternateFunction;
    };

    struct GpioPinAction
    {
        enum class Type
        {
            Set,
            Clear,
            Toggle
        };
        Type type;
        std::uint8_t port;
        std::uint8_t pin;
    };

    // Global recording state (reset between tests)
    inline std::vector<GpioInitCall> g_gpioInitCalls;
    inline std::vector<GpioPinAction> g_gpioPinActions;
    inline bool g_gpioReadValue = false;

    inline void resetMockState()
    {
        g_gpioInitCalls.clear();
        g_gpioPinActions.clear();
        g_gpioReadValue = false;
    }

}  // namespace test

#endif  // TEST_MOCK_REGISTERS_H
