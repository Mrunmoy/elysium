# Phase 9: Interactive Shell

## Overview

A kernel-level interactive command-line interface over UART. The shell
runs as a regular kernel thread and provides built-in commands for
inspecting thread state, heap usage, uptime, and system version.

## Design

The shell is hardware-independent: all I/O goes through a function pointer
(`ShellWriteFn`), making it testable on the host without UART hardware.

```
+-------------------+      +-----------------+      +------------+
| UART RX polling   | ---> | shellProcessChar| ---> | Command    |
| (app thread)      |      | (line editing)  |      | dispatch   |
+-------------------+      +-----------------+      +------------+
                                                          |
                            +-----------------+      +----v-------+
                            | shellWrite(fn)  | <--- | cmdPs()    |
                            | (output callback|      | cmdMem()   |
                            |  -> UART TX)    |      | cmdUptime()|
                            +-----------------+      +------------+
```

## API (kernel/inc/kernel/Shell.h)

```cpp
namespace kernel
{
    using ShellWriteFn = void (*)(const char *str);

    struct ShellConfig
    {
        ShellWriteFn writeFn;   // Output function (e.g., UART write)
    };

    void shellInit(const ShellConfig &config);
    void shellProcessChar(char c);   // Feed one character from UART RX
    void shellPrompt();              // Print "ms-os> "
    void shellReset();               // Reset state (for testing)
}
```

## Built-in Commands

| Command | Description | Kernel APIs Used |
|---------|-------------|------------------|
| `help` | List available commands | (none) |
| `ps` | Thread listing (ID, name, state, priority, stack) | `threadGetTcbArray()` |
| `mem` | Heap statistics (total, used, free, peak, allocs) | `heapGetStats()` |
| `uptime` | Ticks and seconds since boot | `tickCount()` |
| `version` | Print "ms-os v0.9.0" | (none) |

## Line Editing

- 80-character line buffer
- Printable characters are echoed and appended
- Backspace (0x08) and DEL (0x7F) erase the last character
- CR (0x0D) or LF (0x0A) execute the command
- Leading whitespace is trimmed before dispatch

## App Integration

The shell thread polls UART RX and feeds characters to the shell:

```cpp
void shellThread(void *)
{
    kernel::ShellConfig config{};
    config.writeFn = shellWrite;
    kernel::shellInit(config);
    kernel::shellPrompt();

    while (true)
    {
        char c;
        if (hal::uartTryGetChar(board::kConsoleUart, &c))
            kernel::shellProcessChar(c);
        else
            kernel::sleep(10);
    }
}
```

The shell thread runs at priority 20 (above idle, below application threads).

## UART RX Support

Added to the HAL layer for shell input:

```cpp
namespace hal
{
    char uartGetChar(UartId id);           // Blocking
    bool uartTryGetChar(UartId id, char *c); // Non-blocking
}
```

- **STM32F4**: Polls RXNE flag (SR bit 5), reads from DR register
- **Zynq7000**: Polls RX FIFO empty flag (SR bit 1), reads from FIFO register

Note: The STM32 USART has no hardware RX FIFO (single-byte data register).
Characters sent faster than the polling rate will cause overrun. Terminal
programs should use a small inter-character delay or flow control.

## Hardware Verification

Verified on STM32F407ZGT6 via J-Link + UART (115200 baud):

```
ms-os> ps
TID  NAME         STATE   PRI  STACK
---  ----------   ------  ---  -----
0    idle         Ready   31   512
1    echo-srv     Block   8    1024
2    echo-cli     Block   10   1024
3    led          Block   15   1024
4    shell        Run     20   1024

ms-os> mem
total:    16376
used:     0
free:     16376
peak:     0
allocs:   0
largest:  16376

ms-os> uptime
92419 ticks (92s)
```

## Test Coverage

24 tests in `test/kernel/ShellTest.cpp`:
- Prompt display
- Character echo and control character filtering
- Enter/newline handling (CR and LF)
- Backspace and DEL (erase, at-start no-op)
- Unknown command error message
- All 5 built-in commands
- Leading whitespace trimming
- Line buffer overflow truncation
- Multiple sequential commands
- Reset clears state

## Files

| File | Purpose |
|------|---------|
| `kernel/inc/kernel/Shell.h` | Shell public API |
| `kernel/src/core/Shell.cpp` | Implementation (~300 lines) |
| `hal/inc/hal/Uart.h` | UART RX declarations |
| `hal/src/stm32f4/Uart.cpp` | STM32 UART RX (RXNE polling) |
| `hal/src/zynq7000/Uart.cpp` | Zynq UART RX (FIFO polling) |
| `app/ipc-demo/main.cpp` | Shell thread integration |
| `test/kernel/ShellTest.cpp` | 24 host tests |
