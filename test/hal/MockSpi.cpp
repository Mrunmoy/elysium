// Mock SPI implementation for host-side testing.
// Replaces hal/src/stm32f4/Spi.cpp at link time.

#include "hal/Spi.h"

#include "MockRegisters.h"

namespace hal
{
    void spiInit(const SpiConfig &config)
    {
        test::g_spiInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            static_cast<std::uint8_t>(config.mode),
            static_cast<std::uint8_t>(config.prescaler),
            static_cast<std::uint8_t>(config.dataSize),
            static_cast<std::uint8_t>(config.bitOrder),
            config.master,
            config.softwareNss,
        });
    }

    void spiTransfer(SpiId id, const std::uint8_t *txData, std::uint8_t *rxData,
                     std::size_t length)
    {
        test::g_spiTransferCalls.push_back({
            static_cast<std::uint8_t>(id),
            length,
            txData != nullptr,
            rxData != nullptr,
        });

        // Fill rxData from injectable buffer
        if (rxData)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                if (test::g_spiRxReadPos < test::g_spiRxData.size())
                {
                    rxData[i] = test::g_spiRxData[test::g_spiRxReadPos++];
                }
                else
                {
                    rxData[i] = 0;
                }
            }
        }
    }

    std::uint8_t spiTransferByte(SpiId id, std::uint8_t txByte)
    {
        std::uint8_t rx = 0;
        spiTransfer(id, &txByte, &rx, 1);
        return rx;
    }

    void spiTransferAsync(SpiId id, const std::uint8_t *txData, std::uint8_t *rxData,
                          std::size_t length, SpiCallbackFn callback, void *arg)
    {
        test::g_spiTransferCalls.push_back({
            static_cast<std::uint8_t>(id),
            length,
            txData != nullptr,
            rxData != nullptr,
        });

        ++test::g_spiAsyncCount;
        test::g_spiAsyncCallback = reinterpret_cast<void *>(callback);
        test::g_spiAsyncArg = arg;

        // Fill rxData from injectable buffer
        if (rxData)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                if (test::g_spiRxReadPos < test::g_spiRxData.size())
                {
                    rxData[i] = test::g_spiRxData[test::g_spiRxReadPos++];
                }
                else
                {
                    rxData[i] = 0;
                }
            }
        }

        // Immediately invoke callback to simulate completion
        if (callback)
        {
            callback(arg);
        }
    }
}  // namespace hal
