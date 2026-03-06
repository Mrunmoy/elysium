# Phase: RTC (Real-Time Clock)

## Goal

Register-level HAL driver for the STM32F4 RTC peripheral, providing calendar time
(get/set), alarm configuration with callback, and wakeup timer support. Zynq stub
for cross-platform builds.

## Background

### STM32F407 RTC

The RTC sits in the backup domain, powered by VBAT when VDD is off. It has its own
32.768 kHz crystal oscillator (LSE) and maintains calendar time in BCD-encoded
registers. The backup domain requires special unlock sequences before writes.

### Clock Source

- **LSE (Low Speed External):** 32.768 kHz crystal, most accurate
- **LSI (Low Speed Internal):** ~32 kHz RC, less accurate, no crystal needed
- **HSE/128:** External high-speed divided, unusual choice

Default: LSE with PREDIV_A=127, PREDIV_S=255 -> 1 Hz tick.

```
ck_spre = 32768 / ((127 + 1) * (255 + 1)) = 32768 / 32768 = 1 Hz
```

### Register Map (RM0090 Section 26.6)

Base address: 0x40002800

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | TR | Time register (BCD: HH:MM:SS) |
| 0x04 | DR | Date register (BCD: YY:MM:DD + weekday) |
| 0x08 | CR | Control register |
| 0x0C | ISR | Init/status register |
| 0x10 | PRER | Prescaler (async + sync) |
| 0x14 | WUTR | Wakeup timer reload value |
| 0x1C | ALRMAR | Alarm A register |
| 0x20 | ALRMBR | Alarm B register |
| 0x24 | WPR | Write protection register |
| 0x28 | SSR | Sub-second register |

### BCD Encoding

Time register (TR):
- Bits [23:20]: Hour tens (0-2)
- Bits [19:16]: Hour units (0-9)
- Bits [15:12]: Minute tens (0-5)
- Bits [11:8]: Minute units (0-9)
- Bits [7:4]: Second tens (0-5)
- Bits [3:0]: Second units (0-9)

Date register (DR):
- Bits [23:20]: Year tens (0-9)
- Bits [19:16]: Year units (0-9)
- Bits [15:13]: Weekday (1-7, 1=Monday)
- Bit [12]: Month tens (0-1)
- Bits [11:8]: Month units (0-9)
- Bits [7:4]: Day tens (0-3)
- Bits [3:0]: Day units (0-9)

### Write Protection

RTC registers are write-protected. Unlock sequence:
1. Write 0xCA to WPR
2. Write 0x53 to WPR

Any wrong key re-enables protection immediately.

### Backup Domain Access

Before touching RTC or BDCR, must:
1. Enable PWR clock: RCC_APB1ENR bit 28
2. Set DBP in PWR_CR (bit 8) to allow backup domain writes
3. Configure BDCR: enable LSE, select RTC clock source, enable RTC

### Init Mode

To write TR/DR/PRER:
1. Unlock WPR (0xCA, 0x53)
2. Set ISR.INIT (bit 7)
3. Wait for ISR.INITF (bit 6) = 1
4. Write registers
5. Clear ISR.INIT
6. Re-lock WPR (write any wrong key)

### Alarm Configuration

ALRMAR/ALRMBR layout matches TR with added MSK bits:
- MSK1 (bit 7): mask seconds
- MSK2 (bit 14): mask minutes
- MSK3 (bit 23): mask hours
- MSK4 (bit 31): mask date/weekday

All MSK=1: alarm fires every second.
All MSK=0: alarm fires when all fields match.

### NVIC

| Source | IRQn | Notes |
|--------|------|-------|
| RTC Alarm | 41 | Alarm A and B (via EXTI line 17) |
| RTC Wakeup | 3 | Wakeup timer (via EXTI line 22) |

EXTI lines must be configured for rising edge + interrupt mask.

## Scope

### In Scope
1. RTC init with LSI clock source (no external crystal dependency)
2. Get/set time (hours, minutes, seconds)
3. Get/set date (year, month, day, weekday)
4. Alarm A with callback (match on any combination of H:M:S)
5. Alarm cancel
6. BCD encode/decode helpers
7. Host tests with link-time mocks
8. Hardware test on STM32F407
9. Zynq stub

### Out of Scope
- LSE support (requires crystal on board, board-specific)
- Alarm B (identical to A, trivial to add later)
- Wakeup timer (deferred)
- Timestamp / tamper detection
- Sub-second access
- Smooth calibration
- Backup registers

## API Design

```cpp
namespace hal
{
    enum class RtcClockSource : std::uint8_t
    {
        Lsi = 0,  // Internal ~32 kHz RC
        Lse,      // External 32.768 kHz crystal
    };

    struct RtcTime
    {
        std::uint8_t hours;    // 0-23
        std::uint8_t minutes;  // 0-59
        std::uint8_t seconds;  // 0-59
    };

    struct RtcDate
    {
        std::uint8_t year;     // 0-99 (offset from 2000)
        std::uint8_t month;    // 1-12
        std::uint8_t day;      // 1-31
        std::uint8_t weekday;  // 1-7 (1=Monday)
    };

    struct RtcAlarmConfig
    {
        std::uint8_t hours;    // 0-23
        std::uint8_t minutes;  // 0-59
        std::uint8_t seconds;  // 0-59
        bool maskHours;        // true = ignore hours
        bool maskMinutes;      // true = ignore minutes
        bool maskSeconds;      // true = ignore seconds
        bool maskDate;         // true = ignore date (always true for basic alarm)
    };

    using RtcAlarmCallbackFn = void (*)(void *arg);

    void rtcInit(RtcClockSource clockSource);
    void rtcSetTime(const RtcTime &time);
    void rtcGetTime(RtcTime &time);
    void rtcSetDate(const RtcDate &date);
    void rtcGetDate(RtcDate &date);
    void rtcSetAlarm(const RtcAlarmConfig &config, RtcAlarmCallbackFn callback, void *arg);
    void rtcCancelAlarm();
    bool rtcIsReady();
}
```

