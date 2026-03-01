# Phase 14: SPI1 Board-to-Board Test

## Overview

Extend board-to-board testing from UART (Phase 13) to SPI. Two STM32F407ZGT6 boards
with SPI1 pins cross-wired validate the SPI slave driver in a real master-slave
configuration. This phase adds SPI slave mode support to the HAL: interrupt-driven
RX callback, TX pre-load, and SSI bit fix for slave mode.

## Hardware Setup

Two STM32F407ZGT6 boards connected via SPI1:

```
Board 1 (J-Link)                     Board 2 (CMSIS-DAP)
+---------------------+              +---------------------+
| USART1 TX (PA9)     |---> CP2102   | USART1 TX (PA9)     |---> CMSIS-DAP UART
| Console: ttyUSB0    |              | Console: ttyACM0    |
|                      |              |                      |
| SPI1 SCK  (PA5) ----+------+-------+---- SPI1 SCK  (PA5) |
| SPI1 MISO (PA6) ----+------+-------+---- SPI1 MISO (PA6) |
| SPI1 MOSI (PA7) ----+------+-------+---- SPI1 MOSI (PA7) |
|                      |              |                      |
| LED: PC13            |              | LED: PC13            |
| GND -----------------+--------------+-- GND                |
+---------------------+              +---------------------+
```

| Signal  | Board 1 (Master, J-Link)  | Board 2 (Slave, CMSIS-DAP) |
|---------|---------------------------|----------------------------|
| SCK     | PA5 (AF5)                 | PA5 (AF5)                  |
| MISO    | PA6 (AF5)                 | PA6 (AF5)                  |
| MOSI    | PA7 (AF5)                 | PA7 (AF5)                  |
| NSS     | Software (SSI=1)          | Software (SSI=0)           |
| Console | USART1 PA9 (/dev/ttyUSB0) | USART1 PA9 (/dev/ttyACM0)  |
| LED     | PC13 (active low)         | PC13 (active low)          |

## SPI Slave Mode -- New HAL Features

The SPI driver from Phase 12 was master-only. Phase 14 adds three new functions:

### API Additions (`hal/inc/hal/Spi.h`)

```cpp
// Slave RX callback: called in ISR context with each received byte
using SpiSlaveRxCallbackFn = void (*)(void *arg, std::uint8_t rxByte);

// Enable SPI slave RXNE interrupt. Callback fires for each received byte.
// Also enables SPE (deferred from spiInit for slave mode).
void spiSlaveRxInterruptEnable(SpiId id, SpiSlaveRxCallbackFn callback, void *arg);

// Disable SPI slave RXNE interrupt.
void spiSlaveRxInterruptDisable(SpiId id);

// Pre-load a byte into DR for the slave's next TX (response to master).
void spiSlaveSetTxByte(SpiId id, std::uint8_t value);
```

### spiInit() SSI Bit Fix

The original `spiInit()` set SSI=1 whenever softwareNss=true. This is wrong for
slave mode: SSI=1 means "not selected" and the slave ignores all bus traffic.

Fixed behavior:
- `master=true, softwareNss=true`: SSI=1 (prevents MODF fault)
- `master=false, softwareNss=true`: SSI=0 (slave selected)

### SPE Deferral for Slave Mode

`spiInit()` enables SPE immediately for master mode but defers it for slave mode.
This allows the application to call `spiSlaveRxInterruptEnable()` (which enables SPE)
before calling `spiSlaveSetTxByte()` (which writes DR). Per RM0090, DR writes may not
reach the TX buffer while SPE=0.

Init sequence for slave:
1. `spiInit(config)` -- configures CR1/CR2 but does NOT set SPE
2. `spiSlaveRxInterruptEnable(id, callback, arg)` -- sets SPE, enables RXNEIE
3. `spiSlaveSetTxByte(id, 0x00)` -- pre-loads initial response byte

### ISR Handler Extension

`handleSpiIrq()` now checks for slave RX after the existing async master path:

