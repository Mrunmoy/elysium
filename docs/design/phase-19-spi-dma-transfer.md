# Phase 19: SPI DMA Transfer (Peripheral Integration Slice 1)

## Goal

Implement DMA-driven SPI full-duplex transfer for STM32F407 and validate it with:

- host unit tests (mocked HAL)
- on-target board-to-board verification via existing SPI test runner

This phase targets the first item under DMA + Peripheral Integration:

- DMA-driven SPI transfers (`spiTransferDma`)

## Scope

Included:

- new HAL API for SPI DMA transfer
- STM32F4 implementation for SPI1 + DMA2 mapping
- host-side mock + tests for API behavior and status codes
- one on-target SPI board-to-board case using DMA path

Not included in this phase:

- UART DMA TX/RX
- ADC continuous DMA path
- multi-SPI-id DMA mapping beyond SPI1

## API

Add to `hal/inc/hal/Spi.h`:

```cpp
std::int32_t spiTransferDma(SpiId id,
                            const std::uint8_t *txData,
                            std::uint8_t *rxData,
                            std::size_t length,
                            std::uint32_t timeoutLoops);
```

Status codes:

- `kOk` on success
- `kInvalid` for invalid args (`length==0`, bad id)
- `kNoSys` for unsupported DMA mapping in this phase
- `kTimedOut` when DMA/SPI completion does not finish in time

## STM32F4 SPI1 DMA Mapping

For SPI1 (RM0090 mapping):

- RX: DMA2 Stream0 Channel3
- TX: DMA2 Stream3 Channel3

Implementation strategy:

1. Configure RX DMA (`PeriphToMemory`) and TX DMA (`MemoryToPeriph`)
2. Use fixed dummy source/sink when tx/rx pointer is null
3. Enable SPI CR2 DMA request bits (RXDMAEN/TXDMAEN)
4. Start RX first, then TX
5. Poll for DMA idle + SPI BSY clear with bounded timeout
6. Disable DMA request bits and stop streams

## On-Target Verification

Update `app/spi2-test` with one DMA-specific case (multi-byte echo) to prove
`spiTransferDma` works in board-to-board operation and is visible in machine output.

## Test Plan

Host tests (`test/hal/SpiTest.cpp`):

- records DMA transfer call
- invalid args return expected status
- unsupported ID returns `kNoSys`
- callback/side effects unchanged for existing APIs

Hardware:

- run `tools/hw_driver_runner.py --drivers spi` on STM32F407 board pair
- require summary PASS with DMA case included

## Artifacts

- `hal/inc/hal/Spi.h`
- `hal/src/stm32f4/Spi.cpp`
- `test/hal/MockSpi.cpp`
- `test/hal/MockRegisters.h`
- `test/hal/SpiTest.cpp`
- `app/spi2-test/main.cpp`
- `docs/design/phase-19-spi-dma-transfer.md`
- `docs/TASKS.md`
