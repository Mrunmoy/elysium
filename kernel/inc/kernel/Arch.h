#pragma once

#include <cstdint>

namespace kernel
{
    // Forward declaration for assembly interop
    struct ThreadControlBlock;

    // Global TCB pointers used by context switch assembly.
    // Defined in Kernel.cpp, read/written by ContextSwitch.s
    extern "C"
    {
        extern ThreadControlBlock *g_currentTcb;
        extern ThreadControlBlock *g_nextTcb;
    }

    // Called by kernel when a thread function returns
    void kernelThreadExit();

namespace arch
{
    // Trigger a context switch.
    // Cortex-M: pends PendSV exception.
    // Cortex-A9: generates SGI #0 (lowest-priority software interrupt).
    void triggerContextSwitch();

    // Configure the system tick timer for periodic interrupts.
    // ticks: reload value (e.g., SystemCoreClock / 1000 for 1 ms)
    // Cortex-M: programs SysTick. Cortex-A9: programs SCU private timer.
    void configureSysTick(std::uint32_t ticks);

    // Enter/exit critical section (disable/enable interrupts)
    void enterCritical();
    void exitCritical();

    // Launch the first thread (no outgoing context to save).
    // Cortex-M: SVC. Cortex-A9: SVC.
    void startFirstThread();

    // Configure interrupt priorities for context switch and tick.
    void setInterruptPriorities();

    // Return the initial status register value for a new thread's stack frame.
    // Cortex-M: 0x01000000 (xPSR with Thumb bit set).
    // Cortex-A9: 0x1F (CPSR: SYS mode, ARM state, IRQ/FIQ enabled).
    std::uint32_t initialStatusRegister();

    // Return true if currently executing in an interrupt/exception handler.
    // Cortex-M: ICSR VECTACTIVE != 0.
    // Cortex-A9: CPSR mode bits != USR (0x10) and != SYS (0x1F).
    bool inIsrContext();

}  // namespace arch
}  // namespace kernel
