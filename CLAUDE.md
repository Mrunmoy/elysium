# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ms-os is a greenfield Real-Time Operating System (RTOS) targeting ARM Cortex A/R/M series processors. Initial hardware target is STM32F407VGt6. The project is in early design/implementation phase.

**Architecture:** Microkernel design, written in C++17 with Assembly where required. Must support full C++17 applications, multithreading, synchronization, core pinning, IPC, interrupts, heap management, device tree (YAML), and a custom bootloader.

**Key design points:**
- printf/stdout redirected to a configurable serial port
- Applications can register for peripheral interrupts through the OS
- Microkernel: minimal kernel with user-space services (drivers, IPC, process management)

## Build System

- **CMake 3.14+** with **Ninja** generator
- **Arm GNU Toolchain** for cross-compilation
- **Python 3** build scripts following the convention from sibling libraries:

```bash
python3 build.py              # Build only
python3 build.py -c           # Clean build
python3 build.py -t           # Build + run tests
python3 build.py -e           # Build + examples
python3 build.py -c -t -e     # Clean + tests + examples
```

Tests run via ctest:
```bash
ctest --test-dir build --output-on-failure
```

## Reference Implementations

Three reference RTOSes at `/home/litu/sandbox/embedded/rtos/`:
- **FreeRTOS** - Minimal portable kernel, extensive device support
- **ThreadX** - Picokernel with preemption-threshold scheduling, SMP support
- **Zephyr** - Modern modular RTOS with device tree support and extensive driver ecosystem

Microkernel reference at `/mnt/usb/minix/`:
- **Minix** - Microkernel OS (same architecture as ms-os). Key areas: `minix/kernel/` (microkernel core), `minix/servers/` (user-space services like PM, VM, IPC, RS), `minix/include/` (system headers), `minix/drivers/` (user-space device drivers). Especially relevant for IPC message passing design, process management, kernel panic handling, and microkernel service separation.

## Sibling Libraries (Foundation Components)

These production-ready C++17 libraries at `/mnt/data/sandbox/cpp/` serve as building blocks:

- **ms-ringbuffer** - Header-only lock-free SPSC ring buffer, cache-line padded, shared-memory compatible
- **ms-runloop** - Epoll-based event loop with thread-safe callable posting and FD source watching
- **ms-ipc** - IPC framework with IDL code generator (ipcgen), shared memory transport, UDS signaling, zero-copy serialization. Uses ms-ringbuffer and ms-runloop as dependencies

## C++ Coding Standards

### Formatting
- `.clang-format` at `/mnt/data/sandbox/cpp/.clang-format`
- Allman brace style (opening brace on next line), 4-space indent, 100-column limit
- Pointer alignment: right (`int *p`), namespace indentation: all

### Naming
- Files: CamelCase (`RingBuffer.h`, `EventDispatcher.cpp`)
- Classes: CamelCase
- Member variables: `m_` prefix (`m_buffer`, `m_isRunning`)
- Private helpers: lowerCamelCase
- Constants: `k` prefix (`kConstantName`)

### Platform Separation
- **No `#ifdef`/`#ifndef`/`#if defined(...)` in `.cpp` files** - use directory structure instead
- For sibling libraries: `include/{core,platform/{linux,macos,windows}}/`, `src/{core,platform/{linux,macos,windows}}/`
- For ms-os kernel: platform directories should reflect ARM targets (e.g., `stm32f4/`, `cortex-m/`)
- CMake selects platform files at build time

### Two Compilation Modes

**Kernel code** (kernel, drivers, HAL):
- Compiled with `-fno-exceptions -fno-rtti`
- No dynamic allocation (prefer static allocation, `std::array`, `std::optional`)
- No exceptions, no RTTI
- Deterministic behavior only

**User application code:**
- Full C++17 support including exceptions, RTTI, dynamic allocation
- The kernel must provide runtime infrastructure for C++ exceptions (stack unwinding, personality routines, etc.)
- `std::unique_ptr`, `std::vector`, heap allocation all permitted

### Kernel Panic and Crash Dumps
- Kernel panic mechanism that halts the system on unrecoverable faults
- On application crash, print a crash dump to the serial console including: register state, faulting address, stack trace, thread ID/name, and fault type (e.g. HardFault, MemManage, BusFault, UsageFault)
- Crash dump output goes through the serial port (same path as printf/stdout)

### Style
- Early returns to reduce nesting
- No emojis or unicode in code, comments, or markdown
- Minimal `main()` - move logic into classes
- Document thread safety expectations in class comments
- RAII throughout, avoid global state
- No raw `new`/`delete` in any code

## Development Workflow

Every new component, module, or feature MUST follow these four phases in order:

1. **Design** -- Produce a high-level design (architecture, interfaces, data flow) and a low-level detailed design (data structures, algorithms, edge cases) before writing any code.
2. **Implement** -- Write the code according to the approved design.
3. **Test** -- Write unit tests with Google Test that cover the implementation. Every kernel component, driver, and API must have corresponding tests.
4. **Document** -- Update or create the README and both high-level and low-level detailed design docs to reflect what was built.

Do not skip or reorder phases. No phase is complete until its artifacts exist.

## Testing

- Google Test v1.14.0 (as git submodule in `test/vendor/googletest/`)
- Test discovery via CMake `gtest_discover_tests()`
- Python tests for code generators via pytest

## CI

- GitHub Actions with matrix testing (GCC + Clang, Debug + Release)
