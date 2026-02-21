// Cortex-A9 MPU/MMU stub.
//
// The Cortex-A9 has a full MMU (not a simple MPU like Cortex-M).
// MMU page table configuration is deferred to a future phase.
// For now, all MPU functions are stubs that compile cleanly.
//
// The pure utility functions (mpuRoundUpSize, mpuSizeEncoding,
// mpuValidateStack, mpuComputeThreadConfig) are kept identical
// to the Cortex-M implementation since they are portable math
// used by Thread.cpp to pre-compute TCB fields.

#include "kernel/Mpu.h"

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

        config.stackRbar = (baseAddr & ~0x1Fu) | (1u << 4) | regionNum;
        config.stackRasr = (static_cast<std::uint32_t>(sizeEnc) << 1)
                         | (0x03u << 24)
                         | (1u << 28)
                         | (0x01u << 19) | (1u << 18)
                         | (1u << 0);

        return config;
    }

    void mpuConfigureThreadRegion(const ThreadMpuConfig &)
    {
        // Stub: no MPU region updates on Cortex-A9 (no MPU hardware).
        // Context switch assembly does not update MPU registers for A9.
    }

    void mpuInit()
    {
        // Stub: no MPU to configure on Cortex-A9.
        // MMU configuration will be added in a future phase.
    }

}  // namespace kernel