## Implementation Details

### Init Sequence

```
rtcInit(Lsi):
  1. RCC_APB1ENR |= (1 << 28)        // Enable PWR clock
  2. PWR_CR |= (1 << 8)              // Set DBP (backup domain access)
  3. RCC_BDCR |= (1 << 1) [LSI: RCC_CSR |= (1 << 0)]  // Enable clock
  4. Wait for ready flag
  5. RCC_BDCR: RTCSEL = 10 (LSI), RTCEN = 1
  6. Unlock WPR (0xCA, 0x53)
  7. Enter init mode (ISR.INIT = 1, wait INITF)
  8. Set PRER: PREDIV_A=127, PREDIV_S=249 (for LSI ~32kHz)
  9. Set 24-hour format (CR.FMT = 0)
  10. Exit init mode (clear ISR.INIT)
  11. Lock WPR (write 0xFF)
```

Note: LSI is ~32 kHz (not exactly 32768 Hz). PREDIV_S=249 with PREDIV_A=127 gives
32000 / (128 * 250) = 1.0 Hz. The exact LSI frequency varies per chip.

For LSE: PREDIV_A=127, PREDIV_S=255 (standard 32768 Hz crystal).

### LSI vs LSE Clock Enable

LSI is enabled via RCC_CSR (not BDCR):
- RCC_CSR base: RCC + 0x74
- Bit 0: LSION
- Bit 1: LSIRDY

LSE is enabled via RCC_BDCR:
- Bit 0: LSEON
- Bit 1: LSERDY

### EXTI Configuration for Alarm

RTC Alarm uses EXTI line 17:
- EXTI_IMR bit 17 = 1 (unmask)
- EXTI_RTSR bit 17 = 1 (rising edge)
- Clear pending: EXTI_PR bit 17 = 1

EXTI base: 0x40013C00

### BCD Helpers

```cpp
std::uint8_t toBcd(std::uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

std::uint8_t fromBcd(std::uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}
```

## File Layout

### New Files
- `hal/inc/hal/Rtc.h` -- public API header
- `hal/src/stm32f4/Rtc.cpp` -- STM32F4 register-level implementation
- `hal/src/zynq7000/Rtc.cpp` -- Zynq stub
- `test/hal/MockRtc.cpp` -- mock for host tests
- `test/hal/RtcTest.cpp` -- host unit tests
- `app/rtc-test/main.cpp` -- hardware test app
- `app/rtc-test/CMakeLists.txt`

### Modified Files
- `hal/CMakeLists.txt` -- add Rtc.cpp
- `test/hal/CMakeLists.txt` -- add MockRtc.cpp, RtcTest.cpp
- `test/hal/MockRegisters.h` -- add RTC mock state
- `CMakeLists.txt` -- add rtc-test subdirectory

## Hardware Test Plan

Single-board test on STM32F407 (Board 1, J-Link):

1. **Init and ready**: rtcInit(Lsi), verify rtcIsReady() returns true
2. **Set/get time**: set 14:30:00, read back, verify match
3. **Set/get date**: set 2026-03-06 (Friday), read back, verify
4. **Time advances**: set time, wait ~3 seconds, read back, verify seconds advanced
5. **Alarm fires**: set alarm to match seconds=3, verify callback fires within 5s

Console: USART1 on PA9, machine-parseable MSOS_CASE/MSOS_SUMMARY output.

## Hardware Test Results

All 5/5 PASS on STM32F407ZGT6 (Board 1, J-Link, 2026-03-06):

```
=== RTC Hardware Test ===

[1/5] Init and ready
  rtcIsReady() = true
MSOS_CASE:rtc:init_ready:PASS
[2/5] Set/get time
  Set: 14:30:00  Got: 14:30:00
MSOS_CASE:rtc:set_get_time:PASS
[3/5] Set/get date
  Set: 2026-03-06 (wd=5)  Got: 2026-03-06 (wd=5)
MSOS_CASE:rtc:set_get_date:PASS
[4/5] Time advances
  Waiting ~3 seconds...
  After wait: 12:00:03
MSOS_CASE:rtc:time_advances:PASS
[5/5] Alarm fires
  Alarm set for :03, waiting...
  Time when checked: 10:00:03
  Alarm fired: yes
MSOS_CASE:rtc:alarm_fires:PASS

MSOS_SUMMARY:rtc:5/5:PASS
```

### Gotcha: timerDelayUs requires TIM7 init

`timerDelayUs()` needs TIM7 fully initialized with `timerInit()` (PSC=83, ARR=0xFFFF
for 1 MHz at 84 MHz APB1), not just `rccEnableTimerClock()`. Without init, the delay
returns instantly and time-dependent tests fail silently.
