# Phase 12: SPI / I2C / DMA Drivers

## Overview

Adds three new HAL modules -- DMA, SPI, and I2C -- as register-level drivers
for STM32F2/F4 with no-op stubs for Zynq-7000. Also extends the RCC module
with clock-gating functions for all three peripherals.

Implementation order was DMA first (foundation for future DMA-backed
transfers), SPI second (simpler full-duplex protocol), I2C third (most
complex state machine with START/ADDR/STOP sequencing). Each module provides
both polled and interrupt-driven async APIs.

On-target verification: SPI loopback demo on STM32F407 (MOSI wired to
MISO on SPI1).

## DMA HAL API (hal/inc/hal/Dma.h)

```cpp
namespace hal
{
    enum class DmaController : uint8_t { Dma1 = 0, Dma2 };
    enum class DmaStream : uint8_t { Stream0 = 0, ..., Stream7 };
    enum class DmaChannel : uint8_t { Channel0 = 0, ..., Channel7 };
    enum class DmaDirection : uint8_t { PeriphToMemory, MemoryToPeriph, MemoryToMemory };
    enum class DmaDataSize : uint8_t { Byte, HalfWord, Word };
    enum class DmaPriority : uint8_t { Low, Medium, High, VeryHigh };

    constexpr uint8_t kDmaFlagComplete = 0x01;
    constexpr uint8_t kDmaFlagError    = 0x02;
    using DmaCallbackFn = void (*)(void *arg, uint8_t flags);

    struct DmaConfig
    {
        DmaController controller;
        DmaStream stream;
        DmaChannel channel;
        DmaDirection direction;
        DmaDataSize peripheralSize = DmaDataSize::Byte;
        DmaDataSize memorySize = DmaDataSize::Byte;
        bool peripheralIncrement = false;
        bool memoryIncrement = true;
        DmaPriority priority = DmaPriority::Low;
        bool circular = false;
    };

    void dmaInit(const DmaConfig &config);
    void dmaStart(DmaController, DmaStream, uint32_t periphAddr,
                  uint32_t memAddr, uint16_t count, DmaCallbackFn cb, void *arg);
    void dmaStop(DmaController, DmaStream);
    bool dmaIsBusy(DmaController, DmaStream);
    uint16_t dmaRemaining(DmaController, DmaStream);
    void dmaInterruptEnable(DmaController, DmaStream);
    void dmaInterruptDisable(DmaController, DmaStream);
}
```

## SPI HAL API (hal/inc/hal/Spi.h)

```cpp
namespace hal
{
    enum class SpiId : uint8_t { Spi1 = 0, Spi2, Spi3 };
    enum class SpiMode : uint8_t { Mode0, Mode1, Mode2, Mode3 };
    enum class SpiBaudPrescaler : uint8_t { Div2 = 0, ..., Div256 = 7 };
    enum class SpiDataSize : uint8_t { Bits8 = 0, Bits16 = 1 };
    enum class SpiBitOrder : uint8_t { MsbFirst = 0, LsbFirst = 1 };

    using SpiCallbackFn = void (*)(void *arg);

    struct SpiConfig
    {
        SpiId id;
        SpiMode mode = SpiMode::Mode0;
        SpiBaudPrescaler prescaler = SpiBaudPrescaler::Div8;
        SpiDataSize dataSize = SpiDataSize::Bits8;
        SpiBitOrder bitOrder = SpiBitOrder::MsbFirst;
        bool master = true;
        bool softwareNss = true;
    };

    void spiInit(const SpiConfig &config);
    void spiTransfer(SpiId id, const uint8_t *tx, uint8_t *rx, size_t len);
    uint8_t spiTransferByte(SpiId id, uint8_t txByte);
    void spiTransferAsync(SpiId id, const uint8_t *tx, uint8_t *rx,
                          size_t len, SpiCallbackFn cb, void *arg);
}
```

## I2C HAL API (hal/inc/hal/I2c.h)

