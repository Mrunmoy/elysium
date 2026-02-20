#pragma once

#include <cstdint>

namespace kernel
{
    // Forward declaration for assembly interop
    struct ThreadControlBlock;

    // Global TCB pointers used by PendSV_Handler (assembly)
    // These are defined in Kernel.cpp, read/written by ContextSwitch.s
    extern "C"
    {
        extern ThreadControlBlock *g_currentTcb;
        extern ThreadControlBlock *g_nextTcb;
    }

    // Called by kernel when a thread function returns
    void kernelThreadExit();

namespace arch
{
    // Trigger PendSV exception to perform context switch
    void triggerContextSwitch();

    // Configure SysTick timer for periodic interrupts
    // ticks: reload value (e.g., SystemCoreClock / 1000 for 1 ms)
    void configureSysTick(std::uint32_t ticks);

    // Enter/exit critical section (disable/enable interrupts)
    void enterCritical();
    void exitCritical();

    // Launch the first thread via SVC (no context to save)
    void startFirstThread();

    // Set PendSV and SysTick interrupt priorities
    void setInterruptPriorities();

}  // namespace arch
}  // namespace kernel
