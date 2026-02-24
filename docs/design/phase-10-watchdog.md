# Phase 10: Hardware Watchdog

## Overview

Adds an Independent Watchdog (IWDG) driver for STM32F2/F4 targets with
kernel integration through the idle thread. If any thread monopolizes the
CPU and starves the idle thread, the watchdog counter expires and resets
the MCU. A shell command reports whether the watchdog is active.

## HAL API (hal/inc/hal/Watchdog.h)

```cpp
namespace hal
{
    enum class WatchdogPrescaler : std::uint8_t
    {
        Div4   = 0,
        Div8   = 1,
        Div16  = 2,
        Div32  = 3,
        Div64  = 4,
        Div128 = 5,
        Div256 = 6
    };

    struct WatchdogConfig
    {
        WatchdogPrescaler prescaler;
        std::uint16_t reloadValue;      // 0-4095 (12-bit)
    };

    void watchdogInit(const WatchdogConfig &config);
    void watchdogFeed();
}
```

`watchdogInit` unlocks the IWDG registers, writes the prescaler and
reload value, then starts the watchdog. Once started, the IWDG cannot be
stopped -- only a system reset disables it. `watchdogFeed` reloads the
down-counter by writing the reload key.

## Register Map (STM32 IWDG)

Base address: `0x40003000`

| Offset | Name | Width | Description |
|--------|------|-------|-------------|
| 0x00 | KR | 32-bit (write-only) | Key register |
| 0x04 | PR | 32-bit | Prescaler register (bits 2:0) |
| 0x08 | RLR | 32-bit | Reload register (bits 11:0) |
| 0x0C | SR | 32-bit (read-only) | Status register |

### Key Register Values

| Key | Value | Effect |
|-----|-------|--------|
| Unlock | `0x5555` | Enable write access to PR and RLR |
| Reload | `0xAAAA` | Reload the down-counter from RLR |
| Start | `0xCCCC` | Start the watchdog (irreversible) |

### Status Register Bits

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | PVU | Prescaler value update in progress |
| 1 | RVU | Reload value update in progress |

The driver polls PVU and RVU to zero before writing PR and RLR,
respectively, to ensure the previous write has completed.

### Timeout Calculation

The IWDG is clocked from the LSI oscillator at approximately 32 kHz:

```
timeout_ms = (prescaler_divider * (reloadValue + 1)) / 32
```

| Prescaler | Divider | Min timeout (reload=0) | Max timeout (reload=4095) |
|-----------|---------|------------------------|---------------------------|
| Div4 | 4 | 0.125 ms | 512 ms |
| Div8 | 8 | 0.25 ms | 1024 ms |
| Div16 | 16 | 0.5 ms | 2048 ms |
| Div32 | 32 | 1 ms | 4096 ms |
| Div64 | 64 | 2 ms | 8192 ms |
| Div128 | 128 | 4 ms | 16384 ms |
| Div256 | 256 | 8 ms | 32768 ms |

## Implementation

### STM32F2/F4 (hal/src/stm32f4/Watchdog.cpp)

The init sequence:

1. Write `0x5555` to KR (unlock PR/RLR for writing)
2. Poll SR until PVU == 0
3. Write prescaler enum value to PR
4. Poll SR until RVU == 0
5. Write reload value (masked to 12 bits) to RLR
6. Write `0xAAAA` to KR (reload counter)
7. Write `0xCCCC` to KR (start watchdog)

Feed writes `0xAAAA` to KR, which reloads the down-counter from RLR.

Register access uses a local `reg()` helper that returns a reference to
a `volatile uint32_t` at the computed address:

```cpp
volatile std::uint32_t &reg(std::uint32_t offset)
{
    return *reinterpret_cast<volatile std::uint32_t *>(kIwdgBase + offset);
}
```

### Zynq-7000 (hal/src/zynq7000/Watchdog.cpp)

No-op stub. The Zynq PS has a System Watchdog Timer (SWDT) at
`0xF8005000`, but it requires SLCR configuration and is not used in the
current PYNQ-Z2 port. Both `watchdogInit` and `watchdogFeed` are empty
functions that satisfy the linker.

## Kernel Integration

### watchdogStart / watchdogRunning (kernel/inc/kernel/Kernel.h)

