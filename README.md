# ms-os

[![CI](https://github.com/Mrunmoy/elysium/actions/workflows/ci.yml/badge.svg)](https://github.com/Mrunmoy/elysium/actions/workflows/ci.yml)

Microkernel Real-Time Operating System targeting ARM Cortex A/R/M processors.

Hardware targets:
- **STM32F207ZGT6** (Cortex-M3, 120 MHz, 1 MB Flash, 128 KB SRAM)
- **STM32F407ZGT6** (Cortex-M4, 168 MHz, 1 MB Flash, 128 KB SRAM)
- **PYNQ-Z2** (Zynq-7020, dual Cortex-A9 @ 650 MHz, 512 MB DDR3)

For the full development story, see [The Story of ms-os](https://mrunmoy.github.io/elysium/saga/). Also available: [Architecture Guide](https://mrunmoy.github.io/elysium/architecture/) | [Debugging Playbook](https://mrunmoy.github.io/elysium/playbook/).

## Features

| Phase | Feature | Status |
|-------|---------|--------|
| 0 | Foundation (startup, HAL, GPIO, UART) | Complete |
| 1 | Kernel core (threads, context switch, SysTick) | Complete |
| 2 | Scheduling (priority bitmap, mutexes, semaphores, sleep) | Complete |
| 3 | Memory management (BlockPool, Heap, MPU) | Complete |
| 4 | Multi-target (Cortex-A9 port, PYNQ-Z2, GIC, crash dump) | Complete |
| 5 | IPC / message passing (Minix-style send/receive/reply, IDL codegen) | Complete |
| 6 | Syscall interface (SVC dispatch, unprivileged threads) | Complete |
| 7 | Device tree (FDT binary, runtime parser, DTS source, dt shell command) | Complete |
| 8 | Power management (WFI idle, sleep-on-exit, deep sleep, clock gating) | Complete |
| 9 | Shell (interactive CLI: ps, mem, uptime, version, dt, help) | Complete |
| 10 | Hardware watchdog (IWDG driver, idle-thread feed, shell wdt command) | Complete |
| 11 | Dynamic threads (TCB slot reuse, destroyThread, lifecycle cleanup) | Complete |
| 12 | SPI / I2C / DMA drivers (register-level, polled + async, loopback demo) | Complete |
| 13 | Board-to-board USART2 integration test (echo server + test runner) | Complete |
| 14 | Board-to-board SPI1 integration test (slave echo + master test runner) | Complete |
| 15 | Board-to-board I2C1 integration test (slave echo + master test runner + BME680) | Complete |
| 16 | Global error-code unification (`msos::error`) across kernel/HAL layers | Complete |
| 17 | Hardware driver validation runner (machine-parseable UART/SPI/I2C/DMA smoke tests) | Complete |
| 18 | DMA hardware coverage expansion (width/alignment/increment/repeatability matrix) | Complete |
| 19 | SPI DMA transfer integration (`spiTransferDma`) with host + board-to-board validation | Complete |
| 20 | Hardware timers (TIM2-TIM7, periodic interrupt, PWM, microsecond delay) | Complete |
| 21 | RNG hardware random number generator (TRNG driver, 4/4 hardware tests) | Complete |
| 22 | RTC (real-time clock, BCD calendar, alarm callback, LSI/LSE) | Complete |
| 23 | ADC single-shot HAL (`adcInit` + polled `adcRead`) with host + on-target validation | Complete |

**Test coverage:** 538 C++ host tests, 145 Python tests.

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
python3 build.py -f --probe cmsis-dap         # STM32 via CMSIS-DAP
python3 build.py -f --probe stlink            # STM32 via ST-Link V2
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
    inc/hal/                HAL abstraction headers (Gpio, Uart, Rcc, Watchdog, Dma, Spi, I2c, Timer, Rng, Rtc, Adc)
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
    spi-demo/               SPI loopback test (polled + async)
    uart2-echo/             UART2 echo server (board-to-board, Board 2)
    uart2-test/             UART2 test runner (board-to-board, Board 1)
    spi2-slave/             SPI1 slave echo server (board-to-board, Board 2)
    spi2-test/              SPI1 master test runner (board-to-board, Board 1)
    i2c-slave/              I2C1 slave echo server (board-to-board, Board 2)
    i2c-test/               I2C1 master test runner (board-to-board, Board 1)
    dma-test/               DMA2 hardware smoke test runner
    adc-test/               ADC1 internal-channel test runner (VREFINT + temp sensor)
    timer-test/             Timer hardware test (TIM6/TIM7/TIM3/TIM5)
    rng-demo/               RNG hardware random number test
    rtc-test/               RTC hardware test (init, time, date, alarm)
  tools/
    hw_driver_runner.py     Host runner: flash boards + parse machine test output
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
    tools/                  Host-side Python tests for validation tooling
  docs/                     Design documents and project story
```

## Architecture

Microkernel design: minimal kernel with user-space services for drivers, IPC,
and process management. Written in C++17 with assembly where required.

### Kernel Subsystems

- **Threads** -- 8 threads max, 32 priority levels, round-robin within priority, dynamic create/destroy with slot reuse
- **Scheduler** -- O(1) bitmap-based, preemptive, time-sliced
- **Synchronization** -- Recursive mutexes with priority inheritance, counting semaphores
- **Memory** -- First-fit heap with coalescing, fixed-size block pools, MPU protection
- **IPC** -- Per-thread mailbox (4 slots x 64B), synchronous send/receive/reply, async notifications
- **Power** -- WFI in idle thread, sleep-on-exit, deep sleep mode control, peripheral clock gating
- **Watchdog** -- IWDG hardware watchdog, idle-thread feeding, automatic MCU reset on thread starvation
- **DMA** -- Dual-controller DMA with 8 streams each, interrupt callbacks, configurable data sizes and priorities
- **SPI** -- Full-duplex SPI master and slave with polled, async, interrupt-driven, and DMA transfers
- **I2C** -- I2C master and slave (standard/fast mode), polled and async, interrupt-driven slave with RX/TX callbacks
- **ADC** -- ADC1/2/3 single-shot conversions with configurable resolution/alignment/sample time
- **Shell** -- Interactive CLI over UART (help, ps, mem, uptime, version, dt, wdt)
- **Device tree** -- Standard FDT binaries parsed at runtime (DTS source, DTB binary, kernel parser)

### Global Error Codes

Global error semantics are centralized in `common/inc/msos/ErrorCode.h`.

- `0` means success (`msos::error::kOk`)
- Negative values are failures (Linux `errno`-style)
- Status-returning APIs should use `std::int32_t` and return these shared codes
- Legacy bool/ID APIs use migration helpers:
  - `msos::error::boolToStatus(...)`
  - `msos::error::handleToStatus(...)`

Design details and rollout plan: `docs/design/phase-16-global-error-codes.md`.

### Hardware Driver Validation Runner

`tools/hw_driver_runner.py` automates board-to-board validation for STM32F407.

- Flashes Board 2 service firmware, then Board 1 test firmware
- Captures serial output from Board 1
- Parses machine lines:
  - `MSOS_CASE:<driver>:<case-name>:PASS|FAIL`
  - `MSOS_SUMMARY:<driver>:pass=<n>:total=<m>:result=PASS|FAIL`
- Fails with non-zero exit code if expected output is missing or inconsistent

Example usage:

```bash
python3 tools/hw_driver_runner.py \
  --target stm32f407zgt6 \
  --drivers uart,spi,i2c,dma \
  --board1-port /dev/ttyUSB0 \
  --board1-probe jlink \
  --board2-probe stlink
```

Enable raw log artifact capture:

```bash
python3 tools/hw_driver_runner.py --register-trace
```

DMA matrix coverage details are documented in
`docs/design/phase-18-dma-hw-coverage.md`.

ADC single-shot driver details are documented in
`docs/design/phase-20-adc-single-shot.md`.

On-target ADC validation app (`app/adc-test`) emits:

- `MSOS_CASE:adc:<case-name>:PASS|FAIL`
- `MSOS_SUMMARY:adc:pass=<n>:total=<m>:result=PASS|FAIL`

Required board-to-board wiring for these scenarios:

- UART: `PA2 (TX) <-> PA3 (RX)` cross-connected between boards + shared GND
- SPI: `PA5(SCK)`, `PA6(MISO)`, `PA7(MOSI)` + shared GND
- I2C: `PB6(SCL)`, `PB7(SDA)` + shared GND

Troubleshooting note:

- If UART case lines do not progress beyond startup banner, verify USART2 cross-wiring first.
  Missing UART jumpers reproduces deterministic timeout failures in all UART cases.

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
  wdt     - watchdog status

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
