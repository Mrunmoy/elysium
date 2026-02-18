# ms-os

Microkernel Real-Time Operating System targeting ARM Cortex-M processors.

Initial hardware target: **STM32F407VGT6** (Cortex-M4, 168 MHz, 1 MB Flash, 192 KB SRAM).

## Prerequisites

Install [Nix](https://nixos.org/download/) and then:

```bash
nix develop
```

This drops you into a shell with everything needed: ARM toolchain
(`arm-none-eabi-gcc`), CMake, Ninja, OpenOCD, Python 3, ST-Link tools,
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
python3 build.py
```

Produces `build/app/blinky/blinky` (ELF), `blinky.bin`, and `blinky.hex`.

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
python3 build.py -f
```

Requires an ST-Link debugger connected via SWD/JTAG.

## Project Structure

```
ms-os/
  CMakeLists.txt          Top-level CMake (cross-build vs host-build)
  build.py                Build script
  flake.nix               Nix development environment
  cmake/
    arm-none-eabi-gcc.cmake   ARM cross-compilation toolchain
  startup/
    stm32f407vgt6/
      Startup.s           Vector table + reset handler
      Linker.ld           Linker script (memory layout)
      SystemInit.cpp      Clock configuration (PLL -> 168 MHz)
  hal/
    inc/hal/              HAL abstraction headers
    src/stm32f4/          STM32F4 register-level implementation
  kernel/
    inc/kernel/           Kernel public headers (future)
    src/                  Kernel implementation (future)
  app/
    blinky/               LED blink + UART proof-of-life
  vendor/                 Git submodules (ST HAL, CMSIS)
  test/
    vendor/googletest/    Google Test v1.14.0
    hal/                  HAL unit tests (host-compiled with mocks)
  doc/design/             Design documents
```

## Architecture

Microkernel design: minimal kernel with user-space services for drivers, IPC,
and process management. Written in C++17 with assembly where required.

See `CLAUDE.md` for coding standards and `doc/design/` for design documents.