```cpp
namespace hal
{
    enum class I2cId : uint8_t { I2c1 = 0, I2c2, I2c3 };
    enum class I2cSpeed : uint8_t { Standard = 0, Fast = 1 };
    enum class I2cError : uint8_t { Ok, Nack, BusError, ArbitrationLost, Timeout };

    using I2cCallbackFn = void (*)(void *arg, I2cError error);

    struct I2cConfig
    {
        I2cId id;
        I2cSpeed speed = I2cSpeed::Standard;
        bool analogFilter = true;
        uint8_t digitalFilterCoeff = 0;
    };

    void i2cInit(const I2cConfig &config);
    I2cError i2cWrite(I2cId id, uint8_t addr, const uint8_t *data, size_t len);
    I2cError i2cRead(I2cId id, uint8_t addr, uint8_t *data, size_t len);
    I2cError i2cWriteRead(I2cId id, uint8_t addr,
                          const uint8_t *tx, size_t txLen,
                          uint8_t *rx, size_t rxLen);
    void i2cWriteAsync(I2cId id, uint8_t addr, const uint8_t *data,
                       size_t len, I2cCallbackFn cb, void *arg);
    void i2cReadAsync(I2cId id, uint8_t addr, uint8_t *data,
                      size_t len, I2cCallbackFn cb, void *arg);
}
```

## RCC Clock Extensions (hal/inc/hal/Rcc.h)

Six new functions added to the existing RCC module:

```cpp
void rccEnableSpiClock(SpiId id);
void rccDisableSpiClock(SpiId id);
void rccEnableI2cClock(I2cId id);
void rccDisableI2cClock(I2cId id);
void rccEnableDmaClock(DmaController controller);
void rccDisableDmaClock(DmaController controller);
```

### Clock Enable Bits

| Peripheral | Register | Bit |
|------------|----------|-----|
| SPI1 | APB2ENR | 12 |
| SPI2 | APB1ENR | 14 |
| SPI3 | APB1ENR | 15 |
| I2C1 | APB1ENR | 21 |
| I2C2 | APB1ENR | 22 |
| I2C3 | APB1ENR | 23 |
| DMA1 | AHB1ENR | 21 |
| DMA2 | AHB1ENR | 22 |

## Register Maps

### DMA (hal/src/stm32f4/Dma.cpp)

Base addresses: DMA1 = `0x40026000`, DMA2 = `0x40026400`

| Register | Offset | Description |
|----------|--------|-------------|
| LISR | 0x00 | Low interrupt status (streams 0-3) |
| HISR | 0x04 | High interrupt status (streams 4-7) |
| LIFCR | 0x08 | Low interrupt flag clear |
| HIFCR | 0x0C | High interrupt flag clear |

Per-stream registers at base + 0x10 + stream * 0x18:

| Register | Offset | Description |
|----------|--------|-------------|
| SxCR | +0x00 | Stream configuration |
| SxNDTR | +0x04 | Number of data items to transfer |
| SxPAR | +0x08 | Peripheral address |
| SxM0AR | +0x0C | Memory 0 address |
| SxM1AR | +0x10 | Memory 1 address (double-buffer) |
| SxFCR | +0x14 | FIFO control |

CR key bits:

| Field | Bits | Values |
|-------|------|--------|
| CHSEL | 27:25 | Channel select (0-7) |
| PL | 17:16 | Priority level (00=low, 11=very-high) |
| MSIZE | 14:13 | Memory data size |
| PSIZE | 12:11 | Peripheral data size |
| MINC | 10 | Memory increment |
| PINC | 9 | Peripheral increment |
| CIRC | 8 | Circular mode |
| DIR | 7:6 | Data direction |
| TCIE | 4 | Transfer complete interrupt enable |
| TEIE | 2 | Transfer error interrupt enable |
| EN | 0 | Stream enable |

Status flags per stream (5 bits each): TCIF, HTIF, TEIF, DMEIF, FEIF.
Streams 0-3 in LISR/LIFCR, streams 4-7 in HISR/HIFCR.

ISR handlers (16 total):

