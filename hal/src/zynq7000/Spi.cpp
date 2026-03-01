// Zynq-7000 SPI stub
//
// The Zynq PS has SPI0/SPI1 controllers but they are not used
// in the current PYNQ-Z2 port. All functions are no-ops.

#include "hal/Spi.h"

namespace hal
{
    void spiInit(const SpiConfig & /* config */) {}

    void spiTransfer(SpiId /* id */, const std::uint8_t * /* txData */,
                     std::uint8_t * /* rxData */, std::size_t /* length */)
    {
    }

    std::uint8_t spiTransferByte(SpiId /* id */, std::uint8_t /* txByte */) { return 0; }

    void spiTransferAsync(SpiId /* id */, const std::uint8_t * /* txData */,
                          std::uint8_t * /* rxData */, std::size_t /* length */,
                          SpiCallbackFn /* callback */, void * /* arg */)
    {
    }

    void spiSlaveRxInterruptEnable(SpiId /* id */, SpiSlaveRxCallbackFn /* callback */,
                                   void * /* arg */)
    {
    }

    void spiSlaveRxInterruptDisable(SpiId /* id */) {}

    void spiSlaveSetTxByte(SpiId /* id */, std::uint8_t /* value */) {}
}  // namespace hal
