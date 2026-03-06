# RNG (Hardware Random Number Generator) -- Design Document

## Overview

Register-level driver for the STM32F4 True Random Number Generator (TRNG).
The RNG uses analog noise sources to produce 32-bit random numbers.

Available on STM32F4xx (not on STM32F2xx). Zynq-7000 has no equivalent
peripheral -- stub provided.

## Hardware

- **Peripheral:** RNG (True Random Number Generator)
- **Base address:** 0x50060800
- **Clock source:** PLL48CLK (48 MHz, from PLL Q output)
- **RCC gate:** AHB2ENR bit 6

### Register Map

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00   | RNG_CR   | Control: bit 2 = RNGEN (enable), bit 3 = IE (interrupt enable) |
| 0x04   | RNG_SR   | Status: bit 0 = DRDY, bit 1 = CECS (clock error), bit 2 = SECS (seed error) |
| 0x08   | RNG_DR   | Data: 32-bit random value (reading clears DRDY) |

### Operational Sequence

1. Enable RNG clock via RCC AHB2ENR
2. Set RNGEN bit in RNG_CR
3. Poll DRDY in RNG_SR
4. Read RNG_DR (clears DRDY)
5. Check CECS/SECS for errors after each read

## HAL API

```cpp
namespace hal
{
    // Enable RNG clock and peripheral
    std::int32_t rngInit();

    // Read a 32-bit random number. Blocks until DRDY or error.
    // Returns msos::error::kOk on success, kIo on clock/seed error.
    std::int32_t rngRead(std::uint32_t &value);

    // Disable RNG peripheral (does not gate clock)
    void rngDeinit();
}
```

## RCC Integration

New functions added to Rcc.h / Rcc.cpp:

```cpp
void rccEnableRngClock();
void rccDisableRngClock();
```

These set/clear bit 6 of RCC AHB2ENR (base 0x40023800, offset 0x34).

## Error Handling

- `rngRead()` checks CECS (clock error current status) and SECS (seed error
  current status) after DRDY. If either is set, returns `msos::error::kIo`
  and does not write to `value`.
- `rngInit()` returns `msos::error::kOk` unconditionally (no way to detect
  init failure without reading).

## Timeout

`rngRead()` uses a spin-loop with a configurable iteration limit
(default 1000000) to avoid hanging if the peripheral is broken. Returns
`msos::error::kTimedOut` if DRDY never asserts.

## Zynq-7000

Stub: `rngInit()` returns `msos::error::kNoSys`, `rngRead()` returns
`msos::error::kNoSys`, `rngDeinit()` is a no-op.

## Test Plan

Host tests (link-time mock substitution):
1. Init records call
2. Read returns injected value and kOk
3. Read with clock error returns kIo
4. Read with seed error returns kIo
5. Deinit records call
6. Multiple reads record sequentially
7. Read without init still works (mock does not enforce ordering)
8. Reset clears mock state

Hardware test (STM32F407, via ST-Link + ttyACM0):
- rng-demo app: init RNG, read 10 values, print to serial, verify non-zero
  and not all identical

## Hardware Test Results

Tested on STM32F407ZGT6, flashed via ST-Link V2, serial on /dev/ttyACM0
(CMSIS-DAP UART bridge).

```
=== RNG Hardware Test ===
Reading 10 random numbers from TRNG

Test 1: Init and read 10 values: PASS
Test 2: All values non-zero: PASS
Test 3: Not all identical: PASS
Test 4: Deinit: PASS

--- Summary: 4/4 passed (ALL PASS) ---
MSOS_SUMMARY:rng:pass=4:total=4:result=PASS
```

Sample values (all unique, high entropy):
```
0x45E821B9 0xBA1CAD75 0xBC840C9F 0x56F80603 0xA7FF3233
0xE6EC336F 0xA6F99A27 0x3B06E583 0x4F18F610 0xA2D45547
```
