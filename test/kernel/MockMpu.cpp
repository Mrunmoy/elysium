// Mock MPU implementation for host-side testing.
// Replaces kernel/src/arch/cortex-m*/Mpu.cpp at link time.
//
// The utility functions (mpuRoundUpSize, mpuSizeEncoding, mpuValidateStack,
// mpuComputeThreadConfig) are pure portable C++ and run directly.
// Only mpuInit() and mpuConfigureThreadRegion() are mocked (they access HW).

#include "kernel/Mpu.h"
#include "MockMpu.h"

#include <cstdint>

namespace kernel
{
    std::uint32_t mpuRoundUpSize(std::uint32_t size)
    {
        if (size <= 32)
        {
            return 32;
        }

        --size;
        size |= size >> 1;
        size |= size >> 2;
        size |= size >> 4;
        size |= size >> 8;
        size |= size >> 16;
        ++size;

        return size;
    }

    std::uint8_t mpuSizeEncoding(std::uint32_t size)
    {
        if (size < 32)
        {
            return 0;
        }

        if ((size & (size - 1)) != 0)
        {
            return 0;
        }

        return static_cast<std::uint8_t>(__builtin_ctz(size) - 1);
    }

    bool mpuValidateStack(const void *stackBase, std::uint32_t stackSize)
    {
        if (stackSize < 32)
        {
            return false;
        }

        if ((stackSize & (stackSize - 1)) != 0)
        {
            return false;
        }

        auto addr = reinterpret_cast<std::uintptr_t>(stackBase);
        if ((addr & (stackSize - 1)) != 0)
        {
            return false;
        }

        return true;
    }

    ThreadMpuConfig mpuComputeThreadConfig(const void *stackBase, std::uint32_t stackSize)
    {
        ThreadMpuConfig config;

        auto baseAddr = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(stackBase));

        std::uint8_t sizeEnc = mpuSizeEncoding(stackSize);
        std::uint8_t regionNum = static_cast<std::uint8_t>(MpuRegion::ThreadStack);

        // RBAR: base address | VALID (bit 4) | region number
        config.stackRbar = (baseAddr & ~0x1Fu) | (1u << 4) | regionNum;

        // RASR: size | AP=full access | XN | TEX normal non-cache | enable
        config.stackRasr = (static_cast<std::uint32_t>(sizeEnc) << 1)
                         | (0x03u << 24)    // AP full access
                         | (1u << 28)       // XN
                         | (0x01u << 19) | (1u << 18)  // TEX=001, S=1
                         | (1u << 0);       // enable

        return config;
    }

    void mpuConfigureThreadRegion(const ThreadMpuConfig &config)
    {
        test::g_mpuRegionConfigs.push_back({config.stackRbar, config.stackRasr});
    }

    void mpuInit()
    {
        test::g_mpuInitCalled = true;
    }

}  // namespace kernel
