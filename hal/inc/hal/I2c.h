#pragma once

#include <cstddef>
#include <cstdint>

#include "msos/ErrorCode.h"

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
        Timeout,
        Invalid
    };

    // Map HAL-local I2C errors into global ms-os status codes.
    constexpr std::int32_t i2cErrorToStatus(I2cError err)
    {
        switch (err)
        {
            case I2cError::Ok:
                return msos::error::kOk;
            case I2cError::Nack:
                return msos::error::kNoAck;
            case I2cError::BusError:
                return msos::error::kIo;
            case I2cError::ArbitrationLost:
                return msos::error::kBusy;
            case I2cError::Timeout:
                return msos::error::kTimedOut;
            case I2cError::Invalid:
                return msos::error::kInvalid;
            default:
                return msos::error::kInvalid;
        }
    }

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
    // Invalid ID or null data: invokes callback with I2cError::Invalid.
    // Zero length: no-op, callback is NOT invoked.
    void i2cWriteAsync(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                       std::size_t length, I2cCallbackFn callback, void *arg);

    // Interrupt-driven async read.
    // Invalid ID or null data: invokes callback with I2cError::Invalid.
    // Zero length: no-op, callback is NOT invoked.
    void i2cReadAsync(I2cId id, std::uint8_t addr, std::uint8_t *data,
                      std::size_t length, I2cCallbackFn callback, void *arg);

    // --- Slave mode ---

    // Slave RX callback: called in ISR context when master write completes (STOP detected).
    using I2cSlaveRxCallbackFn = void (*)(void *arg, const std::uint8_t *data,
                                          std::size_t length);

    // Slave TX callback: called in ISR context when master requests a read (ADDR with R/W=1).
    // Fill data buffer, set *length. maxLength is buffer capacity.
    using I2cSlaveTxCallbackFn = void (*)(void *arg, std::uint8_t *data,
                                          std::size_t *length, std::size_t maxLength);

    // Init I2C in slave mode with 7-bit own address. Enables PE+ACK, does NOT
    // enable interrupts -- call i2cSlaveEnable() after init.
    void i2cSlaveInit(I2cId id, std::uint8_t ownAddr,
                      I2cSlaveRxCallbackFn rxCallback,
                      I2cSlaveTxCallbackFn txCallback, void *arg);

    // Enable slave event/error/buffer interrupts and NVIC.
    void i2cSlaveEnable(I2cId id);

    // Disable slave interrupts.
    void i2cSlaveDisable(I2cId id);

}  // namespace hal
