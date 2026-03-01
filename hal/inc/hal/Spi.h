#pragma once

#include <cstddef>
#include <cstdint>

namespace hal
{
    enum class SpiId : std::uint8_t
    {
        Spi1 = 0,
        Spi2,
        Spi3
    };

    enum class SpiMode : std::uint8_t
    {
        Mode0 = 0,  // CPOL=0, CPHA=0
        Mode1,      // CPOL=0, CPHA=1
        Mode2,      // CPOL=1, CPHA=0
        Mode3       // CPOL=1, CPHA=1
    };

    enum class SpiBaudPrescaler : std::uint8_t
    {
        Div2 = 0,
        Div4 = 1,
        Div8 = 2,
        Div16 = 3,
        Div32 = 4,
        Div64 = 5,
        Div128 = 6,
        Div256 = 7
    };

    enum class SpiDataSize : std::uint8_t
    {
        Bits8 = 0,
        Bits16 = 1
    };

    enum class SpiBitOrder : std::uint8_t
    {
        MsbFirst = 0,
        LsbFirst = 1
    };

    using SpiCallbackFn = void (*)(void *arg);

    struct SpiConfig
    {
        SpiId id;
        SpiMode mode = SpiMode::Mode0;
        SpiBaudPrescaler prescaler = SpiBaudPrescaler::Div8;
        SpiDataSize dataSize = SpiDataSize::Bits8;
        SpiBitOrder bitOrder = SpiBitOrder::MsbFirst;
        bool master = true;
        bool softwareNss = true;
    };

    void spiInit(const SpiConfig &config);

    // Full-duplex polled transfer: sends txData while receiving into rxData.
    // Either txData or rxData may be nullptr (send-only or receive-only).
    void spiTransfer(SpiId id, const std::uint8_t *txData, std::uint8_t *rxData,
                     std::size_t length);

    // Convenience: transfer a single byte, return received byte.
    std::uint8_t spiTransferByte(SpiId id, std::uint8_t txByte);

    // Interrupt-driven async transfer. Calls callback when complete.
    void spiTransferAsync(SpiId id, const std::uint8_t *txData, std::uint8_t *rxData,
                          std::size_t length, SpiCallbackFn callback, void *arg);

    // Slave RX callback: called in ISR context with each received byte.
    using SpiSlaveRxCallbackFn = void (*)(void *arg, std::uint8_t rxByte);

    // Enable SPI slave RXNE interrupt. Callback fires for each received byte.
    void spiSlaveRxInterruptEnable(SpiId id, SpiSlaveRxCallbackFn callback, void *arg);

    // Disable SPI slave RXNE interrupt.
    void spiSlaveRxInterruptDisable(SpiId id);

    // Pre-load a byte into DR for the slave's next TX (response to master).
    void spiSlaveSetTxByte(SpiId id, std::uint8_t value);

}  // namespace hal
