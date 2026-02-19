#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    // Initialize the kernel (thread subsystem, scheduler, idle thread)
    void init();

    // Create a thread and add it to the scheduler
    // Returns thread ID or kInvalidThreadId on failure
    ThreadId createThread(ThreadFunction function, void *arg, const char *name,
                          std::uint32_t *stack, std::uint32_t stackSize,
                          std::uint32_t timeSlice = 0);

    // Start the scheduler -- does not return
    void startScheduler();

    // Current thread yields its remaining time slice
    void yield();

    // Get the tick count since scheduler started
    std::uint32_t tickCount();

}  // namespace kernel

// ISR handlers (called from vector table, extern "C" linkage)
extern "C"
{
    void SysTick_Handler();
}

#endif  // KERNEL_KERNEL_H
