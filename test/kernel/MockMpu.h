// Mock MPU state recording for host-side testing.

#pragma once

#include <cstdint>
#include <vector>

namespace test
{
    struct MpuRegionConfig
    {
        std::uint32_t rbar;
        std::uint32_t rasr;
    };

    inline bool g_mpuInitCalled = false;
    inline std::vector<MpuRegionConfig> g_mpuRegionConfigs;

    inline void resetMpuMockState()
    {
        g_mpuInitCalled = false;
        g_mpuRegionConfigs.clear();
    }

}  // namespace test