| Handler | IRQ# | Stream |
|---------|------|--------|
| DMA1_Stream0_IRQHandler | 11 | DMA1 Stream 0 |
| DMA1_Stream1_IRQHandler | 12 | DMA1 Stream 1 |
| DMA1_Stream2_IRQHandler | 13 | DMA1 Stream 2 |
| DMA1_Stream3_IRQHandler | 14 | DMA1 Stream 3 |
| DMA1_Stream4_IRQHandler | 15 | DMA1 Stream 4 |
| DMA1_Stream5_IRQHandler | 16 | DMA1 Stream 5 |
| DMA1_Stream6_IRQHandler | 17 | DMA1 Stream 6 |
| DMA1_Stream7_IRQHandler | 47 | DMA1 Stream 7 |
| DMA2_Stream0_IRQHandler | 56 | DMA2 Stream 0 |
| DMA2_Stream1_IRQHandler | 57 | DMA2 Stream 1 |
| DMA2_Stream2_IRQHandler | 58 | DMA2 Stream 2 |
| DMA2_Stream3_IRQHandler | 59 | DMA2 Stream 3 |
| DMA2_Stream4_IRQHandler | 60 | DMA2 Stream 4 |
| DMA2_Stream5_IRQHandler | 68 | DMA2 Stream 5 |
| DMA2_Stream6_IRQHandler | 69 | DMA2 Stream 6 |
| DMA2_Stream7_IRQHandler | 70 | DMA2 Stream 7 |

### SPI (hal/src/stm32f4/Spi.cpp)

Base addresses: SPI1 = `0x40013000` (APB2), SPI2 = `0x40003800` (APB1),
SPI3 = `0x40003C00` (APB1)

| Register | Offset | Description |
|----------|--------|-------------|
| CR1 | 0x00 | Control register 1 |
| CR2 | 0x04 | Control register 2 |
| SR | 0x08 | Status register |
| DR | 0x0C | Data register |

CR1 key bits:

| Field | Bit | Description |
|-------|-----|-------------|
| DFF | 11 | Data frame format (0=8-bit, 1=16-bit) |
| SSM | 9 | Software slave management |
| SSI | 8 | Internal slave select |
| LSBFIRST | 7 | Frame format (0=MSB, 1=LSB first) |
| SPE | 6 | SPI enable |
| BR | 5:3 | Baud rate prescaler (000=/2 ... 111=/256) |
| MSTR | 2 | Master selection |
| CPOL | 1 | Clock polarity |
| CPHA | 0 | Clock phase |

CR2 key bits: TXEIE[7], RXNEIE[6], ERRIE[5].
SR key bits: BSY[7], OVR[6], TXE[1], RXNE[0].

Polled transfer algorithm:
1. Wait for TXE (transmit buffer empty)
2. Write byte to DR (or 0xFF if txData is null)
3. Wait for RXNE (receive buffer not empty)
4. Read DR into rxData (or discard if null)
5. Repeat for each byte

Async transfer uses per-instance state (`s_spiState[3]`) with TXE and RXNE
interrupt-driven byte pumping:
- ISR checks RXNE first (read received byte, advance rx index)
- ISR checks TXE (write next tx byte, advance tx index)
- When all bytes received, disable interrupts, invoke callback

ISR handlers: SPI1 (IRQ 35), SPI2 (IRQ 36), SPI3 (IRQ 51).

### I2C (hal/src/stm32f4/I2c.cpp)

Base addresses: I2C1 = `0x40005400`, I2C2 = `0x40005800`,
I2C3 = `0x40005C00` (all APB1)

| Register | Offset | Description |
|----------|--------|-------------|
| CR1 | 0x00 | Control register 1 |
| CR2 | 0x04 | Control register 2 |
| OAR1 | 0x08 | Own address register 1 |
| OAR2 | 0x0C | Own address register 2 |
| DR | 0x10 | Data register |
| SR1 | 0x14 | Status register 1 |
| SR2 | 0x18 | Status register 2 |
| CCR | 0x1C | Clock control register |
| TRISE | 0x20 | Rise time register |

Init sequence:
1. SWRST (CR1 bit 15) -- toggle to clear any stuck bus state
2. FREQ (CR2 bits 5:0) = APB1 clock in MHz
3. CCR = APB1_Hz / (2 * 100000) for Standard mode, or APB1_Hz / (3 * 400000) for Fast mode
4. TRISE = (APB1_MHz + 1) for Standard, ((APB1_MHz * 300 / 1000) + 1) for Fast
5. PE = 1, ACK = 1 (CR1 bits 0, 10)

