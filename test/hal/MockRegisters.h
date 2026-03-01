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
#include <string>
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

    // RCC mock state
    struct RccEnableCall
    {
        std::string peripheral;
        std::uint8_t id;
    };

    inline std::vector<RccEnableCall> g_rccEnableCalls;
    inline std::vector<RccEnableCall> g_rccDisableCalls;

    // DMA mock state
    struct DmaInitCall
    {
        std::uint8_t controller;
        std::uint8_t stream;
        std::uint8_t channel;
        std::uint8_t direction;
        std::uint8_t peripheralSize;
        std::uint8_t memorySize;
        bool peripheralIncrement;
        bool memoryIncrement;
        std::uint8_t priority;
        bool circular;
    };

    struct DmaStartCall
    {
        std::uint8_t controller;
        std::uint8_t stream;
        std::uint32_t peripheralAddr;
        std::uint32_t memoryAddr;
        std::uint16_t count;
        void *callback;
        void *arg;
    };

    inline std::vector<DmaInitCall> g_dmaInitCalls;
    inline std::vector<DmaStartCall> g_dmaStartCalls;
    inline std::uint32_t g_dmaStopCount = 0;
    inline bool g_dmaBusy = false;
    inline std::uint16_t g_dmaRemaining = 0;
    inline std::uint32_t g_dmaInterruptEnableCount = 0;
    inline std::uint32_t g_dmaInterruptDisableCount = 0;

    // SPI mock state
    struct SpiInitCall
    {
        std::uint8_t id;
        std::uint8_t mode;
        std::uint8_t prescaler;
        std::uint8_t dataSize;
        std::uint8_t bitOrder;
        bool master;
        bool softwareNss;
    };

    struct SpiTransferCall
    {
        std::uint8_t id;
        std::size_t length;
        bool hasTxData;
        bool hasRxData;
    };

    inline std::vector<SpiInitCall> g_spiInitCalls;
    inline std::vector<SpiTransferCall> g_spiTransferCalls;
    inline std::vector<std::uint8_t> g_spiRxData;
    inline std::size_t g_spiRxReadPos = 0;
    inline std::uint32_t g_spiAsyncCount = 0;
    inline void *g_spiAsyncCallback = nullptr;
    inline void *g_spiAsyncArg = nullptr;

    // SPI slave mock state
    struct SpiSlaveRxEnableCall
    {
        std::uint8_t id;
    };

    inline std::vector<SpiSlaveRxEnableCall> g_spiSlaveRxEnableCalls;
    inline std::uint32_t g_spiSlaveRxDisableCount = 0;
    inline void *g_spiSlaveRxCallback = nullptr;
    inline void *g_spiSlaveRxArg = nullptr;
    inline bool g_spiSlaveRxActive = false;
    inline std::vector<std::uint8_t> g_spiSlaveSetTxBytes;

    // I2C mock state
    struct I2cInitCall
    {
        std::uint8_t id;
        std::uint8_t speed;
        bool analogFilter;
        std::uint8_t digitalFilterCoeff;
    };

    struct I2cWriteCall
    {
        std::uint8_t id;
        std::uint8_t addr;
        std::size_t length;
    };

    struct I2cReadCall
    {
        std::uint8_t id;
        std::uint8_t addr;
        std::size_t length;
    };

    struct I2cWriteReadCall
    {
        std::uint8_t id;
        std::uint8_t addr;
        std::size_t txLength;
        std::size_t rxLength;
    };

    inline std::vector<I2cInitCall> g_i2cInitCalls;
    inline std::vector<I2cWriteCall> g_i2cWriteCalls;
    inline std::vector<I2cReadCall> g_i2cReadCalls;
    inline std::vector<I2cWriteReadCall> g_i2cWriteReadCalls;
    inline std::vector<std::uint8_t> g_i2cRxData;
    inline std::size_t g_i2cRxReadPos = 0;
    inline std::uint8_t g_i2cReturnError = 0;  // I2cError::Ok
    inline std::uint32_t g_i2cAsyncWriteCount = 0;
    inline std::uint32_t g_i2cAsyncReadCount = 0;
    inline void *g_i2cAsyncCallback = nullptr;
    inline void *g_i2cAsyncArg = nullptr;

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

        g_rccEnableCalls.clear();
        g_rccDisableCalls.clear();

        g_dmaInitCalls.clear();
        g_dmaStartCalls.clear();
        g_dmaStopCount = 0;
        g_dmaBusy = false;
        g_dmaRemaining = 0;
        g_dmaInterruptEnableCount = 0;
        g_dmaInterruptDisableCount = 0;

        g_spiInitCalls.clear();
        g_spiTransferCalls.clear();
        g_spiRxData.clear();
        g_spiRxReadPos = 0;
        g_spiAsyncCount = 0;
        g_spiAsyncCallback = nullptr;
        g_spiAsyncArg = nullptr;

        g_spiSlaveRxEnableCalls.clear();
        g_spiSlaveRxDisableCount = 0;
        g_spiSlaveRxCallback = nullptr;
        g_spiSlaveRxArg = nullptr;
        g_spiSlaveRxActive = false;
        g_spiSlaveSetTxBytes.clear();

        g_i2cInitCalls.clear();
        g_i2cWriteCalls.clear();
        g_i2cReadCalls.clear();
        g_i2cWriteReadCalls.clear();
        g_i2cRxData.clear();
        g_i2cRxReadPos = 0;
        g_i2cReturnError = 0;
        g_i2cAsyncWriteCount = 0;
        g_i2cAsyncReadCount = 0;
        g_i2cAsyncCallback = nullptr;
        g_i2cAsyncArg = nullptr;
    }

}  // namespace test
