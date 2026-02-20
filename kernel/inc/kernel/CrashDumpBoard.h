// Board-specific crash dump output functions.
//
// Each board provides implementations for low-level output (UART) and
// visual fault indication (LED blink). These are called from the
// portable CrashDumpCommon.cpp and must be safe in any fault context:
// no heap, no exceptions, no RTOS calls, no interrupts.

#pragma once

namespace kernel
{
    // Initialize output device if not already enabled.
    // Called at the start of faultHandlerC to ensure the UART is ready.
    void boardEnsureOutput();

    // Blocking single-character output (polling, no interrupts).
    void boardFaultPutChar(char c);

    // Wait for transmission complete (drain TX shift register).
    void boardFaultFlush();

    // Blink LED indefinitely to indicate fault. Does not return.
    [[noreturn]] void boardFaultBlink();

}  // namespace kernel
