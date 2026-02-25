// Mock DMA implementation for host-side testing.
// Replaces hal/src/stm32f4/Dma.cpp at link time.

#include "hal/Dma.h"

#include "MockRegisters.h"

namespace hal
{
    void dmaInit(const DmaConfig &config)
    {
        test::g_dmaInitCalls.push_back({
            static_cast<std::uint8_t>(config.controller),
            static_cast<std::uint8_t>(config.stream),
            static_cast<std::uint8_t>(config.channel),
            static_cast<std::uint8_t>(config.direction),
            static_cast<std::uint8_t>(config.peripheralSize),
            static_cast<std::uint8_t>(config.memorySize),
            config.peripheralIncrement,
            config.memoryIncrement,
            static_cast<std::uint8_t>(config.priority),
            config.circular,
        });
    }

    void dmaStart(DmaController controller, DmaStream stream,
                  std::uint32_t peripheralAddr, std::uint32_t memoryAddr,
                  std::uint16_t count, DmaCallbackFn callback, void *arg)
    {
        test::g_dmaStartCalls.push_back({
            static_cast<std::uint8_t>(controller),
            static_cast<std::uint8_t>(stream),
            peripheralAddr,
            memoryAddr,
            count,
            reinterpret_cast<void *>(callback),
            arg,
        });
    }

    void dmaStop(DmaController /* controller */, DmaStream /* stream */)
    {
        ++test::g_dmaStopCount;
    }

    bool dmaIsBusy(DmaController /* controller */, DmaStream /* stream */)
    {
        return test::g_dmaBusy;
    }

    std::uint16_t dmaRemaining(DmaController /* controller */, DmaStream /* stream */)
    {
        return test::g_dmaRemaining;
    }

    void dmaInterruptEnable(DmaController /* controller */, DmaStream /* stream */)
    {
        ++test::g_dmaInterruptEnableCount;
    }

    void dmaInterruptDisable(DmaController /* controller */, DmaStream /* stream */)
    {
        ++test::g_dmaInterruptDisableCount;
    }
}  // namespace hal
