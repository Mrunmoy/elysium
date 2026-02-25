// STM32F2/F4 I2C driver
//
// I2C1: 0x40005400, I2C2: 0x40005800, I2C3: 0x40005C00 (all APB1)
// Registers: CR1(0x00), CR2(0x04), OAR1(0x08), OAR2(0x0C), DR(0x10),
//            SR1(0x14), SR2(0x18), CCR(0x1C), TRISE(0x20)

#include "hal/I2c.h"

#include "startup/SystemClock.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kI2c1Base = 0x40005400;
    constexpr std::uint32_t kI2c2Base = 0x40005800;
    constexpr std::uint32_t kI2c3Base = 0x40005C00;

    // Register offsets
    constexpr std::uint32_t kCr1 = 0x00;
    constexpr std::uint32_t kCr2 = 0x04;
    constexpr std::uint32_t kDr = 0x10;
    constexpr std::uint32_t kSr1 = 0x14;
    constexpr std::uint32_t kSr2 = 0x18;
    constexpr std::uint32_t kCcr = 0x1C;
    constexpr std::uint32_t kTrise = 0x20;

    // CR1 bits
    constexpr std::uint32_t kCr1Pe = 0;
    constexpr std::uint32_t kCr1Start = 8;
    constexpr std::uint32_t kCr1Stop = 9;
    constexpr std::uint32_t kCr1Ack = 10;
    constexpr std::uint32_t kCr1Swrst = 15;

    // CR2 bits
    constexpr std::uint32_t kCr2Iterren = 8;
    constexpr std::uint32_t kCr2Itevten = 9;
    constexpr std::uint32_t kCr2Itbufen = 10;

    // SR1 bits
    constexpr std::uint32_t kSr1Sb = 0;
    constexpr std::uint32_t kSr1Addr = 1;
    constexpr std::uint32_t kSr1Btf = 2;
    constexpr std::uint32_t kSr1Rxne = 6;
    constexpr std::uint32_t kSr1Txe = 7;
    constexpr std::uint32_t kSr1Berr = 8;
    constexpr std::uint32_t kSr1Arlo = 9;
    constexpr std::uint32_t kSr1Af = 10;

    // CCR bits
    constexpr std::uint32_t kCcrFs = 15;

    // Timeout counter for busy-waits (arbitrary large value)
    constexpr std::uint32_t kTimeout = 100000;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Read a volatile register and discard the value (used to clear flags)
    void readDiscard(std::uint32_t addr)
    {
        volatile std::uint32_t tmp = reg(addr);
        (void)tmp;
    }

    std::uint32_t i2cBase(hal::I2cId id)
    {
        switch (id)
        {
            case hal::I2cId::I2c1: return kI2c1Base;
            case hal::I2cId::I2c2: return kI2c2Base;
            case hal::I2cId::I2c3: return kI2c3Base;
            default: return kI2c1Base;
        }
    }

    hal::I2cError checkErrors(std::uint32_t base)
    {
        std::uint32_t sr1 = reg(base + kSr1);

        if (sr1 & (1U << kSr1Af))
        {
            // Clear NACK flag
            reg(base + kSr1) = ~(1U << kSr1Af);
            reg(base + kCr1) |= (1U << kCr1Stop);
            return hal::I2cError::Nack;
        }
        if (sr1 & (1U << kSr1Berr))
        {
            reg(base + kSr1) = ~(1U << kSr1Berr);
            reg(base + kCr1) |= (1U << kCr1Stop);
            return hal::I2cError::BusError;
        }
        if (sr1 & (1U << kSr1Arlo))
        {
            reg(base + kSr1) = ~(1U << kSr1Arlo);
            return hal::I2cError::ArbitrationLost;
        }
        return hal::I2cError::Ok;
    }

    bool waitFlag(std::uint32_t base, std::uint32_t regOff, std::uint32_t bit)
    {
        for (std::uint32_t i = 0; i < kTimeout; ++i)
        {
            if (reg(base + regOff) & (1U << bit))
            {
                return true;
            }
        }
        return false;
    }

    struct I2cAsyncState
    {
        const std::uint8_t *txBuf = nullptr;
        std::uint8_t *rxBuf = nullptr;
        std::size_t length = 0;
        std::size_t index = 0;
        std::uint8_t addr = 0;
        bool isRead = false;
        bool active = false;
        hal::I2cCallbackFn callback = nullptr;
        void *arg = nullptr;
    };

    I2cAsyncState s_i2cState[3];

    void handleI2cEventIrq(std::uint8_t idx)
    {
        auto &st = s_i2cState[idx];
        if (!st.active)
        {
            return;
        }

        std::uint32_t base = i2cBase(static_cast<hal::I2cId>(idx));
        std::uint32_t sr1 = reg(base + kSr1);

        // START sent
        if (sr1 & (1U << kSr1Sb))
        {
            if (st.isRead)
            {
                reg(base + kDr) = (st.addr << 1) | 1;
            }
            else
            {
                reg(base + kDr) = (st.addr << 1) | 0;
            }
            return;
        }

        // ADDR matched
        if (sr1 & (1U << kSr1Addr))
        {
            // Clear ADDR by reading SR1 + SR2
            readDiscard(base + kSr1);
            readDiscard(base + kSr2);

            if (st.isRead && st.length == 1)
            {
                // Single-byte read: disable ACK before clearing ADDR
                reg(base + kCr1) &= ~(1U << kCr1Ack);
                reg(base + kCr1) |= (1U << kCr1Stop);
            }
            return;
        }

        // TX empty (write mode)
        if (!st.isRead && (sr1 & (1U << kSr1Txe)))
        {
            if (st.index < st.length)
            {
                reg(base + kDr) = st.txBuf[st.index++];
            }
            else
            {
                // All bytes sent, send STOP
                reg(base + kCr1) |= (1U << kCr1Stop);
                reg(base + kCr2) &= ~((1U << kCr2Itevten) | (1U << kCr2Itbufen));
                st.active = false;
                if (st.callback)
                {
                    st.callback(st.arg, hal::I2cError::Ok);
                }
            }
            return;
        }

        // RX not empty (read mode)
        if (st.isRead && (sr1 & (1U << kSr1Rxne)))
        {
            st.rxBuf[st.index++] = static_cast<std::uint8_t>(reg(base + kDr));

            if (st.index >= st.length)
            {
                reg(base + kCr2) &= ~((1U << kCr2Itevten) | (1U << kCr2Itbufen));
                st.active = false;
                if (st.callback)
                {
                    st.callback(st.arg, hal::I2cError::Ok);
                }
            }
            else if (st.index == st.length - 1)
            {
                // Next byte is last: disable ACK, send STOP
                reg(base + kCr1) &= ~(1U << kCr1Ack);
                reg(base + kCr1) |= (1U << kCr1Stop);
            }
        }
    }

    void handleI2cErrorIrq(std::uint8_t idx)
    {
        auto &st = s_i2cState[idx];
        if (!st.active)
        {
            return;
        }

        std::uint32_t base = i2cBase(static_cast<hal::I2cId>(idx));
        hal::I2cError err = checkErrors(base);

        if (err != hal::I2cError::Ok)
        {
            reg(base + kCr2) &= ~((1U << kCr2Itevten) | (1U << kCr2Itbufen) | (1U << kCr2Iterren));
            st.active = false;
            if (st.callback)
            {
                st.callback(st.arg, err);
            }
        }
    }
}  // namespace

