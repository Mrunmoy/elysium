// Zynq-7000 I2C stub
//
// The Zynq PS has I2C0/I2C1 (Cadence I2C) but they are not used
// in the current PYNQ-Z2 port. All functions are no-ops.

#include "hal/I2c.h"

namespace hal
{
    void i2cInit(const I2cConfig & /* config */) {}

    I2cError i2cWrite(I2cId /* id */, std::uint8_t /* addr */,
                      const std::uint8_t * /* data */, std::size_t /* length */)
    {
        return I2cError::Ok;
    }

    I2cError i2cRead(I2cId /* id */, std::uint8_t /* addr */,
                     std::uint8_t * /* data */, std::size_t /* length */)
    {
        return I2cError::Ok;
    }

    I2cError i2cWriteRead(I2cId /* id */, std::uint8_t /* addr */,
                          const std::uint8_t * /* txData */, std::size_t /* txLength */,
                          std::uint8_t * /* rxData */, std::size_t /* rxLength */)
    {
        return I2cError::Ok;
    }

    void i2cWriteAsync(I2cId /* id */, std::uint8_t /* addr */,
                       const std::uint8_t * /* data */, std::size_t /* length */,
                       I2cCallbackFn /* callback */, void * /* arg */)
    {
    }

    void i2cReadAsync(I2cId /* id */, std::uint8_t /* addr */,
                      std::uint8_t * /* data */, std::size_t /* length */,
                      I2cCallbackFn /* callback */, void * /* arg */)
    {
    }
}  // namespace hal
