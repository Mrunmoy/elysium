# Phase 13: Board-to-Board USART2 Integration Test

## Overview

First board-to-board peripheral test using two identical STM32F407ZGT6 boards
with USART2 TX/RX cross-wired. Validates the existing interrupt-driven UART
driver in a real two-device topology. No new HAL code is required.

## Hardware Setup

```
Board 1 (J-Link)                    Board 2 (CMSIS-DAP)
+-------------------+               +-------------------+
| USART1 TX (PA9)   |---> CP2102    | USART1 TX (PA9)   |---> CMSIS-DAP UART
| Console: ttyUSB0  |               | Console: ttyACM0  |
|                    |               |                    |
| USART2 TX (PA2) --+------+--------+-> USART2 RX (PA3) |
| USART2 RX (PA3) <-+------+--------+-- USART2 TX (PA2) |
|                    |               |                    |
| GND ---------------+--------------+-- GND              |
+-------------------+               +-------------------+
```

- Boards share common ground
- PA2 (AF7) = USART2 TX, PA3 (AF7) = USART2 RX
- USART2 on APB1 (42 MHz on F407), base address 0x40004400, IRQ 38

## Pin Mapping

| Pin | Board 1 Function | Board 2 Function |
|-----|-------------------|-------------------|
| PA2 | USART2 TX (AF7)   | USART2 TX (AF7)   |
| PA3 | USART2 RX (AF7)   | USART2 RX (AF7)   |
| PA9 | USART1 TX (AF7, console) | USART1 TX (AF7, console) |

## Protocol

- Baud rate: 115200 (8N1)
- Board 2 runs a simple echo server: any byte received on USART2 is
  immediately sent back on USART2
- Board 1 runs a test harness: sends test patterns on USART2, waits for
  echoed data, and verifies correctness

## Applications

### uart2-echo (Board 2)

Echo server firmware:
1. Initialize console (USART1) via board config
2. Configure PA2/PA3 as AF7 (USART2)
3. Initialize USART2 at 115200 baud
4. Enable USART2 RX interrupt (ring buffer)
5. Print "UART2 Echo Server ready" on console
6. Main loop: poll ring buffer via `uartTryGetChar(Usart2)`, echo back via
   `uartPutChar(Usart2)`, print periodic activity count on console

### uart2-test (Board 1)

Test runner firmware with 5 tests:

| # | Test | Description | Timeout |
|---|------|-------------|---------|
| 1 | Single byte | Send 0xA5, verify echo | ~100ms |
| 2 | Multi-byte | Send {0xDE, 0xAD, 0xBE, 0xEF}, verify 4-byte echo | ~100ms |
| 3 | Sequential | Send 0x00-0xFF one at a time, verify each echoed back | ~500ms |
| 4 | Burst | Send 16-byte block, collect all echoes, verify match | ~100ms |
| 5 | Stress | Send 64 bytes rapidly, collect echoes, verify match | ~200ms |

Each test:
- Sends test data on USART2
- Polls for echoed response with timeout (busy-wait loop)
- Compares received data with sent data
- Prints PASS/FAIL per test and final summary on USART1 console

## Existing HAL Coverage

No new drivers needed. The following existing APIs are used:

- `hal::uartInit()` -- configure USART2 baud, word length, stop bits
- `hal::uartPutChar()` -- polled TX on USART2
- `hal::uartTryGetChar()` -- non-blocking RX from ring buffer
- `hal::uartRxInterruptEnable()` -- enable ISR-driven RX with 64-byte ring buffer
- `hal::rccEnableUartClock(UartId::Usart2)` -- APB1ENR bit 17
- `hal::rccEnableGpioClock(Port::A)` -- AHB1ENR bit 0
- `hal::gpioInit()` -- AF mode with AF7 for USART2

All of these are already unit-tested in `test/hal/UartTest.cpp`.

## Build System Changes

### `--probe` argument for `build.py`

Add `--probe` argument (choices: `jlink`, `cmsis-dap`, default: `jlink`) to
support flashing Board 2 via its CMSIS-DAP debug probe.

Flash routing:
- `--probe jlink` + STM32: existing `flash_jlink()` (unchanged)
- `--probe cmsis-dap` + STM32: new `flash_openocd_stm32()` using
  `interface/cmsis-dap.cfg` and `target/stm32f4x.cfg`
- PYNQ-Z2: existing `flash_openocd()` (unchanged, ignores `--probe`)

### CMake

Both apps added to the non-PYNQ branch in root `CMakeLists.txt`.

## Expected Console Output

### Board 1 (ttyUSB0) -- uart2-test

```
=== UART2 Board-to-Board Test (Phase 13) ===
Board 1: Test runner on USART2

Single byte (0xA5): PASS
Multi-byte (4 bytes): PASS
Sequential (0x00-0xFF): PASS
Burst (16 bytes): PASS
Stress (64 bytes): PASS

--- Summary: 5/5 passed (ALL PASS) ---
```

### Board 2 (ttyACM0) -- uart2-echo

```
=== UART2 Echo Server (Phase 13) ===
Board 2: Echo on USART2, console on USART1

UART2 Echo Server ready
Echo: 1 bytes
Echo: 4 bytes
Echo: 256 bytes
Echo: 16 bytes
Echo: 64 bytes
```

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Baud rate mismatch | Both boards use identical clock config (168 MHz, APB1=42 MHz) |
| RX ring buffer overflow on burst | 64-byte ring buffer is large enough for 64-byte stress test |
| Echo server not ready when test starts | Test runner waits 500ms after boot before starting tests |
| Ground loop / noise | Short wires, shared ground reference |
