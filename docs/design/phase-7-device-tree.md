# Phase 7: Device Tree (YAML Board Descriptions)

## Overview

Hardware configuration is described in per-target YAML files under `boards/`.
A Python code generator (`dtgen`) compiles them into constexpr C++ headers
(`BoardConfig.h`) that applications include for hardware constants.

This eliminates hardcoded magic numbers (clock frequencies, pin assignments,
UART IDs) from application code and makes adding a new board a single YAML
file rather than editing every app.

## YAML Schema

```yaml
board:
  name: STM32F407ZGT6      # Human-readable name
  mcu: STM32F407ZGT6       # MCU part number
  arch: cortex-m4           # Architecture

clocks:
  system: 168000000         # SYSCLK (Hz)
  apb1: 42000000            # APB1 peripheral clock
  apb2: 84000000            # APB2 peripheral clock
  hse: 8000000              # HSE crystal (optional)

memory:
  flash:
    base: 0x08000000
    size: 0x100000
  sram:
    base: 0x20000000
    size: 0x20000
  ccm:                      # Optional regions
    base: 0x10000000
    size: 0x10000

console:
  uart: usart1              # Maps to hal::UartId enum
  baud: 115200
  tx:                       # Optional (not present on PYNQ-Z2)
    port: A
    pin: 9
    af: 7
  rx:                       # Optional
    port: A
    pin: 10
    af: 7

led:                        # Optional
  port: C
  pin: 13

features:
  fpu: true                 # Optional feature flags
```

## Code Generator (dtgen)

```bash
python3 -m tools.dtgen boards/stm32f407zgt6.yaml --outdir boards/stm32f407zgt6/
```

Generates a `BoardConfig.h` with:
- `board::kBoardName`, `board::kMcu`, `board::kArch`
- `board::kSystemClock`, `board::kApb1Clock`, etc.
- `board::kConsoleUart`, `board::kConsoleBaud`
- `board::kHasConsoleTx`, `board::kConsoleTxPort/Pin/Af`
- `board::kHasConsoleRx`, `board::kConsoleRxPort/Pin/Af`
- `board::kHasLed`, `board::kLedPort/Pin`
- `board::kHasFpu`
- Memory region base/size constants

## CMake Integration

A `board_config` interface library provides the include path:

```cmake
add_library(board_config INTERFACE)
target_include_directories(board_config INTERFACE
    ${CMAKE_SOURCE_DIR}/boards/${MSOS_BOARD_DIR}
)
```

Apps link `board_config` and use `if constexpr` for optional features:

```cpp
#include "BoardConfig.h"

if constexpr (board::kHasLed)
{
    hal::gpioToggle(board::kLedPort, board::kLedPin);
}
```

## Files

| File | Purpose |
|------|---------|
| `boards/*.yaml` | Board descriptions (3 targets) |
| `boards/*/BoardConfig.h` | Generated constexpr headers |
| `tools/dtgen/schema.py` | YAML parser + BoardDescription dataclass |
| `tools/dtgen/emitter.py` | C++ header emitter |
| `tools/dtgen/__main__.py` | CLI entry point |
| `tools/dtgen/test/test_dtgen.py` | 74 tests (schema + emitter) |

## Test Coverage

74 Python tests covering:
- YAML parsing (board, clocks, memory, console, LED, features)
- Validation errors (missing fields, empty input, invalid YAML)
- C++ emission (all constant names, types, values, includes)
- Optional field handling (no TX/RX pin, no LED, no HSE, no CCM)