```cpp
namespace kernel
{
    void watchdogStart(std::uint16_t reloadValue = 4095,
                       std::uint8_t prescaler = 4);
    bool watchdogRunning();
}
```

`watchdogStart` converts the prescaler parameter to `WatchdogPrescaler`
(clamped to 0-6), calls `hal::watchdogInit`, and sets the internal
`s_watchdogEnabled` flag. `watchdogRunning` returns the flag value.

Default parameters: reload=4095, prescaler=4 (Div64), giving a timeout
of approximately 8.2 seconds at 32 kHz LSI.

### Idle Thread Feed

The idle thread loop checks `s_watchdogEnabled` before each WFI:

```cpp
static void idleThreadFunc(void *)
{
    while (true)
    {
        if (s_watchdogEnabled)
        {
            hal::watchdogFeed();
        }
        arch::waitForInterrupt();
    }
}
```

This design provides a system-level health check: if the idle thread
runs, all higher-priority threads are cooperating (sleeping, blocking,
or yielding). If any thread enters an infinite loop or deadlocks at a
priority above idle, the idle thread never executes, the watchdog
counter expires, and the MCU resets.

## Shell Command (wdt)

The `wdt` command queries `kernel::watchdogRunning()` and prints the
status:

```cpp
void cmdWdt()
{
    write("watchdog: ");
    writeLine(kernel::watchdogRunning() ? "active" : "inactive");
}
```

Output examples:

```
ms-os> wdt
watchdog: inactive

ms-os> wdt
watchdog: active
```

The `help` command includes `wdt` in its listing:

```
  wdt     - watchdog status
```

## Test Coverage

### HAL Tests (test/hal/WatchdogTest.cpp) -- 8 tests

| Test | Verifies |
|------|----------|
| `InitRecordsPrescalerAndReload` | Div32 prescaler and reload=1000 recorded |
| `InitRecordsDiv4Prescaler` | Div4 prescaler with max reload=4095 |
| `InitRecordsDiv256Prescaler` | Div256 prescaler with reload=2048 |
| `FeedIncrementsFeedCount` | Single feed increments counter |
| `MultipleFeedsAccumulate` | Three feeds result in count=3 |
| `FeedCountResetsOnSetUp` | `resetMockState()` clears feed counter |
| `MultipleInitsRecordInOrder` | Two successive inits recorded in order |
| `ZeroReloadValueRecorded` | Edge case: reload=0 is stored correctly |

Mock implementation (`test/hal/MockWatchdog.cpp`) records init calls into
`test::g_watchdogInitCalls` (vector of `{prescaler, reloadValue}`) and
increments `test::g_watchdogFeedCount` on each feed.

### Shell Tests (test/kernel/ShellTest.cpp) -- 3 tests

| Test | Verifies |
|------|----------|
| `Wdt_InactiveByDefault` | Output contains "watchdog: inactive" |
| `Wdt_ActiveAfterStart` | After setting `g_watchdogRunning = true`, output contains "watchdog: active" |
| `Help_IncludesWdt` | Help text mentions "wdt" and "watchdog" |

## Files

| File | Purpose |
|------|---------|
| `hal/inc/hal/Watchdog.h` | Public API (WatchdogConfig, watchdogInit, watchdogFeed) |
| `hal/src/stm32f4/Watchdog.cpp` | STM32 IWDG driver (~70 lines) |
| `hal/src/zynq7000/Watchdog.cpp` | Zynq no-op stub |
| `hal/CMakeLists.txt` | Adds Watchdog.cpp to hal library |
| `kernel/inc/kernel/Kernel.h` | watchdogStart / watchdogRunning declarations |
| `kernel/src/core/Kernel.cpp` | Kernel integration (idle feed, s_watchdogEnabled) |
| `kernel/src/core/Shell.cpp` | cmdWdt implementation |
| `test/hal/MockWatchdog.cpp` | Mock recording init calls and feed count |
| `test/hal/MockRegisters.h` | WatchdogInitCall struct, g_watchdogFeedCount |
| `test/hal/WatchdogTest.cpp` | 8 HAL unit tests |
| `test/kernel/MockKernel.h` | g_watchdogRunning mock flag |
| `test/kernel/MockKernelGlobals.cpp` | Mock watchdogStart / watchdogRunning |
| `test/kernel/ShellTest.cpp` | 3 wdt shell tests |
