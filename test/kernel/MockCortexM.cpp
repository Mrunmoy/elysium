// Mock Cortex-M3 arch layer for host-side testing.
// Replaces kernel/src/arch/cortex-m3/CortexM.cpp at link time.

#include "kernel/CortexM.h"

#include "MockKernel.h"

namespace kernel
{
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

}  // namespace arch
}  // namespace kernel
