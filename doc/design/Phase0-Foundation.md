# Phase 0: Project Foundation

## Overview

Phase 0 establishes the project skeleton for ms-os: directory structure, build
system, vendor dependencies, startup code, minimal HAL, and a proof-of-life
blinky application.

## Components

### Nix Development Environment

`flake.nix` provides a reproducible dev shell with all required tools:
gcc-arm-embedded, cmake, ninja, openocd, python3, git, stlink, clang-tools.

`.envrc` enables automatic environment activation via direnv.

### Vendor Dependencies (Git Submodules)

| Submodule | Path | Version |
|-----------|------|---------|
| STM32F4 HAL Driver | `vendor/stm32f4xx-hal-driver` | v1.8.5 |
| CMSIS Device F4 | `vendor/cmsis-device-f4` | v2.6.11 |
| CMSIS 5 (Core) | `vendor/cmsis_core` | 5.9.0 |
| Google Test | `test/vendor/googletest` | v1.14.0 |

### Startup Code (startup/stm32f407vgt6/)

**Linker.ld** -- Memory layout for STM32F407VGT6:
- FLASH: 0x08000000, 1024K
- SRAM: 0x20000000, 128K
- CCM: 0x10000000, 64K

Sections: `.isr_vector`, `.text`, `.rodata`, `.data` (LMA flash, VMA SRAM),
`.bss`, `.heap`, `._stack`.

**Startup.s** -- GNU AS, Cortex-M4 Thumb-2:
- Vector table (initial SP + 15 system exceptions + 82 IRQs)
- Reset_Handler: FPU enable, .data copy, .bss zero, __libc_init_array, SystemInit, main
- Default handlers: weak infinite loops, overridable

**SystemInit.cpp** -- Clock configuration:
- Flash: 5 wait states, prefetch + I/D cache
- HSE: 25 MHz external crystal
- PLL: M=25, N=336, P=2, Q=7 -> 168 MHz SYSCLK
- AHB /1 (168 MHz), APB1 /4 (42 MHz), APB2 /2 (84 MHz)

### HAL Abstraction (hal/)

Register-level C++ wrappers (no dependency on ST HAL library at runtime):

- **Gpio.h/Gpio.cpp** -- GPIO init (mode, speed, pull, AF), set, clear, toggle, read
- **Uart.h/Uart.cpp** -- UART init (baud, 8N1), blocking putchar/write/writeString
- **Rcc.h/Rcc.cpp** -- Peripheral clock enable (GPIO ports, UART peripherals)

### Blinky Application (app/blinky/)

Proof-of-life: initializes GPIO PD12 (LED) and USART2 PA2 (TX), prints
"ms-os alive" to serial, then toggles LED at 500ms using SysTick busy-wait.

### Build System

**CMakeLists.txt** -- Dual-mode:
- Cross-compile (with toolchain file): builds startup, hal, blinky
- Host build (no toolchain file): builds Google Test + unit tests

**build.py** -- Entry point:
- `build.py` -- cross-compile firmware
- `build.py -t` -- host tests
- `build.py -f` -- flash via OpenOCD
- `build.py -c` -- clean

### Test Infrastructure

Host-compiled tests using Google Test v1.14.0 with mock HAL:
- MockGpio.cpp provides link-time HAL substitution
- GpioTest.cpp verifies init parameters, set/clear/toggle/read behavior

## Design Decisions

1. **Register-level HAL** instead of wrapping ST HAL library. The ST HAL is
   available as a vendor submodule for reference and potential future use, but
   our HAL layer directly accesses STM32F4 registers. This keeps the code
   small, avoids HAL initialization ceremony (HAL_Init, SysTick handler), and
   is more appropriate for an RTOS kernel.

2. **Dual CMake build** -- one CMakeLists.txt handles both cross and host
   builds. `CMAKE_CROSSCOMPILING` selects which targets to include.

3. **Link-time mock substitution** for tests -- mock .cpp files provide the
   same symbols as the real HAL, recording calls for verification.

4. **C++17 with extern "C"** for SystemInit -- the startup assembly calls
   SystemInit with C linkage, so SystemInit.cpp uses `extern "C"`.