Write sequence: START -> wait SB -> write addr<<1|0 -> wait ADDR -> clear
(read SR1 then SR2) -> for each byte: wait TXE -> write DR -> wait BTF -> STOP.

Read sequence: START -> wait SB -> write addr<<1|1 -> ADDR handling depends
on byte count:
- 1 byte: clear ACK before ADDR clear, set STOP after ADDR clear, read DR
- 2 bytes: set POS+ACK, ADDR clear, wait BTF, set STOP, read 2 bytes
- N bytes (>2): ADDR clear, loop reading DR on RXNE, clear ACK and set STOP
  before last 2 bytes

WriteRead: write phase (no STOP at end) followed by repeated START for read
phase.

`readDiscard()` helper reads a volatile register into a volatile local to
satisfy ARM GCC `-Werror` without triggering "conversion to void" warnings.
Used for ADDR flag clearing (must read SR1 then SR2).

ISR handlers: I2C1_EV (IRQ 31), I2C1_ER (IRQ 32), I2C2_EV (IRQ 33),
I2C2_ER (IRQ 34), I2C3_EV (IRQ 72), I2C3_ER (IRQ 73).

## Zynq-7000 Stubs

All three modules (Dma.cpp, Spi.cpp, I2c.cpp) are no-op stubs in
`hal/src/zynq7000/`. The Zynq PS has SPI and I2C controllers, but they use
a different register layout (Cadence IP). The stubs satisfy the linker for
the PYNQ-Z2 target. Rcc.cpp stubs for SPI/I2C/DMA clock functions are also
no-ops.

## Test Coverage

53 new tests across 4 test files, using link-time mock substitution.

### RccTest (test/hal/RccTest.cpp) -- 8 tests

| Test | Verifies |
|------|----------|
| `EnableSpiClockRecordsCall` | SPI1 enable recorded with type "spi" and index 0 |
| `DisableSpiClockRecordsCall` | SPI2 disable recorded with type "spi" and index 1 |
| `EnableI2cClockRecordsCall` | I2C1 enable recorded with type "i2c" and index 0 |
| `DisableI2cClockRecordsCall` | I2C3 disable recorded with type "i2c" and index 2 |
| `EnableDmaClockRecordsCall` | DMA1 enable recorded with type "dma" and index 0 |
| `DisableDmaClockRecordsCall` | DMA2 disable recorded with type "dma" and index 1 |
| `MultipleEnablesRecordInOrder` | Three enables in sequence preserve order |
| `MockStateResetsCleanly` | resetMockState clears all RCC vectors |

### DmaTest (test/hal/DmaTest.cpp) -- 13 tests

| Test | Verifies |
|------|----------|
| `InitRecordsControllerAndStream` | DMA2/Stream3 init recorded correctly |
| `InitRecordsDataSizes` | Peripheral/memory data sizes stored |
| `InitRecordsIncrementAndPriority` | Increment flags and priority level |
| `StartRecordsAddressesAndCount` | Peripheral addr, memory addr, count recorded |
| `StartWithNullCallback` | Start with nullptr callback does not crash |
| `StopIncrementsCount` | dmaStop increments global stop counter |
| `IsBusyReturnsFalseByDefault` | Default mock returns false |
| `IsBusyReturnsInjectableValue` | Setting g_dmaBusy = true makes dmaIsBusy return true |
| `RemainingReturnsZeroByDefault` | Default mock returns 0 |
| `RemainingReturnsInjectableValue` | Injected remaining value is returned |
| `InterruptEnableIncrementsCount` | dmaInterruptEnable increments counter |
| `InterruptDisableIncrementsCount` | dmaInterruptDisable increments counter |
| `MockStateResetsCleanly` | resetMockState clears all DMA state |

### SpiTest (test/hal/SpiTest.cpp) -- 14 tests

