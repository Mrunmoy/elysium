# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ms-os is a greenfield Real-Time Operating System (RTOS) targeting ARM Cortex A/R/M series processors. Initial hardware target is STM32F207ZGT6 (Cortex-M3, 120 MHz, 1 MB FLASH, 128 KB SRAM). The project is in early implementation phase (Phase 0 complete).

**Architecture:** Microkernel design, written in C++17 with Assembly where required. Must support full C++17 applications, multithreading, synchronization, core pinning, IPC, interrupts, heap management, device tree (YAML), and a custom bootloader.

**Key design points:**
- printf/stdout redirected to a configurable serial port
- Applications can register for peripheral interrupts through the OS
- Microkernel: minimal kernel with user-space services (drivers, IPC, process management)
- Register-level HAL (direct register access, not wrapping the ST HAL library)

## Build Commands

Requires Nix dev shell (enter with `nix develop`):

```bash
python3 build.py              # Cross-compile ARM firmware -> build/
python3 build.py -t           # Build + run host unit tests -> build-test/
python3 build.py -c           # Clean both build directories
python3 build.py -f           # Flash firmware via J-Link
python3 build.py -e           # Build examples
```

Run a specific test binary directly:
```bash
./build-test/test/hal/hal_tests                          # Run all HAL tests
./build-test/test/hal/hal_tests --gtest_filter='*Toggle*' # Run one test
```

Run tests via ctest:
```bash
ctest --test-dir build-test --output-on-failure
```

## Dual Build System Architecture

The root `CMakeLists.txt` uses `CMAKE_CROSSCOMPILING` to switch between two completely different build graphs:

**Cross-compile** (`python3 build.py`): Uses `cmake/arm-none-eabi-gcc.cmake` toolchain file. Builds `startup` + `hal` + `app/blinky` into `build/`. Output: ELF, .bin, .hex.

**Host build** (`python3 build.py -t`): No toolchain file, uses native GCC. Builds Google Test + test executables into `build-test/`. Real HAL sources are NOT compiled; mock implementations are linked instead.

This means adding a new HAL module requires changes in three places:
1. `hal/src/stm32f4/` -- real implementation (cross-compile only)
2. `hal/CMakeLists.txt` -- add source to the `hal` library
3. `test/hal/` -- mock implementation + test file + update `test/hal/CMakeLists.txt`

## Testing Strategy: Link-Time Mock Substitution

Tests run on the host machine (x86) by substituting mock implementations at link time:

- **Public API headers** (`hal/inc/hal/*.h`) are shared between real and mock code
- **Real implementations** (`hal/src/stm32f4/Gpio.cpp`) manipulate hardware registers via volatile pointers
- **Mock implementations** (`test/hal/MockGpio.cpp`) provide the same symbols but record calls into global vectors defined in `test/hal/MockRegisters.h`
- **Test files** (`test/hal/GpioTest.cpp`) call the public HAL API, then assert on the recorded mock state

Each test's `SetUp()` calls `test::resetMockState()` to clear recorded calls. When adding a new HAL component, follow this same pattern: write mock that records calls, write tests that verify recorded state.

## Startup Sequence (Cross-Compiled Firmware)

```
Reset_Handler (startup/stm32f207zgt6/Startup.s)
  -> Copy .data from FLASH to SRAM
  -> Zero .bss
  -> __libc_init_array (C++ static constructors)
  -> SystemInit() (startup/stm32f207zgt6/SystemInit.cpp)
       -> Configure PLL: HSE 25MHz, M=25 N=240 P=2 Q=5 -> 120 MHz SYSCLK
       -> Flash: 3 wait states, prefetch + I/D caches
       -> APB1 /4 = 30 MHz, APB2 /2 = 60 MHz
  -> main()
```

**Memory layout** (from `startup/stm32f207zgt6/Linker.ld`):
- FLASH: 0x08000000 (1024K) -- .isr_vector, .text, .rodata, .ARM.exidx
- SRAM: 0x20000000 (128K) -- .data, .bss, .heap (16K), ._stack (4K at top)

SystemInit.cpp uses `extern "C"` for linkage with the assembly Reset_Handler. The global `SystemCoreClock` variable (120000000) is expected by CMSIS headers.

## Reference Implementations