```
handleSpiIrq(idx):
  if s_spiState[idx].active:       // async master transfer
    // existing code unchanged
    return
  if s_spiSlaveState[idx].active:  // slave RX interrupt
    if RXNE set:
      read byte from DR
      invoke callback(arg, rxByte)
```

### NVIC Enable

Added NVIC helper functions (same pattern as Uart.cpp). SPI IRQ numbers:
SPI1=35, SPI2=36, SPI3=51. Priority set to 0x80 for both async and slave paths.

## SPI Echo Protocol

SPI is full-duplex: master and slave exchange bytes simultaneously. The slave can only
respond with data that was pre-loaded into DR before the master starts clocking. This
creates a 1-byte offset echo protocol:

```
Master TX:  [PRIME] [B0] [B1] [B2] [DUMMY]
Slave  RX:          [B0] [B1] [B2] [DUMMY]  (slave receives & pre-loads echo)
Slave  TX:  [0x00]  [PRIME] [B0] [B1] [B2]  (echo of previous byte)
Master RX:  [junk]  [PRIME] [B0] [B1] [B2]  (master verifies these)
```

- **PRIME byte:** master sends a known byte first; slave's echo callback pre-loads it
- **Data bytes:** each byte the slave receives is pre-loaded as the next response
- **DUMMY byte:** master sends 0x00 to clock out the slave's echo of the last data byte
- **Verification:** rx[i] should equal tx[i-1] for i>0, and rxLast should equal tx[N-1]
- **Inter-byte delay:** 1000-cycle busy-wait between transfers lets slave ISR process

## Test Descriptions

| # | Test | Description |
|---|------|-------------|
| 1 | Single byte echo | Prime(0xA5) + dummy, verify echo of 0xA5 |
| 2 | Multi-byte echo (4 bytes) | Prime + {0xDE,0xAD,0xBE,0xEF} + dummy, verify 4-byte echo |
| 3 | Sequential echo (0x00-0xFF) | Prime(0x00) + 0x01-0xFF + dummy, verify 256-byte echo |
| 4 | Burst echo (16 bytes) | Prime + 16 varied bytes + dummy, verify 16-byte echo |
| 5 | Stress echo (64 bytes) | Prime + 64 XOR-pattern bytes + dummy, verify 64-byte echo |

## Applications

### spi2-slave (Board 2, CMSIS-DAP)

SPI1 slave echo server:
1. Init console (USART1) via board config
2. Init LED on PC13 (active low)
3. Configure SPI1 GPIO: PA5 (SCK, AF5), PA6 (MISO, AF5), PA7 (MOSI, AF5)
4. Configure SPI1: slave, software NSS, Mode0, 8-bit
5. Enable slave RX interrupt (also enables SPE)
6. Pre-load DR with 0x00
7. Echo callback: `spiSlaveSetTxByte(rxByte)`, toggles LED
8. Main loop: clears stale debug state (FPB, DWT, DEMCR), then WFI

### spi2-test (Board 1, J-Link)

SPI1 master test runner:
1. Init console (USART1) via board config
2. Init LED on PC13 (active low)
3. Configure SPI1 GPIO: PA5 (SCK, AF5), PA6 (MISO, AF5), PA7 (MOSI, AF5)
4. Configure SPI1: master, software NSS, Mode0, 8-bit, prescaler Div256
5. Wait 500ms for slave to be ready
6. Run 5 tests using polled `spiTransferByte()`
7. Print per-test PASS/FAIL and summary on USART1 console

SPI clock: APB2 (84 MHz) / 256 = 328 kHz. Slow enough for slave ISR processing.

## Expected Console Output

### Board 1 (ttyUSB0) -- spi2-test

```
=== SPI1 Board-to-Board Test (Phase 14) ===
Board 1: SPI1 master test runner, LED on PC13
Pins: PA5(SCK) PA6(MISO) PA7(MOSI)

Waiting for slave...
Single byte echo: PASS
Multi-byte echo (4 bytes): PASS
Sequential echo (0x00-0xFF): PASS
Burst echo (16 bytes): PASS
Stress echo (64 bytes): PASS

--- Summary: 5/5 passed (ALL PASS) ---
```