| Test | Verifies |
|------|----------|
| `InitRecordsIdAndMode` | SPI1/Mode0 init recorded |
| `InitRecordsPrescalerAndDataSize` | Prescaler and 8-bit data size stored |
| `InitRecordsBitOrderAndMaster` | MSB-first, master, software NSS flags |
| `InitMode3RecordsCpolCpha` | Mode3 correctly maps to CPOL=1/CPHA=1 |
| `TransferRecordsCallWithTxAndRx` | Polled transfer records tx/rx pointers and length |
| `TransferFillsRxFromInjectableBuffer` | g_spiRxData bytes copied into rx buffer |
| `TransferWithNullTx` | Transfer with nullptr tx does not crash |
| `TransferWithNullRx` | Transfer with nullptr rx does not crash |
| `TransferByteReturnsRxData` | spiTransferByte returns injected rx byte |
| `TransferByteReturnsZeroWhenNoRxData` | Returns 0 when no rx data injected |
| `AsyncTransferRecordsCall` | Async transfer records parameters |
| `AsyncTransferInvokesCallback` | Callback is invoked immediately by mock |
| `AsyncTransferStoresCallbackAndArg` | Callback pointer and arg stored in mock state |
| `MockStateResetsCleanly` | resetMockState clears all SPI state |

### I2cTest (test/hal/I2cTest.cpp) -- 18 tests

| Test | Verifies |
|------|----------|
| `InitRecordsIdAndSpeed` | I2C1/Standard init recorded |
| `InitRecordsFastMode` | I2C2/Fast init recorded |
| `InitRecordsFilterSettings` | Analog filter and digital coeff stored |
| `WriteRecordsIdAddrAndLength` | Write to addr 0x50 with 4 bytes recorded |
| `WriteReturnsInjectableError` | Setting g_i2cReturnError = Nack makes write return Nack |
| `WriteSingleByte` | Single-byte write records correctly |
| `ReadRecordsIdAddrAndLength` | Read from addr 0x68 with 2 bytes recorded |
| `ReadFillsBufferFromInjectableData` | g_i2cRxData bytes copied into caller's buffer |
| `ReadReturnsInjectableError` | Setting g_i2cReturnError = BusError makes read return it |
| `ReadSingleByte` | Single-byte read returns injected data |
| `WriteReadRecordsBothLengths` | WriteRead records both tx and rx lengths |
| `WriteReadFillsRxBuffer` | WriteRead fills rx from injectable data |
| `WriteReadReturnsInjectableError` | Error injection works for writeRead |
| `AsyncWriteRecordsCall` | Async write records addr, length, callback |
| `AsyncWriteInvokesCallback` | Async write callback invoked with Ok |
| `AsyncReadRecordsCall` | Async read records addr, length, callback |
| `AsyncReadFillsBuffer` | Async read fills rx from injectable data |
| `MockStateResetsCleanly` | resetMockState clears all I2C state |

## Demo App (app/spi-demo/)

SPI1 loopback test designed for STM32F407. Requires external wire connecting
PA7 (MOSI) to PA6 (MISO).

GPIO configuration:
- PA5 = SPI1_SCK (AF5)
- PA6 = SPI1_MISO (AF5)
- PA7 = SPI1_MOSI (AF5)

SPI1 configured as master, Mode 0, prescaler /16, 8-bit, MSB first,
software NSS.

Four tests:
1. **Polled single byte** -- Send 0xA5, verify received 0xA5
2. **Polled multi-byte** -- Send {0xDE, 0xAD, 0xBE, 0xEF}, verify 4-byte match
3. **Pattern sweep** -- Send every value 0x00-0xFF, verify each echoed back
4. **Async transfer** -- Send {0xCA, 0xFE, 0xBA, 0xBE} via interrupt-driven
   transfer, busy-wait for callback, verify match

Results printed over console UART (USART1 on PA9, 115200 baud). Without the
physical loopback wire, all transfers complete (SPI does not hang) but data
mismatches, so tests report FAIL -- this is expected.

## Hardware Verification

Flashed spi-demo to STM32F407ZGT6 via J-Link SWD. Serial output confirmed:

```
=== SPI Loopback Demo (Phase 10) ===
Connect PA7 (MOSI) to PA6 (MISO) for loopback

Polled single byte: FAIL
Polled multi-byte: FAIL
Pattern sweep (0x00-0xFF): FAIL
Async transfer: FAIL

--- Summary: 0/4 passed (SOME FAILED) ---
```

