// STM32F2/F4 SPI driver
//
// SPI1: 0x40013000 (APB2), SPI2: 0x40003800 (APB1), SPI3: 0x40003C00 (APB1)
// Registers: CR1(0x00), CR2(0x04), SR(0x08), DR(0x0C)

#include "hal/Spi.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kSpi1Base = 0x40013000;
    constexpr std::uint32_t kSpi2Base = 0x40003800;
    constexpr std::uint32_t kSpi3Base = 0x40003C00;

    // Register offsets
    constexpr std::uint32_t kCr1 = 0x00;
    constexpr std::uint32_t kCr2 = 0x04;
    constexpr std::uint32_t kSr = 0x08;
    constexpr std::uint32_t kDr = 0x0C;

    // CR1 bit positions
    constexpr std::uint32_t kCr1Cpha = 0;
    constexpr std::uint32_t kCr1Cpol = 1;
    constexpr std::uint32_t kCr1Mstr = 2;
    constexpr std::uint32_t kCr1Br = 3;     // 3 bits [5:3]
    constexpr std::uint32_t kCr1Spe = 6;
    constexpr std::uint32_t kCr1LsbFirst = 7;
    constexpr std::uint32_t kCr1Ssi = 8;
    constexpr std::uint32_t kCr1Ssm = 9;
    constexpr std::uint32_t kCr1Dff = 11;

    // CR2 bit positions
    constexpr std::uint32_t kCr2Errie = 5;
    constexpr std::uint32_t kCr2Rxneie = 6;
    constexpr std::uint32_t kCr2Txeie = 7;

    // SR bit positions
    constexpr std::uint32_t kSrRxne = 0;
    constexpr std::uint32_t kSrTxe = 1;
    constexpr std::uint32_t kSrBsy = 7;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    std::uint32_t spiBase(hal::SpiId id)
    {
        switch (id)
        {
            case hal::SpiId::Spi1: return kSpi1Base;
            case hal::SpiId::Spi2: return kSpi2Base;
            case hal::SpiId::Spi3: return kSpi3Base;
            default: return kSpi1Base;
        }
    }

    std::uint32_t disableIrq()
    {
        std::uint32_t primask;
        __asm volatile("mrs %0, primask" : "=r"(primask));
        __asm volatile("cpsid i" ::: "memory");
        return primask;
    }

    void restoreIrq(std::uint32_t primask)
    {
        __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    }

    struct SpiAsyncState
    {
        const std::uint8_t *txBuf = nullptr;
        std::uint8_t *rxBuf = nullptr;
        std::size_t length = 0;
        std::size_t txIndex = 0;
        std::size_t rxIndex = 0;
        hal::SpiCallbackFn callback = nullptr;
        void *arg = nullptr;
        bool active = false;
    };

    SpiAsyncState s_spiState[3];

    void handleSpiIrq(std::uint8_t idx)
    {
        auto &st = s_spiState[idx];
        if (!st.active)
        {
            return;
        }

        std::uint32_t base = spiBase(static_cast<hal::SpiId>(idx));
        std::uint32_t sr = reg(base + kSr);

        // TX empty -- send next byte
        if ((sr & (1U << kSrTxe)) && st.txIndex < st.length)
        {
            if (st.txBuf)
            {
                reg(base + kDr) = st.txBuf[st.txIndex];
            }
            else
            {
                reg(base + kDr) = 0xFF;
            }
            ++st.txIndex;

            // Disable TXE interrupt if all bytes sent
            if (st.txIndex >= st.length)
            {
                reg(base + kCr2) &= ~(1U << kCr2Txeie);
            }
        }

        // RX not empty -- read byte
        if ((sr & (1U << kSrRxne)) && st.rxIndex < st.length)
        {
            std::uint8_t data = static_cast<std::uint8_t>(reg(base + kDr));
            if (st.rxBuf)
            {
                st.rxBuf[st.rxIndex] = data;
            }
            ++st.rxIndex;

            // Transfer complete
            if (st.rxIndex >= st.length)
            {
                // Disable all SPI interrupts
                reg(base + kCr2) &= ~((1U << kCr2Txeie) | (1U << kCr2Rxneie) | (1U << kCr2Errie));
                st.active = false;

                if (st.callback)
                {
                    st.callback(st.arg);
                }
            }
        }
    }
}  // namespace

namespace hal
{
    void spiInit(const SpiConfig &config)
    {
        std::uint32_t base = spiBase(config.id);

        // Disable SPI first
        reg(base + kCr1) &= ~(1U << kCr1Spe);

        // Build CR1
        std::uint32_t cr1 = 0;

        // Clock polarity and phase from mode
        switch (config.mode)
        {
            case SpiMode::Mode0:
                break;
            case SpiMode::Mode1:
                cr1 |= (1U << kCr1Cpha);
                break;
            case SpiMode::Mode2:
                cr1 |= (1U << kCr1Cpol);
                break;
            case SpiMode::Mode3:
                cr1 |= (1U << kCr1Cpol) | (1U << kCr1Cpha);
                break;
        }

        if (config.master)
        {
            cr1 |= (1U << kCr1Mstr);
        }

        cr1 |= (static_cast<std::uint32_t>(config.prescaler) << kCr1Br);

        if (config.dataSize == SpiDataSize::Bits16)
        {
            cr1 |= (1U << kCr1Dff);
        }

        if (config.bitOrder == SpiBitOrder::LsbFirst)
        {
            cr1 |= (1U << kCr1LsbFirst);
        }

        if (config.softwareNss)
        {
            cr1 |= (1U << kCr1Ssm) | (1U << kCr1Ssi);
        }

        reg(base + kCr1) = cr1;

        // Clear CR2 (no interrupts initially)
        reg(base + kCr2) = 0;

        // Enable SPI
        reg(base + kCr1) |= (1U << kCr1Spe);
    }

    void spiTransfer(SpiId id, const std::uint8_t *txData, std::uint8_t *rxData,
                     std::size_t length)
    {
        std::uint32_t base = spiBase(id);

        for (std::size_t i = 0; i < length; ++i)
        {
            // Wait for TXE
            while (!(reg(base + kSr) & (1U << kSrTxe)))
            {
            }

            // Send byte
            if (txData)
            {
                reg(base + kDr) = txData[i];
            }
            else
            {
                reg(base + kDr) = 0xFF;
            }

            // Wait for RXNE
            while (!(reg(base + kSr) & (1U << kSrRxne)))
            {
            }

            // Read byte
            std::uint8_t data = static_cast<std::uint8_t>(reg(base + kDr));
            if (rxData)
            {
                rxData[i] = data;
            }
        }

        // Wait until not busy
        while (reg(base + kSr) & (1U << kSrBsy))
        {
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
        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = spiBase(id);

        std::uint32_t saved = disableIrq();

        auto &st = s_spiState[idx];
        st.txBuf = txData;
        st.rxBuf = rxData;
        st.length = length;
        st.txIndex = 0;
        st.rxIndex = 0;
        st.callback = callback;
        st.arg = arg;
        st.active = true;

        // Enable RXNE and TXE interrupts
        reg(base + kCr2) |= (1U << kCr2Rxneie) | (1U << kCr2Txeie) | (1U << kCr2Errie);

        restoreIrq(saved);
    }
}  // namespace hal

// ISR handlers
extern "C" void SPI1_IRQHandler() { handleSpiIrq(0); }
extern "C" void SPI2_IRQHandler() { handleSpiIrq(1); }
extern "C" void SPI3_IRQHandler() { handleSpiIrq(2); }
