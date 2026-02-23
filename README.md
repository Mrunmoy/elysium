# ms-os

Microkernel Real-Time Operating System targeting ARM Cortex A/R/M processors.

Hardware targets:
- **STM32F207ZGT6** (Cortex-M3, 120 MHz, 1 MB Flash, 128 KB SRAM)
- **STM32F407ZGT6** (Cortex-M4, 168 MHz, 1 MB Flash, 128 KB SRAM)
- **PYNQ-Z2** (Zynq-7020, dual Cortex-A9 @ 650 MHz, 512 MB DDR3)

For the full development story, see [docs/the-story-of-ms-os.html](docs/the-story-of-ms-os.html).

## Features

| Phase | Feature | Status |
|-------|---------|--------|
| 0 | Foundation (startup, HAL, GPIO, UART) | Complete |
| 1 | Kernel core (threads, context switch, SysTick) | Complete |
| 2 | Scheduling (priority bitmap, mutexes, semaphores, sleep) | Complete |
| 3 | Memory management (BlockPool, Heap, MPU) | Complete |
| 4 | Multi-target (Cortex-A9 port, PYNQ-Z2, GIC, crash dump) | Complete |
| 5 | IPC / message passing (Minix-style send/receive/reply, IDL codegen) | Complete |
| 7 | Device tree (FDT binary, runtime parser, DTS source, dt shell command) | Complete |
| 8 | Power management (WFI idle, sleep-on-exit, deep sleep, clock gating) | Complete |
| 9 | Shell (interactive CLI: ps, mem, uptime, version, dt, help) | Complete |

**Test coverage:** 269 C++ host tests, 135 Python generator tests.

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
  boards/
    stm32f207zgt6.dts       Device tree source (clocks, memory, pins)
    stm32f407zgt6.dts       Device tree source
    pynq-z2.dts             Device tree source
    stm32f207zgt6/          board.dtb + BoardDtb.cpp (embedded DTB)
    stm32f407zgt6/          board.dtb + BoardDtb.cpp (embedded DTB)
    pynq-z2/                board.dtb + BoardDtb.cpp (embedded DTB)
  startup/
    stm32f207zgt6/          STM32F207 vector table, linker script, clock init
    stm32f407zgt6/          STM32F407 vector table, linker script, clock init
    pynq-z2/                PYNQ-Z2 startup (ARM mode, GIC, SCU timer)
  hal/
    inc/hal/                HAL abstraction headers (Gpio, Uart, Rcc)
    src/stm32f4/            STM32F2/F4 register-level implementation
    src/zynq7000/           Zynq-7000 register-level implementation
  kernel/
    inc/kernel/             Kernel public headers
      Thread.h              Thread creation and TCB
      Scheduler.h           Priority-bitmap scheduler
      Mutex.h               Recursive mutex with priority inheritance
      Semaphore.h           Counting semaphore
      Heap.h                First-fit heap allocator
      BlockPool.h           Fixed-size block allocator
      Ipc.h                 Message passing (send/receive/reply)
      Shell.h               Interactive command-line shell
      Fdt.h                 FDT (Flattened Device Tree) parser
      BoardConfig.h         Runtime board configuration from DTB
      Arch.h                Architecture abstraction (context switch, power)
      Mpu.h                 Memory protection unit
      CrashDump.h           Fault handler and crash reporting
    src/core/               Core kernel implementation
    src/arch/cortex-m3/     Cortex-M3 context switch, fault handlers, MPU
    src/arch/cortex-m4/     Cortex-M4 context switch, fault handlers, MPU
    src/arch/cortex-a9/     Cortex-A9 context switch, GIC, fault handlers
    src/board/              Per-board crash dump output
  app/
    blinky/                 LED blink + UART proof-of-life
    hello/                  UART hello world (PYNQ-Z2)
    threads/                Multi-thread demo with sleep-based timing
    ipc-demo/               IPC echo server/client + interactive shell
  tools/
    ipcgen/                 IDL code generator (embedded backend)
    fdtlib.py               Python FDT builder (creates DTB binaries)
    build_dtbs.py           Builds DTBs for all boards
    dtb2cpp.py              Converts DTB binary to C++ const byte array
    dtgen/                  Legacy YAML-to-constexpr generator (reference)
  services/
    echo/                   Example Echo service IDL + generated code
  vendor/                   Git submodules (ST HAL, CMSIS, Google Test)
  test/
    hal/                    HAL unit tests (host-compiled with mocks)
    kernel/                 Kernel unit tests (scheduler, mutex, IPC, shell, ...)
  docs/                     Design documents and project story
```

## Architecture

Microkernel design: minimal kernel with user-space services for drivers, IPC,
and process management. Written in C++17 with assembly where required.

### Kernel Subsystems

- **Threads** -- 8 threads max, 32 priority levels, round-robin within priority
- **Scheduler** -- O(1) bitmap-based, preemptive, time-sliced
- **Synchronization** -- Recursive mutexes with priority inheritance, counting semaphores
- **Memory** -- First-fit heap with coalescing, fixed-size block pools, MPU protection
- **IPC** -- Per-thread mailbox (4 slots x 64B), synchronous send/receive/reply, async notifications
- **Power** -- WFI in idle thread, sleep-on-exit, deep sleep mode control, peripheral clock gating
- **Shell** -- Interactive CLI over UART (help, ps, mem, uptime, version, dt)
- **Device tree** -- Standard FDT binaries parsed at runtime (DTS source, DTB binary, kernel parser)

### Shell Commands

Connect to the console UART (115200 baud) and type commands:

```
ms-os> help
commands:
  help    - show this message
  ps      - list threads
  mem     - heap statistics
  uptime  - ticks since boot
  version - show version
  dt      - device tree info

ms-os> ps
TID  NAME         STATE   PRI  STACK
---  ----------   ------  ---  -----
0    idle         Ready   31   512
1    echo-srv     Block   8    1024
2    echo-cli     Block   10   1024
3    led          Block   15   1024
4    shell        Run     20   1024
```

See `CLAUDE.md` for coding standards and `docs/` for design documents.
