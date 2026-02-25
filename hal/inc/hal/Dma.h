#pragma once

#include <cstddef>
#include <cstdint>

namespace hal
{
    enum class DmaController : std::uint8_t
    {
        Dma1 = 0,
        Dma2
    };

    enum class DmaStream : std::uint8_t
    {
        Stream0 = 0,
        Stream1,
        Stream2,
        Stream3,
        Stream4,
        Stream5,
        Stream6,
        Stream7
    };

    enum class DmaChannel : std::uint8_t
    {
        Channel0 = 0,
        Channel1,
        Channel2,
        Channel3,
        Channel4,
        Channel5,
        Channel6,
        Channel7
    };

    enum class DmaDirection : std::uint8_t
    {
        PeriphToMemory = 0,
        MemoryToPeriph = 1,
        MemoryToMemory = 2
    };

    enum class DmaDataSize : std::uint8_t
    {
        Byte = 0,
        HalfWord = 1,
        Word = 2
    };

    enum class DmaPriority : std::uint8_t
    {
        Low = 0,
        Medium = 1,
        High = 2,
        VeryHigh = 3
    };

    // Callback flags OR'd together in the flags argument
    constexpr std::uint8_t kDmaFlagComplete = 0x01;
    constexpr std::uint8_t kDmaFlagError = 0x02;

    using DmaCallbackFn = void (*)(void *arg, std::uint8_t flags);

    struct DmaConfig
    {
        DmaController controller;
        DmaStream stream;
        DmaChannel channel;
        DmaDirection direction;
        DmaDataSize peripheralSize = DmaDataSize::Byte;
        DmaDataSize memorySize = DmaDataSize::Byte;
        bool peripheralIncrement = false;
        bool memoryIncrement = true;
        DmaPriority priority = DmaPriority::Low;
        bool circular = false;
    };

    void dmaInit(const DmaConfig &config);
    void dmaStart(DmaController controller, DmaStream stream,
                  std::uint32_t peripheralAddr, std::uint32_t memoryAddr,
                  std::uint16_t count, DmaCallbackFn callback, void *arg);
    void dmaStop(DmaController controller, DmaStream stream);
    bool dmaIsBusy(DmaController controller, DmaStream stream);
    std::uint16_t dmaRemaining(DmaController controller, DmaStream stream);
    void dmaInterruptEnable(DmaController controller, DmaStream stream);
    void dmaInterruptDisable(DmaController controller, DmaStream stream);

}  // namespace hal