namespace hal
{
    void i2cInit(const I2cConfig &config)
    {
        std::uint32_t base = i2cBase(config.id);
        std::uint32_t apb1Mhz = g_apb1Clock / 1000000;

        // Software reset
        reg(base + kCr1) |= (1U << kCr1Swrst);
        reg(base + kCr1) &= ~(1U << kCr1Swrst);

        // Disable peripheral
        reg(base + kCr1) &= ~(1U << kCr1Pe);

        // Set APB1 frequency in CR2 (MHz value in bits [5:0])
        reg(base + kCr2) = apb1Mhz & 0x3F;

        // Configure clock control
        if (config.speed == I2cSpeed::Fast)
        {
            // Fast mode: CCR = APB1 / (3 * 400kHz)
            std::uint32_t ccr = g_apb1Clock / (3 * 400000);
            if (ccr < 1)
            {
                ccr = 1;
            }
            reg(base + kCcr) = ccr | (1U << kCcrFs);

            // TRISE = (300ns / tPCLK1) + 1
            reg(base + kTrise) = (apb1Mhz * 300 / 1000) + 1;
        }
        else
        {
            // Standard mode: CCR = APB1 / (2 * 100kHz)
            std::uint32_t ccr = g_apb1Clock / (2 * 100000);
            if (ccr < 4)
            {
                ccr = 4;
            }
            reg(base + kCcr) = ccr;

            // TRISE = (1000ns / tPCLK1) + 1 = APB1_MHz + 1
            reg(base + kTrise) = apb1Mhz + 1;
        }

        // Enable peripheral + ACK
        reg(base + kCr1) = (1U << kCr1Pe) | (1U << kCr1Ack);
    }

