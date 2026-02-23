// Mock arch layer for host-side testing.
// Replaces kernel/src/arch/*/Arch.cpp at link time.

#include "kernel/Arch.h"

#include "MockKernel.h"

namespace kernel
{
// Syscall flag (host mock -- Arch.cpp defines it for cross-compile)
volatile bool g_inSyscall = false;
namespace arch
{
    void triggerContextSwitch()
    {
        test::g_contextSwitchTriggers.push_back({true});
    }

    void configureSysTick(std::uint32_t ticks)
    {
        test::g_sysTickConfigs.push_back({ticks});
    }

    void enterCritical()
    {
        test::g_criticalSectionActions.push_back(
            {test::CriticalSectionAction::Type::Enter});
    }

    void exitCritical()
    {
        test::g_criticalSectionActions.push_back(
            {test::CriticalSectionAction::Type::Exit});
    }

    void startFirstThread()
    {
        test::g_schedulerStarted = true;
    }

    void setInterruptPriorities()
    {
        // No-op for tests
    }

    std::uint32_t initialStatusRegister()
    {
        return 0x01000000u;    // xPSR: Thumb bit (matches Cortex-M)
    }

    bool inIsrContext()
    {
        return test::g_isrContext;
    }

    void waitForInterrupt()
    {
        ++test::g_wfiCount;
    }

    void enableSleepOnExit()
    {
        test::g_sleepOnExit = true;
    }

    void disableSleepOnExit()
    {
        test::g_sleepOnExit = false;
    }

    void enableDeepSleep()
    {
        test::g_deepSleep = true;
    }

    void disableDeepSleep()
    {
        test::g_deepSleep = false;
    }

}  // namespace arch
}  // namespace kernel
