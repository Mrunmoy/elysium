# Phase 15: Board-to-Board I2C1 Integration Test

## Overview

Extends the board-to-board test infrastructure (Phases 13-14: UART2, SPI1) to I2C1.
Adds I2C slave support to the HAL, then validates with an echo server + test runner
across two physical STM32F407ZGT6 boards.

## Hardware Setup

```
Board 1 (J-Link)                Board 2 (ST-Link V2)
  PB6 (SCL) --------+-------- PB6 (SCL)
  PB7 (SDA) --------+-------- PB7 (SDA)
  GND ----------------------- GND
```

- I2C1 on both boards: PB6 = SCL, PB7 = SDA (AF4)
- GPIO: open-drain output, internal pull-ups (can add 4.7K external if unreliable)
- Standard mode: 100 kHz
- Board 1 console: /dev/ttyUSB0 (CP2102, 115200)
- Board 2 console: /dev/ttyACM0 (CMSIS-DAP UART, 115200)

## I2C Slave HAL API

### New Types

```cpp
// Called in ISR context when master write completes (STOP detected).
using I2cSlaveRxCallbackFn = void (*)(void *arg, const std::uint8_t *data, std::size_t length);

// Called in ISR context when master requests a read (ADDR match with R/W=1).
// Fill data buffer, set *length. maxLength is buffer capacity.
using I2cSlaveTxCallbackFn = void (*)(void *arg, std::uint8_t *data, std::size_t *length,
                                       std::size_t maxLength);
```

### New Functions

```cpp
// Init I2C in slave mode with 7-bit own address.
void i2cSlaveInit(I2cId id, std::uint8_t ownAddr,
                  I2cSlaveRxCallbackFn rxCallback,
                  I2cSlaveTxCallbackFn txCallback, void *arg);

// Enable slave event/error/buffer interrupts and NVIC.
void i2cSlaveEnable(I2cId id);

// Disable slave interrupts.
void i2cSlaveDisable(I2cId id);
```

### Design Rationale

I2C is transaction-based (START/STOP delimited), unlike SPI's per-byte model:
- **rxCallback**: delivers complete received messages on STOP detection
- **txCallback**: called on ADDR match with read bit, letting app fill a response buffer

## STM32F4 Implementation

### Slave State

```cpp
constexpr std::size_t kSlaveBufferSize = 256;

struct I2cSlaveState
{
    I2cSlaveRxCallbackFn rxCallback = nullptr;
    I2cSlaveTxCallbackFn txCallback = nullptr;
    void *arg = nullptr;
    std::uint8_t ownAddr = 0;
    bool active = false;

    std::uint8_t rxBuf[kSlaveBufferSize];
    std::size_t rxIndex = 0;

    std::uint8_t txBuf[kSlaveBufferSize];
    std::size_t txLength = 0;
    std::size_t txIndex = 0;

    bool isTx = false;
};
```

### Register Details

- **OAR1** (offset 0x08): bits [7:1] = 7-bit address, bit [14] = 1 (required by RM0090)
- **SR1.ADDR** (bit 1): address matched -- clear by reading SR1 then SR2
- **SR1.STOPF** (bit 4): STOP detected -- clear by reading SR1, then writing CR1 with PE=1
- **SR1.AF** (bit 10): acknowledge failure -- normal in slave TX mode (master NACK = done)
- **SR2.TRA** (bit 2): 0 = receiver (master write), 1 = transmitter (master read)

### ISR State Machine

```
ADDR set:
  Read SR1+SR2 (clears ADDR)
  if SR2.TRA=0: slave receiving, rxIndex=0
  if SR2.TRA=1: slave transmitting, call txCallback, txIndex=0

RXNE set (slave receiving):
  rxBuf[rxIndex++] = DR (clamp at kSlaveBufferSize)

TXE set (slave transmitting):
  DR = txBuf[txIndex++] or 0xFF if exhausted

STOPF set (end of master write):
  Clear: read SR1, write CR1 with PE=1
  Call rxCallback(arg, rxBuf, rxIndex)
  Reset rxIndex

AF in error ISR (slave TX mode):
  Normal: master NACK = done reading
  Clear AF flag, reset TX state, return (not an error)
```

### NVIC IRQ Numbers

| I2C | EV IRQ | ER IRQ |
|-----|--------|--------|
| I2C1 | 31 | 32 |
| I2C2 | 33 | 34 |
| I2C3 | 72 | 73 |

## Echo Protocol

1. Master writes N bytes to slave address 0x44
2. Slave rxCallback stores received data in echo buffer
3. Master reads N bytes from slave address 0x44
4. Slave txCallback copies echo buffer into TX response
5. Master verifies: received data matches sent data

## Hardware Tests

| # | Test | Description |
|---|------|-------------|
| 1 | Single byte | Write {0xA5}, read 1 byte, verify |
| 2 | Multi-byte | Write {0xDE,0xAD,0xBE,0xEF}, read 4, verify |
| 3 | Sequential | Write 0x00-0xFF (256 bytes), read 256, verify |
| 4 | Burst | Write 16 varied bytes, read 16, verify |
| 5 | Stress | Write 64 XOR-pattern bytes, read 64, verify |
| 6 | BME680 | Read chip ID register (0xD0) from sensor at 0x77, expect 0x61 |

## Host Tests (~10 new)

- I2cSlaveInitRecordsOwnAddr
- I2cSlaveInitRecordsCallbacks
- I2cSlaveEnableRecordsCall
- I2cSlaveDisableRecordsCall
- I2cSlaveEnableThenDisableClearsState
- I2cSlaveRxCallbackInvoked
- I2cSlaveTxCallbackInvoked
- I2cSlaveMultipleInits
- I2cSlaveInitStoresArg
- I2cSlaveStateResetsCleanly

## Key Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| I2C bus lockup (SDA stuck low) | Toggle SCL 9 times via GPIO before AF mode |
| Internal pull-ups too weak | Start internal; add 4.7K external if unreliable |
| STOPF clearing wrong | Follow RM0090: read SR1, write CR1 with PE=1 |
| Buffer overflow on 256-byte test | kSlaveBufferSize=256; clamp rxIndex in ISR |
| AF in error ISR | Check slave active; AF in slave TX = normal |