    I2cError i2cWrite(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                      std::size_t length)
    {
        std::uint32_t base = i2cBase(id);

        // Generate START
        reg(base + kCr1) |= (1U << kCr1Start);

        // Wait for SB
        if (!waitFlag(base, kSr1, kSr1Sb))
        {
            return I2cError::Timeout;
        }

        // Send address with write bit
        reg(base + kDr) = (addr << 1) | 0;

        // Wait for ADDR
        if (!waitFlag(base, kSr1, kSr1Addr))
        {
            I2cError err = checkErrors(base);
            return (err != I2cError::Ok) ? err : I2cError::Timeout;
        }

        // Clear ADDR by reading SR1 + SR2
        readDiscard(base + kSr1);
        readDiscard(base + kSr2);

        // Send data
        for (std::size_t i = 0; i < length; ++i)
        {
            if (!waitFlag(base, kSr1, kSr1Txe))
            {
                I2cError err = checkErrors(base);
                return (err != I2cError::Ok) ? err : I2cError::Timeout;
            }
            reg(base + kDr) = data[i];
        }

        // Wait for BTF (byte transfer finished)
        if (!waitFlag(base, kSr1, kSr1Btf))
        {
            I2cError err = checkErrors(base);
            return (err != I2cError::Ok) ? err : I2cError::Timeout;
        }

        // Generate STOP
        reg(base + kCr1) |= (1U << kCr1Stop);

        return I2cError::Ok;
    }

    I2cError i2cRead(I2cId id, std::uint8_t addr, std::uint8_t *data,
                     std::size_t length)
    {
        std::uint32_t base = i2cBase(id);

        if (length == 0)
        {
            return I2cError::Ok;
        }

        // Enable ACK
        reg(base + kCr1) |= (1U << kCr1Ack);

        // Generate START
        reg(base + kCr1) |= (1U << kCr1Start);

        // Wait for SB
        if (!waitFlag(base, kSr1, kSr1Sb))
        {
            return I2cError::Timeout;
        }

        // Send address with read bit
        reg(base + kDr) = (addr << 1) | 1;

        // Wait for ADDR
        if (!waitFlag(base, kSr1, kSr1Addr))
        {
            I2cError err = checkErrors(base);
            return (err != I2cError::Ok) ? err : I2cError::Timeout;
        }

        if (length == 1)
        {
            // Single byte: disable ACK before clearing ADDR
            reg(base + kCr1) &= ~(1U << kCr1Ack);

            // Clear ADDR
            readDiscard(base + kSr1);
            readDiscard(base + kSr2);

            // Generate STOP
            reg(base + kCr1) |= (1U << kCr1Stop);

            // Wait for RXNE
            if (!waitFlag(base, kSr1, kSr1Rxne))
            {
                return I2cError::Timeout;
            }
            data[0] = static_cast<std::uint8_t>(reg(base + kDr));
        }
        else
        {
            // Clear ADDR
            readDiscard(base + kSr1);
            readDiscard(base + kSr2);

            for (std::size_t i = 0; i < length; ++i)
            {
                if (i == length - 1)
                {
                    // Last byte: disable ACK, send STOP
                    reg(base + kCr1) &= ~(1U << kCr1Ack);
                    reg(base + kCr1) |= (1U << kCr1Stop);
                }

                if (!waitFlag(base, kSr1, kSr1Rxne))
                {
                    I2cError err = checkErrors(base);
                    return (err != I2cError::Ok) ? err : I2cError::Timeout;
                }
                data[i] = static_cast<std::uint8_t>(reg(base + kDr));
            }
        }

        return I2cError::Ok;
    }

