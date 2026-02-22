#pragma once

// Mock state recording for kernel host-side testing.
//
// The kernel arch layer (Arch.h) accesses hardware registers (NVIC, SysTick,
// GIC, etc). On the host, these addresses are not mapped.
//
// This mock records calls to arch functions so tests can verify the kernel
// core logic (thread creation, scheduling, context switch triggers) without
// any hardware dependency.

#include "kernel/Scheduler.h"

#include <cstdint>
#include <vector>

namespace test
{
    struct ContextSwitchTrigger
    {
        bool pendSvSet;
    };

    struct SysTickConfig
    {
        std::uint32_t ticks;
    };

    struct CriticalSectionAction
    {
        enum class Type
        {
            Enter,
            Exit
        };
        Type type;
    };

    // Global recording state (reset between tests)
    inline std::vector<ContextSwitchTrigger> g_contextSwitchTriggers;
    inline std::vector<SysTickConfig> g_sysTickConfigs;
    inline std::vector<CriticalSectionAction> g_criticalSectionActions;
    inline bool g_schedulerStarted = false;
    inline std::uint32_t g_basePriority = 0;
    inline bool g_isrContext = false;
    inline std::uint32_t g_wfiCount = 0;
    inline bool g_sleepOnExit = false;
    inline bool g_deepSleep = false;
    inline std::uint32_t g_tickCount = 0;

    inline void resetKernelMockState()
    {
        g_contextSwitchTriggers.clear();
        g_sysTickConfigs.clear();
        g_criticalSectionActions.clear();
        g_schedulerStarted = false;
        g_basePriority = 0;
        g_isrContext = false;
        g_wfiCount = 0;
        g_sleepOnExit = false;
        g_deepSleep = false;
        g_tickCount = 0;
    }

}  // namespace test

// Scheduler accessor for Mutex/Semaphore integration tests.
// Returns the global test scheduler (defined in MockKernelGlobals.cpp).
namespace kernel
{
namespace internal
{
    Scheduler &scheduler();
}  // namespace internal
}  // namespace kernel
