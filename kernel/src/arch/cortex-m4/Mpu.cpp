// ARMv7-M MPU configuration for Cortex-M4.
//
// Identical to Cortex-M3 -- MPU registers are at the same addresses and
// have the same layout across all ARMv7-M cores.

#include "kernel/Mpu.h"

#include <cstdint>

namespace
{
    // MPU registers (ARMv7-M)
    constexpr std::uint32_t kMpuBase = 0xE000ED90;
    constexpr std::uint32_t kMpuType = kMpuBase + 0x00;
    constexpr std::uint32_t kMpuCtrl = kMpuBase + 0x04;
    constexpr std::uint32_t kMpuRnr  = kMpuBase + 0x08;
    constexpr std::uint32_t kMpuRbar = kMpuBase + 0x0C;
    constexpr std::uint32_t kMpuRasr = kMpuBase + 0x10;

    // MPU CTRL bits
    constexpr std::uint32_t kMpuCtrlEnable    = 1u << 0;
    constexpr std::uint32_t kMpuCtrlHfnmiena  = 1u << 1;
    constexpr std::uint32_t kMpuCtrlPrivdefena = 1u << 2;

    // RBAR bits
    constexpr std::uint32_t kRbarValid  = 1u << 4;

    // RASR bits
    constexpr std::uint32_t kRasrEnable = 1u << 0;
    constexpr std::uint32_t kRasrXn     = 1u << 28;

    // AP field: bits [26:24]
    constexpr std::uint32_t kApPrivRwOnly     = 0x01u << 24;
    constexpr std::uint32_t kApFullAccess     = 0x03u << 24;
    constexpr std::uint32_t kApPrivUnprivRo   = 0x06u << 24;

    // TEX/S/C/B for memory types
    constexpr std::uint32_t kTexNormalNonCache = (0x01u << 19) | (1u << 18);
    constexpr std::uint32_t kTexDevice         = (0x00u << 19) | (1u << 18) | (1u << 16);
    constexpr std::uint32_t kTexStronglyOrdered = (0x00u << 19) | (1u << 18);
    constexpr std::uint32_t kTexNormalWt       = (0x00u << 19) | (1u << 18) | (1u << 17);

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    void configureRegion(std::uint32_t rbar, std::uint32_t rasr)
    {
        reg(kMpuRbar) = rbar;
        reg(kMpuRasr) = rasr;
    }

    extern "C" std::uint8_t _heap_start;
    extern "C" std::uint8_t _heap_end;

}  // namespace

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

        config.stackRbar = (baseAddr & ~0x1Fu) | kRbarValid | regionNum;
        config.stackRasr = (static_cast<std::uint32_t>(sizeEnc) << 1)
                         | kApFullAccess
                         | kRasrXn
                         | kTexNormalNonCache
                         | kRasrEnable;

        return config;
    }

    void mpuConfigureThreadRegion(const ThreadMpuConfig &config)
    {
        reg(kMpuRbar) = config.stackRbar;
        reg(kMpuRasr) = config.stackRasr;
    }

    void mpuInit()
    {
        reg(kMpuCtrl) = 0;
        __asm volatile("dsb" ::: "memory");
        __asm volatile("isb" ::: "memory");

        // Region 0: FLASH -- 512KB for F407, RO, execute allowed
        configureRegion(
            (0x08000000u & ~0x1Fu) | kRbarValid | static_cast<std::uint32_t>(MpuRegion::Flash),
            (static_cast<std::uint32_t>(mpuSizeEncoding(512u * 1024u)) << 1)
                | kApPrivUnprivRo
                | kTexNormalWt
                | kRasrEnable
        );

        // Region 1: Kernel SRAM -- 128KB, priv RW only, XN
        configureRegion(
            (0x20000000u & ~0x1Fu) | kRbarValid | static_cast<std::uint32_t>(MpuRegion::KernelSram),
            (static_cast<std::uint32_t>(mpuSizeEncoding(128u * 1024u)) << 1)
                | kApPrivRwOnly
                | kRasrXn
                | kTexNormalNonCache
                | kRasrEnable
        );

        // Region 2: Peripherals -- 512MB at 0x40000000, priv RW only, XN, device
        configureRegion(
            (0x40000000u & ~0x1Fu) | kRbarValid | static_cast<std::uint32_t>(MpuRegion::Peripherals),
            (static_cast<std::uint32_t>(mpuSizeEncoding(512u * 1024u * 1024u)) << 1)
                | kApPrivRwOnly
                | kRasrXn
                | kTexDevice
                | kRasrEnable
        );

        // Region 3: System (NVIC, SCB) -- 512MB at 0xE0000000
        configureRegion(
            (0xE0000000u & ~0x1Fu) | kRbarValid | static_cast<std::uint32_t>(MpuRegion::System),
            (static_cast<std::uint32_t>(mpuSizeEncoding(512u * 1024u * 1024u)) << 1)
                | kApPrivRwOnly
                | kRasrXn
                | kTexStronglyOrdered
                | kRasrEnable
        );

        // Region 5: Heap
        auto heapBase = reinterpret_cast<std::uintptr_t>(&_heap_start);
        auto heapEnd = reinterpret_cast<std::uintptr_t>(&_heap_end);
        auto heapSize = static_cast<std::uint32_t>(heapEnd - heapBase);
        std::uint32_t heapRegionSize = mpuRoundUpSize(heapSize);

        configureRegion(
            (static_cast<std::uint32_t>(heapBase) & ~0x1Fu)
                | kRbarValid | static_cast<std::uint32_t>(MpuRegion::Heap),
            (static_cast<std::uint32_t>(mpuSizeEncoding(heapRegionSize)) << 1)
                | kApFullAccess
                | kRasrXn
                | kTexNormalNonCache
                | kRasrEnable
        );

        // Enable MPU: PRIVDEFENA=1, HFNMIENA=0
        reg(kMpuCtrl) = kMpuCtrlEnable | kMpuCtrlPrivdefena;
        __asm volatile("dsb" ::: "memory");
        __asm volatile("isb" ::: "memory");
    }

}  // namespace kernel
