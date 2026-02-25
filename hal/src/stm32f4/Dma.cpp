// STM32F2/F4 DMA driver
//
// DMA1: 0x40026000, DMA2: 0x40026400
// Each controller has 8 streams, each stream has 6 registers at 0x18 stride.
// Stream registers start at base + 0x10.
//
// Interrupt status: LISR (streams 0-3), HISR (streams 4-7)
// Clear flags: LIFCR, HIFCR

#include "hal/Dma.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kDma1Base = 0x40026000;
    constexpr std::uint32_t kDma2Base = 0x40026400;

    // Global registers (offset from controller base)
    constexpr std::uint32_t kLisr = 0x00;
    constexpr std::uint32_t kHisr = 0x04;
    constexpr std::uint32_t kLifcr = 0x08;
    constexpr std::uint32_t kHifcr = 0x0C;

    // Per-stream registers (offset from stream base)
    constexpr std::uint32_t kSxCr = 0x00;
    constexpr std::uint32_t kSxNdtr = 0x04;
    constexpr std::uint32_t kSxPar = 0x08;
    constexpr std::uint32_t kSxM0ar = 0x0C;
    constexpr std::uint32_t kSxFcr = 0x14;

    // CR bit positions
    constexpr std::uint32_t kCrEn = 0;
    constexpr std::uint32_t kCrTeie = 2;
    constexpr std::uint32_t kCrTcie = 4;
    constexpr std::uint32_t kCrDir = 6;
    constexpr std::uint32_t kCrCirc = 8;
    constexpr std::uint32_t kCrPinc = 9;
    constexpr std::uint32_t kCrMinc = 10;
    constexpr std::uint32_t kCrPsize = 11;
    constexpr std::uint32_t kCrMsize = 13;
    constexpr std::uint32_t kCrPl = 16;
    constexpr std::uint32_t kCrChsel = 25;

    // IRQ status bit offsets per stream within LISR/HISR
    // Streams 0,4 -> bits 0-5;  1,5 -> bits 6-11;  2,6 -> bits 16-21;  3,7 -> bits 22-27
    constexpr std::uint32_t kStreamBitOffset[4] = {0, 6, 16, 22};

    // TCIF bit within each stream's status group
    constexpr std::uint32_t kTcif = 5;
    constexpr std::uint32_t kTeif = 3;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    std::uint32_t controllerBase(hal::DmaController c)
    {
        return (c == hal::DmaController::Dma1) ? kDma1Base : kDma2Base;
    }

    std::uint32_t streamBase(std::uint32_t base, hal::DmaStream s)
    {
        return base + 0x10 + static_cast<std::uint32_t>(s) * 0x18;
    }

    std::uint32_t disableIrq()
    {
        std::uint32_t primask;
        __asm volatile("mrs %0, primask" : "=r"(primask));
        __asm volatile("cpsid i" ::: "memory");
        return primask;
    }

    void restoreIrq(std::uint32_t primask)
    {
        __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    }

    struct DmaStreamState
    {
        hal::DmaCallbackFn callback = nullptr;
        void *arg = nullptr;
    };

    DmaStreamState s_state[2][8];

    void clearStreamFlags(std::uint32_t base, std::uint8_t streamIdx)
    {
        std::uint32_t localIdx = streamIdx & 0x03;
        std::uint32_t shift = kStreamBitOffset[localIdx];
        std::uint32_t mask = 0x3DU << shift;  // All 6 flag bits

        if (streamIdx < 4)
        {
            reg(base + kLifcr) = mask;
        }
        else
        {
            reg(base + kHifcr) = mask;
        }
    }

    void handleDmaIrq(std::uint8_t controllerIdx, std::uint8_t streamIdx)
    {
        std::uint32_t base = (controllerIdx == 0) ? kDma1Base : kDma2Base;
        std::uint32_t localIdx = streamIdx & 0x03;
        std::uint32_t shift = kStreamBitOffset[localIdx];

        // Read status register
        std::uint32_t status;
        if (streamIdx < 4)
        {
            status = reg(base + kLisr);
        }
        else
        {
            status = reg(base + kHisr);
        }

        std::uint8_t flags = 0;
        if (status & (1U << (shift + kTcif)))
        {
            flags |= hal::kDmaFlagComplete;
        }
        if (status & (1U << (shift + kTeif)))
        {
            flags |= hal::kDmaFlagError;
        }

        // Clear flags
        clearStreamFlags(base, streamIdx);

        // Invoke callback
        auto &st = s_state[controllerIdx][streamIdx];
        if (st.callback && flags)
        {
            st.callback(st.arg, flags);
        }
    }
}  // namespace