    I2cError i2cWriteRead(I2cId id, std::uint8_t addr,
                          const std::uint8_t *txData, std::size_t txLength,
                          std::uint8_t *rxData, std::size_t rxLength)
    {
        std::uint32_t base = i2cBase(id);

        // --- Write phase (no STOP) ---

        // Generate START
        reg(base + kCr1) |= (1U << kCr1Start);

        if (!waitFlag(base, kSr1, kSr1Sb))
        {
            return I2cError::Timeout;
        }

        reg(base + kDr) = (addr << 1) | 0;

        if (!waitFlag(base, kSr1, kSr1Addr))
        {
            I2cError err = checkErrors(base);
            return (err != I2cError::Ok) ? err : I2cError::Timeout;
        }

        readDiscard(base + kSr1);
        readDiscard(base + kSr2);

        for (std::size_t i = 0; i < txLength; ++i)
        {
            if (!waitFlag(base, kSr1, kSr1Txe))
            {
                I2cError err = checkErrors(base);
                return (err != I2cError::Ok) ? err : I2cError::Timeout;
            }
            reg(base + kDr) = txData[i];
        }

        if (!waitFlag(base, kSr1, kSr1Btf))
        {
            I2cError err = checkErrors(base);
            return (err != I2cError::Ok) ? err : I2cError::Timeout;
        }

        // --- Read phase (repeated START) ---
        return i2cRead(id, addr, rxData, rxLength);
    }

    void i2cWriteAsync(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                       std::size_t length, I2cCallbackFn callback, void *arg)
    {
        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = i2cBase(id);

        auto &st = s_i2cState[idx];
        st.txBuf = data;
        st.rxBuf = nullptr;
        st.length = length;
        st.index = 0;
        st.addr = addr;
        st.isRead = false;
        st.active = true;
        st.callback = callback;
        st.arg = arg;

        // Enable event, buffer, and error interrupts
        reg(base + kCr2) |= (1U << kCr2Itevten) | (1U << kCr2Itbufen) | (1U << kCr2Iterren);

        // Generate START
        reg(base + kCr1) |= (1U << kCr1Start);
    }

    void i2cReadAsync(I2cId id, std::uint8_t addr, std::uint8_t *data,
                      std::size_t length, I2cCallbackFn callback, void *arg)
    {
        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = i2cBase(id);

        auto &st = s_i2cState[idx];
        st.txBuf = nullptr;
        st.rxBuf = data;
        st.length = length;
        st.index = 0;
        st.addr = addr;
        st.isRead = true;
        st.active = true;
        st.callback = callback;
        st.arg = arg;

        // Enable ACK
        reg(base + kCr1) |= (1U << kCr1Ack);

        // Enable event, buffer, and error interrupts
        reg(base + kCr2) |= (1U << kCr2Itevten) | (1U << kCr2Itbufen) | (1U << kCr2Iterren);

        // Generate START
        reg(base + kCr1) |= (1U << kCr1Start);
    }
}  // namespace hal

// ISR handlers
extern "C" void I2C1_EV_IRQHandler() { handleI2cEventIrq(0); }
extern "C" void I2C1_ER_IRQHandler() { handleI2cErrorIrq(0); }
extern "C" void I2C2_EV_IRQHandler() { handleI2cEventIrq(1); }
extern "C" void I2C2_ER_IRQHandler() { handleI2cErrorIrq(1); }
extern "C" void I2C3_EV_IRQHandler() { handleI2cEventIrq(2); }
extern "C" void I2C3_ER_IRQHandler() { handleI2cErrorIrq(2); }
