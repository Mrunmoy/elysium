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
    constexpr std::uint32_t kI2sCfgr = 0x1C;

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

    // SPI slave RX interrupt state
    struct SpiSlaveRxState
    {
        hal::SpiSlaveRxCallbackFn callback = nullptr;
        void *arg = nullptr;
        bool active = false;
    };

    SpiSlaveRxState s_spiSlaveState[3];

    // NVIC registers
    constexpr std::uint32_t kNvicIser = 0xE000E100;
    constexpr std::uint32_t kNvicIpr = 0xE000E400;

    void nvicEnableIrq(std::uint8_t irqn)
    {
        reg(kNvicIser + (irqn / 32) * 4) = 1U << (irqn % 32);
    }

    void nvicSetPriority(std::uint8_t irqn, std::uint8_t priority)
    {
        volatile auto *ipr = reinterpret_cast<volatile std::uint8_t *>(kNvicIpr);
        ipr[irqn] = priority;
    }

    std::uint8_t spiIrqNumber(hal::SpiId id)
    {
        switch (id)
        {
            case hal::SpiId::Spi1: return 35;
            case hal::SpiId::Spi2: return 36;
            case hal::SpiId::Spi3: return 51;
            default: return 35;
        }
    }

    void handleSpiIrq(std::uint8_t idx)
    {
        // Async master transfer path
        auto &st = s_spiState[idx];
        if (st.active)
        {
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
                    reg(base + kCr2) &=
                        ~((1U << kCr2Txeie) | (1U << kCr2Rxneie) | (1U << kCr2Errie));
                    st.active = false;

                    if (st.callback)
                    {
                        st.callback(st.arg);
                    }
                }
            }
            return;
        }

        // Slave RX interrupt path
        auto &slave = s_spiSlaveState[idx];
        if (slave.active)
        {
            std::uint32_t base = spiBase(static_cast<hal::SpiId>(idx));
            if (reg(base + kSr) & (1U << kSrRxne))
            {
                std::uint8_t rxByte = static_cast<std::uint8_t>(reg(base + kDr));
                if (slave.callback)
                {
                    slave.callback(slave.arg, rxByte);
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
            cr1 |= (1U << kCr1Ssm);
            if (config.master)
            {
                cr1 |= (1U << kCr1Ssi);  // SSI=1 for master (prevent MODF)
            }
            // SSI=0 for slave (slave selected)
        }

        reg(base + kCr1) = cr1;

        // Clear CR2 (no interrupts initially)
        reg(base + kCr2) = 0;

        // Ensure SPI mode, not I2S (clear I2SMOD bit in I2SCFGR)
        reg(base + kI2sCfgr) &= ~(1U << 11);

        // Enable SPI for master mode immediately.
        // For slave mode, SPE is deferred so the application can pre-load
        // DR before enabling (per RM0090: "the data byte must be written
        // to SPI_DR before the master starts to transmit").
        if (config.master)
        {
            reg(base + kCr1) |= (1U << kCr1Spe);
        }
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

        // Enable NVIC for SPI IRQ
        nvicSetPriority(spiIrqNumber(id), 0x80);
        nvicEnableIrq(spiIrqNumber(id));

        // Enable RXNE and TXE interrupts
        reg(base + kCr2) |= (1U << kCr2Rxneie) | (1U << kCr2Txeie) | (1U << kCr2Errie);

        restoreIrq(saved);
    }

    void spiSlaveRxInterruptEnable(SpiId id, SpiSlaveRxCallbackFn callback, void *arg)
    {
        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = spiBase(id);

        std::uint32_t saved = disableIrq();

        auto &slave = s_spiSlaveState[idx];
        slave.callback = callback;
        slave.arg = arg;
        slave.active = true;

        // Enable NVIC for SPI IRQ
        nvicSetPriority(spiIrqNumber(id), 0x80);
        nvicEnableIrq(spiIrqNumber(id));

        // Enable SPE if not already set.  For slave mode, spiInit()
        // defers SPE so this is the first time it gets set.
        reg(base + kCr1) |= (1U << kCr1Spe);

        // Enable RXNE interrupt in CR2
        reg(base + kCr2) |= (1U << kCr2Rxneie);

        restoreIrq(saved);
    }

    void spiSlaveRxInterruptDisable(SpiId id)
    {
        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = spiBase(id);

        std::uint32_t saved = disableIrq();

        // Disable RXNE interrupt in CR2
        reg(base + kCr2) &= ~(1U << kCr2Rxneie);

        auto &slave = s_spiSlaveState[idx];
        slave.callback = nullptr;
        slave.arg = nullptr;
        slave.active = false;

        restoreIrq(saved);
    }

    void spiSlaveSetTxByte(SpiId id, std::uint8_t value)
    {
        std::uint32_t base = spiBase(id);
        reg(base + kDr) = value;
    }
}  // namespace hal

// ISR handlers
extern "C" void SPI1_IRQHandler() { handleSpiIrq(0); }
extern "C" void SPI2_IRQHandler() { handleSpiIrq(1); }
extern "C" void SPI3_IRQHandler() { handleSpiIrq(2); }
