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

    // Destroy a thread: remove from scheduler, clean up IPC, free TCB slot.
    // The thread must not be the currently running thread (use threadExit
    // or let the thread function return for self-termination).
    // Returns true on success, false if the ID is invalid or is the
    // idle thread.
    bool destroyThread(ThreadId id);

    // Start the scheduler -- does not return
    void startScheduler();

    // Current thread yields its remaining time slice
    void yield();

    // Block the current thread for the given number of ticks
    void sleep(std::uint32_t ticks);

    // Get the tick count since scheduler started
    std::uint32_t tickCount();

    // Enable the hardware watchdog.  The idle thread will automatically
    // feed it.  If any thread monopolises the CPU (starves idle), the
    // watchdog fires and resets the MCU.
    void watchdogStart(std::uint16_t reloadValue = 4095,
                       std::uint8_t prescaler = 4);

    // Returns true if watchdogStart() has been called
    bool watchdogRunning();

}  // namespace kernel

// ISR handlers (called from vector table, extern "C" linkage)
extern "C"
{
    void SysTick_Handler();
}
