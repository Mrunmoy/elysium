// Mock I2C implementation for host-side testing.
// Replaces hal/src/stm32f4/I2c.cpp at link time.

#include "hal/I2c.h"

#include "MockRegisters.h"

namespace hal
{
    namespace
    {
        constexpr std::uint8_t kI2cCount = 3;

        bool isValidI2cId(I2cId id)
        {
            return static_cast<std::uint8_t>(id) < kI2cCount;
        }
    }  // namespace

    void i2cInit(const I2cConfig &config)
    {
        if (!isValidI2cId(config.id))
        {
            return;
        }

        test::g_i2cInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            static_cast<std::uint8_t>(config.speed),
            config.analogFilter,
            config.digitalFilterCoeff,
        });
    }

    I2cError i2cWrite(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                      std::size_t length)
    {
        if (!isValidI2cId(id))
        {
            return I2cError::Invalid;
        }
        if (length > 0 && data == nullptr)
        {
            return I2cError::Invalid;
        }

        (void)data;
        test::g_i2cWriteCalls.push_back({
            static_cast<std::uint8_t>(id),
            addr,
            length,
        });
        return static_cast<I2cError>(test::g_i2cReturnError);
    }

    I2cError i2cRead(I2cId id, std::uint8_t addr, std::uint8_t *data,
                     std::size_t length)
    {
        if (!isValidI2cId(id))
        {
            return I2cError::Invalid;
        }
        if (length > 0 && data == nullptr)
        {
            return I2cError::Invalid;
        }

        test::g_i2cReadCalls.push_back({
            static_cast<std::uint8_t>(id),
            addr,
            length,
        });

        // Fill data from injectable buffer
        if (data)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                if (test::g_i2cRxReadPos < test::g_i2cRxData.size())
                {
                    data[i] = test::g_i2cRxData[test::g_i2cRxReadPos++];
                }
                else
                {
                    data[i] = 0;
                }
            }
        }

        return static_cast<I2cError>(test::g_i2cReturnError);
    }

    I2cError i2cWriteRead(I2cId id, std::uint8_t addr,
                          const std::uint8_t *txData, std::size_t txLength,
                          std::uint8_t *rxData, std::size_t rxLength)
    {
        if (!isValidI2cId(id))
        {
            return I2cError::Invalid;
        }
        if (txLength > 0 && txData == nullptr)
        {
            return I2cError::Invalid;
        }
        if (rxLength > 0 && rxData == nullptr)
        {
            return I2cError::Invalid;
        }

        (void)txData;
        test::g_i2cWriteReadCalls.push_back({
            static_cast<std::uint8_t>(id),
            addr,
            txLength,
            rxLength,
        });

        // Fill rxData from injectable buffer
        if (rxData)
        {
            for (std::size_t i = 0; i < rxLength; ++i)
            {
                if (test::g_i2cRxReadPos < test::g_i2cRxData.size())
                {
                    rxData[i] = test::g_i2cRxData[test::g_i2cRxReadPos++];
                }
                else
                {
                    rxData[i] = 0;
                }
            }
        }

        return static_cast<I2cError>(test::g_i2cReturnError);
    }

    void i2cWriteAsync(I2cId id, std::uint8_t addr, const std::uint8_t *data,
                       std::size_t length, I2cCallbackFn callback, void *arg)
    {
        if (!isValidI2cId(id))
        {
            if (callback != nullptr)
            {
                callback(arg, I2cError::Invalid);
            }
            return;
        }
        if (length == 0)
        {
            return;
        }
        if (data == nullptr)
        {
            if (callback != nullptr)
            {
                callback(arg, I2cError::Invalid);
            }
            return;
        }

        (void)data;
        test::g_i2cWriteCalls.push_back({
            static_cast<std::uint8_t>(id),
            addr,
            length,
        });

        ++test::g_i2cAsyncWriteCount;
        test::g_i2cAsyncCallback = reinterpret_cast<void *>(callback);
        test::g_i2cAsyncArg = arg;

        // Immediately invoke callback
        if (callback)
        {
            callback(arg, static_cast<I2cError>(test::g_i2cReturnError));
        }
    }

    void i2cReadAsync(I2cId id, std::uint8_t addr, std::uint8_t *data,
                      std::size_t length, I2cCallbackFn callback, void *arg)
    {
        if (!isValidI2cId(id))
        {
            if (callback != nullptr)
            {
                callback(arg, I2cError::Invalid);
            }
            return;
        }
        if (length == 0)
        {
            return;
        }
        if (data == nullptr)
        {
            if (callback != nullptr)
            {
                callback(arg, I2cError::Invalid);
            }
            return;
        }

        test::g_i2cReadCalls.push_back({
            static_cast<std::uint8_t>(id),
            addr,
            length,
        });

        // Fill data from injectable buffer
        if (data)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                if (test::g_i2cRxReadPos < test::g_i2cRxData.size())
                {
                    data[i] = test::g_i2cRxData[test::g_i2cRxReadPos++];
                }
                else
                {
                    data[i] = 0;
                }
            }
        }

        ++test::g_i2cAsyncReadCount;
        test::g_i2cAsyncCallback = reinterpret_cast<void *>(callback);
        test::g_i2cAsyncArg = arg;

        if (callback)
        {
            callback(arg, static_cast<I2cError>(test::g_i2cReturnError));
        }
    }
    void i2cSlaveInit(I2cId id, std::uint8_t ownAddr,
                      I2cSlaveRxCallbackFn rxCallback,
                      I2cSlaveTxCallbackFn txCallback, void *arg)
    {
        if (!isValidI2cId(id))
        {
            return;
        }

        test::g_i2cSlaveInitCalls.push_back({
            static_cast<std::uint8_t>(id),
            ownAddr,
            reinterpret_cast<void *>(rxCallback),
            reinterpret_cast<void *>(txCallback),
            arg,
        });
        test::g_i2cSlaveRxCallback = reinterpret_cast<void *>(rxCallback);
        test::g_i2cSlaveTxCallback = reinterpret_cast<void *>(txCallback);
        test::g_i2cSlaveArg = arg;
    }

    void i2cSlaveEnable(I2cId id)
    {
        if (!isValidI2cId(id))
        {
            return;
        }
        ++test::g_i2cSlaveEnableCount;
        test::g_i2cSlaveActive = true;
    }

    void i2cSlaveDisable(I2cId id)
    {
        if (!isValidI2cId(id))
        {
            return;
        }
        ++test::g_i2cSlaveDisableCount;
        test::g_i2cSlaveActive = false;
    }

}  // namespace hal
