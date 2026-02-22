# ms-os

Microkernel Real-Time Operating System targeting ARM Cortex A/R/M processors.

Hardware targets:
- **STM32F207ZGT6** (Cortex-M3, 120 MHz, 1 MB Flash, 128 KB SRAM)
- **STM32F407ZGT6** (Cortex-M4, 168 MHz, 1 MB Flash, 128 KB SRAM)
- **PYNQ-Z2** (Zynq-7020, dual Cortex-A9 @ 650 MHz, 512 MB DDR3)

For the full development story, see [docs/the-story-of-ms-os.html](docs/the-story-of-ms-os.html).

## Prerequisites

Install [Nix](https://nixos.org/download/) and then:

```bash
nix develop
```

This drops you into a shell with everything needed: ARM toolchain
(`arm-none-eabi-gcc`), CMake, Ninja, J-Link tools, Python 3,
and clang-format. Nothing else to install.

If you use [direnv](https://direnv.net/), the `.envrc` activates the
environment automatically when you enter the directory.

<details>
<summary>Alternative: manual install (without Nix)</summary>

On Ubuntu/Debian:

```bash
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build python3 openocd
```

</details>

## Building

### Cross-compile firmware (ARM)

```bash
python3 build.py                              # STM32F207 (default)
python3 build.py --target stm32f407zgt6       # STM32F407
python3 build.py --target pynq-z2             # PYNQ-Z2 (Cortex-A9)
```

### Clean build

```bash
python3 build.py -c
```

### Host-side unit tests

```bash
python3 build.py -t
```

Runs tests via ctest. No ARM toolchain required.

### Flash to target

```bash
python3 build.py -f                           # STM32F207 via J-Link
python3 build.py -f --target stm32f407zgt6    # STM32F407 via J-Link
python3 build.py -f --target pynq-z2          # PYNQ-Z2 via OpenOCD
```

## Project Structure

```
ms-os/
  CMakeLists.txt            Top-level CMake (cross-build vs host-build)
  build.py                  Build script (cross-compile, test, flash)
  flake.nix                 Nix development environment
  cmake/
    arm-none-eabi-gcc.cmake ARM cross-compilation toolchain
  startup/
    stm32f207zgt6/          STM32F207 vector table, linker script, clock init
    stm32f407zgt6/          STM32F407 vector table, linker script, clock init
    pynq-z2/                PYNQ-Z2 startup (ARM mode, GIC, SCU timer)
  hal/
    inc/hal/                HAL abstraction headers (Gpio, Uart)
    src/stm32f4/            STM32F2/F4 register-level implementation
    src/zynq7000/           Zynq-7000 register-level implementation
  kernel/
    inc/kernel/             Kernel public headers (Thread, Scheduler, Mutex, ...)
    src/core/               Core kernel (scheduler, sync primitives, memory)
    src/arch/cortex-m3/     Cortex-M3 context switch, fault handlers, MPU
    src/arch/cortex-m4/     Cortex-M4 context switch, fault handlers, MPU
    src/arch/cortex-a9/     Cortex-A9 context switch, GIC, fault handlers
    src/board/              Per-board crash dump output
  app/
    blinky/                 LED blink + UART proof-of-life (STM32)
    hello/                  UART hello world (PYNQ-Z2)
    threads/                Multi-thread demo with sleep-based timing
  tools/
    ipcgen/                 IDL code generator (embedded backend)
  vendor/                   Git submodules (ST HAL, CMSIS, Google Test)
  test/
    hal/                    HAL unit tests (host-compiled with mocks)
    kernel/                 Kernel unit tests (scheduler, mutex, semaphore, ...)
  docs/                     Design documents and project story
```

## Architecture

Microkernel design: minimal kernel with user-space services for drivers, IPC,
and process management. Written in C++17 with assembly where required.

See `CLAUDE.md` for coding standards and `docs/` for design documents.
