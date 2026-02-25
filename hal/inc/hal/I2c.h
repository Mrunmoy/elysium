#pragma once

#include <cstddef>
#include <cstdint>

namespace hal
{
    enum class I2cId : std::uint8_t
    {
        I2c1 = 0,
        I2c2,
        I2c3
    };

    enum class I2cSpeed : std::uint8_t
    {
        Standard = 0,   // 100 kHz
        Fast = 1        // 400 kHz
    };

    enum class I2cError : std::uint8_t
    {
        Ok = 0,
        Nack,
        BusError,
        ArbitrationLost,
        Timeout
    };

    using I2cCallbackFn = void (*)(void *arg, I2cError error);

    struct I2cConfig
    {
        I2cId id;
        I2cSpeed speed = I2cSpeed::Standard;
        bool analogFilter = true;
        std::uint8_t digitalFilterCoeff = 0;
    };

    void i2cInit(const I2cConfig &config);

    // Polled write: send data to slave at addr (7-bit). Returns error code.
    I2cError i2cWrite(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                      std::size_t length);

    // Polled read: receive data from slave at addr (7-bit). Returns error code.
    I2cError i2cRead(I2cId id, std::uint8_t addr, std::uint8_t *data,
                     std::size_t length);

    // Polled write-then-read (repeated start): write txData, then read into rxData.
    I2cError i2cWriteRead(I2cId id, std::uint8_t addr,
                          const std::uint8_t *txData, std::size_t txLength,
                          std::uint8_t *rxData, std::size_t rxLength);

    // Interrupt-driven async write.
    void i2cWriteAsync(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                       std::size_t length, I2cCallbackFn callback, void *arg);

    // Interrupt-driven async read.
    void i2cReadAsync(I2cId id, std::uint8_t addr, std::uint8_t *data,
                      std::size_t length, I2cCallbackFn callback, void *arg);

}  // namespace hal
