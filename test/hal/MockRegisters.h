#pragma once

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

    // Watchdog mock state
    struct WatchdogInitCall
    {
        std::uint8_t prescaler;
        std::uint16_t reloadValue;
    };

    // Global recording state (reset between tests)
    inline std::vector<GpioInitCall> g_gpioInitCalls;
    inline std::vector<GpioPinAction> g_gpioPinActions;
    inline bool g_gpioReadValue = false;

    inline std::vector<WatchdogInitCall> g_watchdogInitCalls;
    inline std::uint32_t g_watchdogFeedCount = 0;

    // UART mock state
    struct UartInitCall
    {
        std::uint8_t id;
        std::uint32_t baudRate;
    };

    struct UartPutCharCall
    {
        std::uint8_t id;
        char c;
    };

    struct UartRxEnableCall
    {
        std::uint8_t id;
        void *notifyFn;
        void *notifyArg;
    };

    inline std::vector<UartInitCall> g_uartInitCalls;
    inline std::vector<UartPutCharCall> g_uartPutCharCalls;
    inline std::vector<UartRxEnableCall> g_uartRxEnableCalls;
    inline bool g_uartRxInterruptEnabled = false;
    inline std::uint32_t g_uartRxInterruptDisableCount = 0;

    // Injectable RX data for uartTryGetChar / uartGetChar
    inline std::vector<char> g_uartRxBuffer;
    inline std::size_t g_uartRxReadPos = 0;

    inline void resetMockState()
    {
        g_gpioInitCalls.clear();
        g_gpioPinActions.clear();
        g_gpioReadValue = false;

        g_watchdogInitCalls.clear();
        g_watchdogFeedCount = 0;

        g_uartInitCalls.clear();
        g_uartPutCharCalls.clear();
        g_uartRxEnableCalls.clear();
        g_uartRxInterruptEnabled = false;
        g_uartRxInterruptDisableCount = 0;
        g_uartRxBuffer.clear();
        g_uartRxReadPos = 0;
    }

}  // namespace test
