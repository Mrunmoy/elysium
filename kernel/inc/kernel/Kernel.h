#pragma once

#include "kernel/Thread.h"

#include <cstdint>

namespace kernel
{
    // Initialize the kernel (thread subsystem, scheduler, idle thread)
    void init();

    // Create a thread and add it to the scheduler.
    // Returns thread ID or kInvalidThreadId on failure.
    // privileged: true = runs in privileged mode (direct kernel access),
    //             false = runs unprivileged (must use SVC for kernel calls).
    ThreadId createThread(ThreadFunction function, void *arg, const char *name,
                          std::uint32_t *stack, std::uint32_t stackSize,
                          std::uint8_t priority = kDefaultPriority,
                          std::uint32_t timeSlice = 0,
                          bool privileged = true);

    // Start the scheduler -- does not return
    void startScheduler();

    // Current thread yields its remaining time slice
    void yield();

    // Block the current thread for the given number of ticks
    void sleep(std::uint32_t ticks);

    // Get the tick count since scheduler started
    std::uint32_t tickCount();

}  // namespace kernel

// ISR handlers (called from vector table, extern "C" linkage)
extern "C"
{
    void SysTick_Handler();
}