Reference RTOSes at `/home/litu/sandbox/embedded/rtos/`:
- **FreeRTOS** - Minimal portable kernel, extensive device support
- **ThreadX** - Picokernel with preemption-threshold scheduling, SMP support
- **Zephyr** - Modern modular RTOS with device tree support and extensive driver ecosystem

Microkernel reference at `/mnt/usb/minix/`:
- **Minix** - Microkernel OS (same architecture as ms-os). Key areas: `minix/kernel/` (microkernel core), `minix/servers/` (user-space services like PM, VM, IPC, RS), `minix/include/` (system headers), `minix/drivers/` (user-space device drivers). Especially relevant for IPC message passing design, process management, kernel panic handling, and microkernel service separation.

## Sibling Libraries (Foundation Components)

Production-ready C++17 libraries at `/mnt/data/sandbox/cpp/` that serve as building blocks:

- **ms-ringbuffer** - Header-only lock-free SPSC ring buffer, cache-line padded, shared-memory compatible
- **ms-runloop** - Epoll-based event loop with thread-safe callable posting and FD source watching
- **ms-ipc** - IPC framework with IDL code generator (ipcgen), shared memory transport, UDS signaling, zero-copy serialization

## C++ Coding Standards

### Formatting
- `.clang-format` at `/mnt/data/sandbox/cpp/.clang-format`
- Allman brace style (opening brace on next line), 4-space indent, 100-column limit
- Pointer alignment: right (`int *p`), namespace indentation: all

### Naming
- Files: CamelCase (`RingBuffer.h`, `EventDispatcher.cpp`)
- Classes: CamelCase
- Member variables: `m_` prefix (`m_buffer`, `m_isRunning`) for classes
- For structs members: no prefix in members
- Private helpers: lowerCamelCase
- Constants: `k` prefix (`kConstantName`)

### Platform Separation
- **No `#ifdef`/`#ifndef`/`#if defined(...)` in `.cpp` files** -- use directory structure instead
- HAL pattern: `hal/inc/hal/` (public headers), `hal/src/stm32f4/` (platform implementations)
- CMake selects platform files at build time; new platform targets get their own directories (e.g., `cortex-m/`, `stm32f4/`)

### Two Compilation Modes

**Kernel code** (kernel, drivers, HAL):
- Compiled with `-fno-exceptions -fno-rtti -fno-unwind-tables`
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
- Minimal `main()` -- move logic into classes
- Document thread safety expectations in class comments
- RAII throughout, avoid global state
- No raw `new`/`delete` in any code
- C++17 only (not C++20) -- designated initializers (`{.field = val}`) are not available

## Development Workflow

Every new component, module, or feature MUST follow these five steps in strict order. Do NOT stop partway through -- complete ALL five steps before considering the task done.

1. **Design** -- Create or update the design document in `docs/design/` BEFORE writing any code. Include architecture, interfaces, register maps, data flow, edge cases. Get user approval if needed.
2. **TDD** -- Write unit tests first (Google Test). Define the expected behavior and mock state, then make the tests compile (they will fail). Every kernel component, driver, and API must have corresponding tests.
3. **Implement** -- Write the production code to make the tests pass. Follow the approved design.
4. **Build and test on host AND target** -- Run `python3 build.py -t` (host tests pass), cross-compile all targets (`stm32f207zgt6`, `stm32f407zgt6`, `pynq-z2`), flash to hardware, verify on-target behavior via serial output or debugger. Commit and push.
5. **Document** -- Update ALL relevant documentation:
   - Design doc in `docs/design/` (reflect any changes from implementation)
   - `README.md` (features table, test counts, project structure)
   - `docs/the-story-of-ms-os.html` (saga chapter for the new phase)
   - `docs/debugging-playbook.html` (any debug findings, gotchas, resolutions)
   - Commit and push documentation separately.

**CRITICAL:** Do not skip or reorder steps. No step is complete until its artifacts exist. Do not stop after step 3 or step 4 -- always complete through step 5. If blocked on hardware testing (step 4), explicitly tell the user rather than silently skipping it.

## Testing

- Google Test v1.14.0 (as git submodule in `test/vendor/googletest/`)
- Test discovery via CMake `gtest_discover_tests()`
- Python tests for code generators via pytest
- Link-time mock substitution pattern (see "Testing Strategy" section above)

## CI

- GitHub Actions with matrix testing (GCC + Clang, Debug + Release)
