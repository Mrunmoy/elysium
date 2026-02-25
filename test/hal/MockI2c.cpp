// Mock I2C implementation for host-side testing.
// Replaces hal/src/stm32f4/I2c.cpp at link time.

#include "hal/I2c.h"

#include "MockRegisters.h"

namespace hal
{
    void i2cInit(const I2cConfig &config)
    {
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
}  // namespace hal