namespace hal
{
    void dmaInit(const DmaConfig &config)
    {
        std::uint32_t base = controllerBase(config.controller);
        std::uint32_t sbase = streamBase(base, config.stream);
        std::uint8_t streamIdx = static_cast<std::uint8_t>(config.stream);

        // Disable stream first
        reg(sbase + kSxCr) &= ~(1U << kCrEn);

        // Wait until EN bit clears
        while (reg(sbase + kSxCr) & (1U << kCrEn))
        {
        }

        // Clear all interrupt flags for this stream
        clearStreamFlags(base, streamIdx);

        // Build CR value
        std::uint32_t cr = 0;
        cr |= (static_cast<std::uint32_t>(config.channel) << kCrChsel);
        cr |= (static_cast<std::uint32_t>(config.direction) << kCrDir);
        cr |= (static_cast<std::uint32_t>(config.peripheralSize) << kCrPsize);
        cr |= (static_cast<std::uint32_t>(config.memorySize) << kCrMsize);
        cr |= (static_cast<std::uint32_t>(config.priority) << kCrPl);

        if (config.peripheralIncrement)
        {
            cr |= (1U << kCrPinc);
        }
        if (config.memoryIncrement)
        {
            cr |= (1U << kCrMinc);
        }
        if (config.circular)
        {
            cr |= (1U << kCrCirc);
        }

        reg(sbase + kSxCr) = cr;

        // Disable direct mode (use FIFO with threshold = full)
        reg(sbase + kSxFcr) = 0x21;  // DMDIS=1, FTH=01 (1/2 full)
    }

    void dmaStart(DmaController controller, DmaStream stream,
                  std::uint32_t peripheralAddr, std::uint32_t memoryAddr,
                  std::uint16_t count, DmaCallbackFn callback, void *arg)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        std::uint8_t ctrlIdx = static_cast<std::uint8_t>(controller);
        std::uint8_t streamIdx = static_cast<std::uint8_t>(stream);

        std::uint32_t saved = disableIrq();

        // Store callback
        s_state[ctrlIdx][streamIdx].callback = callback;
        s_state[ctrlIdx][streamIdx].arg = arg;

        // Configure addresses and count
        reg(sbase + kSxPar) = peripheralAddr;
        reg(sbase + kSxM0ar) = memoryAddr;
        reg(sbase + kSxNdtr) = count;

        // Clear flags
        clearStreamFlags(base, streamIdx);

        // Enable transfer complete and error interrupts, then enable stream
        std::uint32_t cr = reg(sbase + kSxCr);
        cr |= (1U << kCrTcie) | (1U << kCrTeie) | (1U << kCrEn);
        reg(sbase + kSxCr) = cr;

        restoreIrq(saved);
    }

    void dmaStop(DmaController controller, DmaStream stream)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        std::uint8_t streamIdx = static_cast<std::uint8_t>(stream);

        // Disable stream
        reg(sbase + kSxCr) &= ~(1U << kCrEn);

        // Wait until disabled
        while (reg(sbase + kSxCr) & (1U << kCrEn))
        {
        }

        // Clear flags
        clearStreamFlags(base, streamIdx);
    }

    bool dmaIsBusy(DmaController controller, DmaStream stream)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        return (reg(sbase + kSxCr) & (1U << kCrEn)) != 0;
    }

    std::uint16_t dmaRemaining(DmaController controller, DmaStream stream)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        return static_cast<std::uint16_t>(reg(sbase + kSxNdtr));
    }

    void dmaInterruptEnable(DmaController controller, DmaStream stream)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        std::uint32_t saved = disableIrq();
        reg(sbase + kSxCr) |= (1U << kCrTcie) | (1U << kCrTeie);
        restoreIrq(saved);
    }

    void dmaInterruptDisable(DmaController controller, DmaStream stream)
    {
        std::uint32_t base = controllerBase(controller);
        std::uint32_t sbase = streamBase(base, stream);
        std::uint32_t saved = disableIrq();
        reg(sbase + kSxCr) &= ~((1U << kCrTcie) | (1U << kCrTeie));
        restoreIrq(saved);
    }
}  // namespace hal

// ISR handlers -- DMA1 streams 0-7 (IRQs 11-17, 47)
extern "C" void DMA1_Stream0_IRQHandler() { handleDmaIrq(0, 0); }
extern "C" void DMA1_Stream1_IRQHandler() { handleDmaIrq(0, 1); }
extern "C" void DMA1_Stream2_IRQHandler() { handleDmaIrq(0, 2); }
extern "C" void DMA1_Stream3_IRQHandler() { handleDmaIrq(0, 3); }
extern "C" void DMA1_Stream4_IRQHandler() { handleDmaIrq(0, 4); }
extern "C" void DMA1_Stream5_IRQHandler() { handleDmaIrq(0, 5); }
extern "C" void DMA1_Stream6_IRQHandler() { handleDmaIrq(0, 6); }
extern "C" void DMA1_Stream7_IRQHandler() { handleDmaIrq(0, 7); }

// DMA2 streams 0-7 (IRQs 56-60, 68-70)
extern "C" void DMA2_Stream0_IRQHandler() { handleDmaIrq(1, 0); }
extern "C" void DMA2_Stream1_IRQHandler() { handleDmaIrq(1, 1); }
extern "C" void DMA2_Stream2_IRQHandler() { handleDmaIrq(1, 2); }
extern "C" void DMA2_Stream3_IRQHandler() { handleDmaIrq(1, 3); }
extern "C" void DMA2_Stream4_IRQHandler() { handleDmaIrq(1, 4); }
extern "C" void DMA2_Stream5_IRQHandler() { handleDmaIrq(1, 5); }
extern "C" void DMA2_Stream6_IRQHandler() { handleDmaIrq(1, 6); }
extern "C" void DMA2_Stream7_IRQHandler() { handleDmaIrq(1, 7); }
