// ARMv7-M MPU configuration for memory protection.
//
// Region layout (8 regions total):
//   0: FLASH       -- read-only (priv + unpriv), execute
//   1: Kernel SRAM -- priv RW only, no execute (default deny for unpriv)
//   2: Peripherals -- priv RW only, no execute, device memory
//   3: System      -- priv RW only, no execute (NVIC, SCB)
//   4: Thread stack -- full access, no execute (updated on context switch)
//   5: Heap        -- full access, no execute (set once during init)
//   6-7: Reserved  -- for future user regions
//
// PRIVDEFENA=1: privileged code gets default memory map.
// HFNMIENA=0: MPU disabled during HardFault so crash dump works.

#pragma once

#include <cstdint>

namespace kernel
{
    enum class MpuRegion : std::uint8_t
    {
        Flash = 0,
        KernelSram = 1,
        Peripherals = 2,
        System = 3,
        ThreadStack = 4,
        Heap = 5,
        User0 = 6,
        User1 = 7
    };

    struct ThreadMpuConfig
    {
        std::uint32_t stackRbar;   // pre-computed RBAR value
        std::uint32_t stackRasr;   // pre-computed RASR value
    };

    // Initialize MPU with static regions and enable it.
    void mpuInit();

    // Apply thread stack region during context switch.
    void mpuConfigureThreadRegion(const ThreadMpuConfig &config);

    // Compute RBAR/RASR for a thread stack region.
    ThreadMpuConfig mpuComputeThreadConfig(const void *stackBase, std::uint32_t stackSize);

    // Validate that a stack buffer meets MPU requirements:
    // power-of-2 size, aligned to its size, minimum 32 bytes.
    bool mpuValidateStack(const void *stackBase, std::uint32_t stackSize);

    // Round up to the next power of 2 (minimum 32).
    std::uint32_t mpuRoundUpSize(std::uint32_t size);

    // Encode a power-of-2 size into the 5-bit SIZE field for RASR.
    // SIZE = log2(size) - 1. Returns 0 if not a valid power of 2.
    std::uint8_t mpuSizeEncoding(std::uint32_t size);

}  // namespace kernel
