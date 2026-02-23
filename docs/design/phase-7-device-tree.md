# Phase 7: Device Tree (FDT Binary)

## Overview

Hardware configuration is described in standard DTS (Device Tree Source) files
under `boards/`. A Python toolchain compiles them into DTB (Device Tree Blob)
binaries, which are embedded in firmware as const byte arrays. The kernel
parses the DTB at boot to populate a runtime `BoardConfig` struct.

This replaces the previous YAML-to-constexpr approach with the industry-standard
FDT format used by Linux, U-Boot, and Zephyr.

## Architecture

```
boards/*.dts                       DTS source (hand-written, standard format)
     |
     v  tools/build_dtbs.py (or dtc)
boards/*/board.dtb                 DTB binary (standard FDT format)
     |
     v  tools/dtb2cpp.py
boards/*/BoardDtb.cpp              C++ file: const uint8_t g_boardDtb[] = {...};
     |
     v  compiled + linked into firmware
Firmware .rodata                   DTB blob accessible at runtime
     |
     v  kernel::fdt::parse() at boot
board::BoardConfig struct          Runtime config populated from DTB
     |
     v  board::config() accessor
Apps use board::config().systemClock, board::config().hasLed, etc.
```

## DTS Format

Standard `/dts-v1/` format with ms-os-specific node layout:

```dts
/dts-v1/;

/ {
    compatible = "ms-os,stm32f407zgt6";
    model = "STM32F407ZGT6";

    board {
        name = "STM32F407ZGT6";
        mcu = "STM32F407ZGT6";
        arch = "cortex-m4";
    };

    clocks {
        system-clock = <168000000>;
        apb1-clock = <42000000>;
        apb2-clock = <84000000>;
        hse-clock = <8000000>;
    };

    memory {
        flash { reg = <0x08000000 0x100000>; };
        sram  { reg = <0x20000000 0x20000>; };
        ccm   { reg = <0x10000000 0x10000>; };
    };

    console {
        uart = "usart1";
        baud = <115200>;
        tx { port = "A"; pin = <9>; af = <7>; };
        rx { port = "A"; pin = <10>; af = <7>; };
    };

    led {
        port = "C";
        pin = <13>;
    };

    features {
        fpu;
    };
};
```

## FDT Binary Format

Standard format (magic `0xD00DFEED`):
- Big-endian, 40-byte header
- Structure block: tokens FDT_BEGIN_NODE(1), FDT_END_NODE(2), FDT_PROP(3), FDT_END(9)
- Strings block: null-terminated property name strings
- Properties: `{len, nameoff}` followed by value bytes (4-byte aligned)

## Kernel FDT Parser

Minimal read-only parser (`kernel/inc/kernel/Fdt.h`, ~300 lines):

- `validate()` -- check magic, version, size
- `findNode()` -- locate node by path (e.g., "/clocks", "/console/tx")
- `readU32()` -- big-endian uint32 property
- `readString()` -- null-terminated string property
- `hasProperty()` -- boolean (zero-length) property check
- `readReg()` -- reg property (base, size pair)
- `firstChild()` / `nextSibling()` -- child iteration
- `nodeName()` -- get node name at offset

Uses `__builtin_bswap32()` for big-endian conversion.

## Runtime BoardConfig

Populated at boot from DTB (`kernel/inc/kernel/BoardConfig.h`):

```cpp
namespace board
{
    struct BoardConfig
    {
        const char *boardName, *mcu, *arch;
        std::uint32_t systemClock, apb1Clock, apb2Clock, hseClock;
        MemoryRegion memoryRegions[4];
        std::uint8_t memoryRegionCount;
        const char *consoleUart;
        std::uint32_t consoleBaud;
        bool hasConsoleTx, hasConsoleRx;
        PinConfig consoleTx, consoleRx;
        bool hasLed;
        PinConfig led;
        bool hasFpu;
    };

    void configInit(const std::uint8_t *dtb, std::uint32_t dtbSize);
    const BoardConfig &config();
    hal::UartId consoleUartId();
}
```

Apps call `board::configInit(g_boardDtb, g_boardDtbSize)` at start of `main()`.

## Shell Command

The `dt` command displays the parsed device tree:

```
ms-os> dt
board: STM32F407ZGT6
mcu:   STM32F407ZGT6
arch:  cortex-m4
clocks:
  system: 168000000
  apb1:   42000000
  apb2:   84000000
  hse:    8000000
memory:
  flash: 0x8000000 (1048576)
  sram: 0x20000000 (131072)
  ccm: 0x10000000 (65536)
console: usart1 @ 115200
led: C13
fpu: yes
```

## CMake Integration

DTB blob compiled as a static library:

```cmake
add_library(board_dtb STATIC
    ${CMAKE_SOURCE_DIR}/boards/${MSOS_BOARD_DIR}/BoardDtb.cpp
)
```

FDT parser and BoardConfig are part of the kernel library:

```cmake
# In kernel/CMakeLists.txt
src/core/Fdt.cpp
src/core/BoardConfig.cpp
```

## Tools

| Tool | Purpose |
|------|---------|
| `tools/fdtlib.py` | Python FDT builder (creates DTB from data structures) |
| `tools/build_dtbs.py` | Builds DTBs for all boards |
| `tools/dtb2cpp.py` | Converts DTB binary to C++ const byte array |
| `tools/dtgen/` | Legacy YAML-to-constexpr generator (kept for reference) |

## Files

| File | Purpose |
|------|---------|
| `boards/*.dts` | DTS source files (3 boards) |
| `boards/*/board.dtb` | Compiled DTB binaries |
| `boards/*/BoardDtb.cpp` | DTB as C++ const byte array |
| `kernel/inc/kernel/Fdt.h` | FDT parser API |
| `kernel/src/core/Fdt.cpp` | FDT parser implementation |
| `kernel/inc/kernel/BoardConfig.h` | Runtime board config struct |
| `kernel/src/core/BoardConfig.cpp` | Config population from DTB |
| `test/kernel/FdtTest.cpp` | 38 FDT parser tests |
| `test/kernel/BoardConfigTest.cpp` | 19 BoardConfig tests |
| `tools/dtb2cpp_test.py` | 21 dtb2cpp Python tests |

## Test Coverage

- 38 C++ FDT parser tests (header validation, node finding, property reading, child iteration, error cases)
- 19 C++ BoardConfig tests (identity, clocks, memory, console, LED, FPU, UART mapping, invalid DTB)
- 9 C++ shell dt command tests
- 21 Python dtb2cpp tests (DTB format, C++ output, round-trip, error handling)

Total: 87 new tests (66 C++ + 21 Python)

## Memory Budget

| Component | Bytes |
|-----------|-------|
| DTB blob (.rodata) | ~500-800 |
| FDT parser code (.text) | ~1,500 |
| BoardConfig struct (.bss) | ~80 |
| BoardConfig.cpp code (.text) | ~400 |
| Shell dt command (.text) | ~200 |
| **Total FDT overhead** | **~2,700-3,000** |