## Host Test Coverage

8 new SPI slave tests in `test/hal/SpiTest.cpp`:

| Test | Verifies |
|------|----------|
| InitSlaveSoftwareNssClearsSsi | master=false + softwareNss -> SSI=0 |
| InitMasterSoftwareNssSetsSsi | master=true + softwareNss -> SSI=1 (existing behavior) |
| SlaveRxInterruptEnableRecordsCall | Records SPI ID, callback, arg |
| SlaveRxInterruptDisableRecordsCall | Records SPI ID |
| SlaveSetTxByteRecordsCall | Records SPI ID and byte value |
| SlaveRxEnableThenDisableClearsState | Enable then disable clears active flag |
| SlaveRxCallbackInvokedWithByte | Mock simulates RX and invokes callback with byte |
| SlaveSetTxByteRecordsMultipleCalls | Multiple calls record each byte |

## Test Results

| Scenario | Result |
|----------|--------|
| Flash slave + flash master | **5/5 PASS** |
| Master-only reset (slave untouched) | **5/5 PASS** |
| After OpenOCD connect/disconnect | 0/5 FAIL (known limitation) |

## Known Limitation: OpenOCD Debug Adapter Interaction

Connecting to the slave board via OpenOCD (even `init; shutdown` with no reset)
permanently breaks SPI1 until the board is reflashed. After OpenOCD connects:

- CPU continues running (not halted)
- All registers read correctly (SPI CR1/CR2, GPIO, NVIC, DEMCR, PRIMASK)
- RXNE never fires (neither ISR nor polled mode)
- Clearing FPB, DWT, DEMCR, and C_DEBUGEN does not restore functionality

**Root cause:** Unknown. Suspected hardware-level interaction between the CMSIS-DAP
adapter's SWD connection and the SPI peripheral. All software-visible debug registers
appear correct after clearing.

**Mitigation:** The slave's main loop aggressively clears debug state (DHCSR C_DEBUGEN,
DEMCR TRCENA/MON_EN, FPB, DWT comparators) as a best-effort measure. The FPB/DEMCR
clearing in SystemInit() prevents DebugMon exceptions during boot.

**Workaround:** Always use flash-based workflow. After flashing, do not connect
OpenOCD to the slave board. The master board can be reset freely.

## SystemInit Debug State Clearing

Added to both F407 and F207 SystemInit() (before clock configuration):

```cpp
// Disable FPB (FP_CTRL: KEY=1, ENABLE=0)
reg(0xE0002000) = (1U << 1);
// Clear MON_EN in DEMCR
reg(0xE000EDFC) &= ~(1U << 16);
```

This prevents DebugMon exceptions caused by stale hardware breakpoints left by
a prior debug session. FPB comparators and DEMCR.MON_EN persist across reset.

## Files Created/Modified

| File | Action |
|------|--------|
| `docs/design/phase-14-spi-board-to-board.md` | Created |
| `hal/inc/hal/Spi.h` | Modified (3 functions + callback type) |
| `hal/src/stm32f4/Spi.cpp` | Modified (SSI fix, slave RX, NVIC, ISR update, SPE deferral) |
| `hal/src/zynq7000/Spi.cpp` | Modified (3 no-op stubs) |
| `test/hal/SpiTest.cpp` | Modified (8 new slave tests) |
| `test/hal/MockSpi.cpp` | Modified (3 mock functions) |
| `test/hal/MockRegisters.h` | Modified (slave mock state) |
| `app/spi2-slave/main.cpp` | Created |
| `app/spi2-slave/CMakeLists.txt` | Created |
| `app/spi2-test/main.cpp` | Created |
| `app/spi2-test/CMakeLists.txt` | Created |
| `CMakeLists.txt` | Modified (add subdirectories, bump to 0.14.0) |
| `startup/stm32f407zgt6/SystemInit.cpp` | Modified (FPB+DEMCR clearing) |
| `startup/stm32f207zgt6/SystemInit.cpp` | Modified (FPB+DEMCR clearing) |
