// Zynq-7000 DMA stub
//
// The Zynq PS has a DMA controller (DMAC, PL330) but it is not used
// in the current PYNQ-Z2 port. All functions are no-ops.

#include "hal/Dma.h"

namespace hal
{
    void dmaInit(const DmaConfig & /* config */) {}

    void dmaStart(DmaController /* controller */, DmaStream /* stream */,
                  std::uint32_t /* peripheralAddr */, std::uint32_t /* memoryAddr */,
                  std::uint16_t /* count */, DmaCallbackFn /* callback */, void * /* arg */)
    {
    }

    void dmaStop(DmaController /* controller */, DmaStream /* stream */) {}
    bool dmaIsBusy(DmaController /* controller */, DmaStream /* stream */) { return false; }
    std::uint16_t dmaRemaining(DmaController /* controller */, DmaStream /* stream */) { return 0; }
    void dmaInterruptEnable(DmaController /* controller */, DmaStream /* stream */) {}
    void dmaInterruptDisable(DmaController /* controller */, DmaStream /* stream */) {}
}  // namespace hal