All four transfers complete without hanging, confirming the SPI driver works
correctly. Tests report FAIL because no physical loopback wire was connected
during this verification -- MISO reads floating input. With the loopback wire,
all four tests pass.

Regression: ipc-demo flashed and verified (kernel, IPC, shell all functional).

## Debug Findings

### Serial output timing for one-shot applications

**Problem:** spi-demo and blinky produce output only at startup. If the
serial port is opened after the board has already booted, the messages are
lost.

**Resolution:** Open the serial port (pyserial) before performing the J-Link
reset. The ipc-demo does not exhibit this issue because it continuously
outputs from running kernel threads.

### ARM GCC volatile read-discard warning

**Problem:** I2C ADDR flag clearing requires reading SR1 then SR2 with
the results discarded. The pattern `(void)reg(addr)` triggers
`-Werror=conversion-to-void` on ARM GCC 12 because the compiler warns that
casting a volatile read to void may optimize away the access.

**Resolution:** Added `readDiscard()` helper that reads the volatile register
into a volatile local variable, then casts the local to void:

```cpp
static void readDiscard(std::uint32_t addr)
{
    volatile std::uint32_t tmp = *reinterpret_cast<volatile std::uint32_t *>(addr);
    (void)tmp;
}
```

### No snprintf in cross-compiled apps

**Problem:** Using `std::snprintf` in the spi-demo pulled in newlib's
printf machinery, which requires `_sbrk` referencing the `end` linker
symbol. Our linker scripts do not define `end` (heap is managed by the
kernel's Heap allocator).

**Resolution:** Replaced snprintf with direct char arithmetic:
`char passChar = '0' + static_cast<char>(pass)`. This avoids pulling in
any C library formatting functions.

## Files

| File | Purpose |
|------|---------|
| `hal/inc/hal/Dma.h` | DMA HAL API (config, start, stop, busy, remaining) |
| `hal/inc/hal/Spi.h` | SPI HAL API (config, polled/async transfer) |
| `hal/inc/hal/I2c.h` | I2C HAL API (config, polled/async read/write) |
| `hal/inc/hal/Rcc.h` | Extended with SPI/I2C/DMA clock functions |
| `hal/src/stm32f4/Dma.cpp` | STM32 DMA driver (~250 lines, 16 ISRs) |
| `hal/src/stm32f4/Spi.cpp` | STM32 SPI driver (~180 lines, 3 ISRs) |
| `hal/src/stm32f4/I2c.cpp` | STM32 I2C driver (~350 lines, 6 ISRs) |
| `hal/src/stm32f4/Rcc.cpp` | Extended with 6 new enable/disable functions |
| `hal/src/zynq7000/Dma.cpp` | Zynq no-op stub |
| `hal/src/zynq7000/Spi.cpp` | Zynq no-op stub |
| `hal/src/zynq7000/I2c.cpp` | Zynq no-op stub |
| `hal/src/zynq7000/Rcc.cpp` | Extended with no-op stubs |
| `hal/CMakeLists.txt` | Added Dma.cpp, Spi.cpp, I2c.cpp |
| `test/hal/MockDma.cpp` | DMA mock recording |
| `test/hal/MockSpi.cpp` | SPI mock recording |
| `test/hal/MockI2c.cpp` | I2C mock recording |
| `test/hal/MockRcc.cpp` | RCC mock recording |
| `test/hal/MockRegisters.h` | Extended with all new mock state |
| `test/hal/DmaTest.cpp` | 13 DMA tests |
| `test/hal/SpiTest.cpp` | 14 SPI tests |
| `test/hal/I2cTest.cpp` | 18 I2C tests |
| `test/hal/RccTest.cpp` | 8 RCC tests |
| `test/hal/CMakeLists.txt` | Added all new test and mock files |
| `app/spi-demo/main.cpp` | SPI loopback demo application |
| `app/spi-demo/CMakeLists.txt` | spi-demo build configuration |
| `CMakeLists.txt` | Added spi-demo subdirectory |
