// Kernel shell: interactive command-line interface over UART.
//
// The shell runs as a kernel thread and provides built-in commands
// for inspecting thread state, heap usage, uptime, and IPC mailboxes.
//
// Usage:
//   1. Call shellInit() with an output function (e.g., uartWriteString wrapper)
//   2. Call shellProcessChar() for each received character (from UART polling)
//   3. Call shellPrompt() once before the first character to display the prompt
//
// The shell does not own the UART or any I/O hardware -- it communicates
// through function pointers, making it testable on the host without hardware.

#pragma once

#include <cstdint>

namespace kernel
{
    // Function type for shell output (writes a null-terminated string)
    using ShellWriteFn = void (*)(const char *str);

    struct ShellConfig
    {
        ShellWriteFn writeFn;
    };

    // Initialize the shell with an output function.
    void shellInit(const ShellConfig &config);

    // Process a single input character.
    // Handles line editing (backspace, enter) and command dispatch.
    void shellProcessChar(char c);

    // Print the shell prompt.
    void shellPrompt();

    // Reset shell state (for testing).
    void shellReset();

}  // namespace kernel
