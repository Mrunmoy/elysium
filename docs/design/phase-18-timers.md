# Phase 18: Hardware Timer Abstraction

## Goal

Provide a register-level HAL driver for STM32F4 hardware timers, covering:

- Basic timers (TIM6, TIM7) for periodic interrupts and microsecond delays
- General-purpose timers (TIM2-TIM5) for PWM output and input capture
- Portable API with Zynq stub for cross-platform builds

## Background

### STM32F407 Timer Inventory

The STM32F407 has 14 timers across three categories:

| Category | Timers | Bus | Counter | Channels | Features |
|---|---|---|---|---|---|
| Advanced | TIM1, TIM8 | APB2 | 16-bit | 4 | Complementary outputs, break, dead-time |
| General-purpose | TIM2, TIM5 | APB1 | 32-bit | 4 | Full capture/compare, PWM, encoder |
| General-purpose | TIM3, TIM4 | APB1 | 16-bit | 4 | Same as above but 16-bit counter |
| Basic | TIM6, TIM7 | APB1 | 16-bit | 0 | Update event only, DAC trigger |
| Low-power | TIM9-TIM14 | APB1/APB2 | 16-bit | 1-2 | Reduced feature set |

### Timer Clock Sources

From RM0090 Section 7.2 (Clocks):

- APB1 timers (TIM2-7, TIM12-14): if APB1 prescaler != 1, timer clock = APB1 x 2
  - F407: APB1 = 42 MHz, prescaler = /4, so timer clock = **84 MHz**
  - F207: APB1 = 30 MHz, prescaler = /4, so timer clock = **60 MHz**
- APB2 timers (TIM1, TIM8-11): if APB2 prescaler != 1, timer clock = APB2 x 2
  - F407: APB2 = 84 MHz, prescaler = /2, so timer clock = **168 MHz**
  - F207: APB2 = 60 MHz, prescaler = /2, so timer clock = **120 MHz**

### Register Map (TIM_TypeDef, RM0090 Section 18.4)

All timers share the same register layout (some fields reserved for basic timers):

| Offset | Register | Description |
|---|---|---|
| 0x00 | CR1 | Control register 1 (CEN, UDIS, URS, OPM, DIR, CMS, ARPE) |
| 0x04 | CR2 | Control register 2 (MMS, CCDS) |
| 0x08 | SMCR | Slave mode control (not used for basic timers) |
| 0x0C | DIER | DMA/interrupt enable (UIE, CCxIE, TIE, UDE) |
| 0x10 | SR | Status register (UIF, CCxIF, TIF, CCxOF) |
| 0x14 | EGR | Event generation (UG) |
| 0x18 | CCMR1 | Capture/compare mode register 1 |
| 0x1C | CCMR2 | Capture/compare mode register 2 |
| 0x20 | CCER | Capture/compare enable register |
| 0x24 | CNT | Counter |
| 0x28 | PSC | Prescaler (16-bit, divides by PSC+1) |
| 0x2C | ARR | Auto-reload register |
| 0x30 | RCR | Repetition counter (advanced timers only) |
| 0x34 | CCR1 | Capture/compare register 1 |
| 0x38 | CCR2 | Capture/compare register 2 |
| 0x3C | CCR3 | Capture/compare register 3 |
| 0x40 | CCR4 | Capture/compare register 4 |
| 0x44 | BDTR | Break and dead-time (advanced timers only) |

### Key Register Bits (RM0090 Section 18.3)

**CR1:**
- Bit 0: CEN (counter enable)
- Bit 1: UDIS (update disable)
- Bit 2: URS (update request source: 0=any, 1=overflow only)
- Bit 3: OPM (one-pulse mode: stop after one update)
- Bit 4: DIR (direction: 0=up, 1=down)
- Bit 7: ARPE (auto-reload preload enable)

**DIER:**
- Bit 0: UIE (update interrupt enable)
- Bit 1-4: CC1IE-CC4IE (capture/compare interrupt enable)

**SR:**
- Bit 0: UIF (update interrupt flag, cleared by software writing 0)
- Bit 1-4: CC1IF-CC4IF (capture/compare flags)

**CCMR1 (output compare mode for channels 1-2):**
- Bits 6:4 (OC1M): output compare mode (000=frozen, 110=PWM mode 1, 111=PWM mode 2)
- Bit 3 (OC1PE): output compare preload enable

**CCER:**
- Bit 0: CC1E (capture/compare 1 output enable)
- Bit 1: CC1P (capture/compare 1 polarity)

### NVIC IRQ Numbers (STM32F407)

| Timer | IRQn | Notes |
|---|---|---|
| TIM2 | 28 | Single global IRQ |
| TIM3 | 29 | Single global IRQ |
| TIM4 | 30 | Single global IRQ |
| TIM5 | 50 | Single global IRQ |
| TIM6 | 54 | Shared with DAC underrun |
| TIM7 | 55 | Single global IRQ |
| TIM1 | 24-27 | 4 separate IRQs (BRK, UP, TRG_COM, CC) |
| TIM8 | 43-46 | 4 separate IRQs (BRK, UP, TRG_COM, CC) |

## Scope

### In Scope (Phase 18)

1. **Basic timer** (TIM6, TIM7): periodic interrupt with callback
2. **General-purpose timer** (TIM2-TIM5): PWM output on a channel
3. **Microsecond delay** utility using a basic timer
4. **RCC clock enable/disable** for timers
5. **ISR handlers** for TIM2-TIM7
6. Host tests with link-time mocks
7. Hardware test app on STM32F407
8. Zynq stubs

### Out of Scope (Future)

- Advanced timers (TIM1, TIM8) with complementary outputs, break, dead-time
- Input capture mode
- Encoder interface mode
- DMA-driven timer operations
- Low-power timers (TIM9-TIM14)
- Kernel software timers (timer wheel / callback scheduler)

## API Design

### Timer Identification

```cpp
namespace hal
{
    enum class TimerId : std::uint8_t
    {
        Tim2 = 0,
        Tim3,
        Tim4,
        Tim5,
        Tim6,
        Tim7
    };

    constexpr std::uint32_t kTimerCount = 6;

    enum class TimerChannel : std::uint8_t
    {
        Ch1 = 0,
        Ch2,
        Ch3,
        Ch4
    };
}
```

### Timer Configuration

```cpp
namespace hal
{
    using TimerCallbackFn = void (*)(void *arg);

    struct TimerConfig
    {
        TimerId id;
        std::uint16_t prescaler;    // Divides timer clock by (prescaler + 1)
        std::uint32_t period;       // Auto-reload value (ARR)
        bool autoReload = true;     // ARPE: buffer ARR writes
        bool onePulse = false;      // OPM: stop after one update
    };

    struct PwmConfig
    {
        TimerId id;
        TimerChannel channel;
        std::uint16_t prescaler;
        std::uint32_t period;       // ARR: determines PWM frequency
        std::uint32_t duty;         // CCRx: determines duty cycle
        bool activeHigh = true;     // Output polarity
    };
}
```

### Public API

```cpp
namespace hal
{
    // Basic timer: init, start with periodic interrupt, stop
    void timerInit(const TimerConfig &config);
    void timerStart(TimerId id, TimerCallbackFn callback, void *arg);
    void timerStop(TimerId id);

    // Counter access
    std::uint32_t timerGetCount(TimerId id);
    void timerSetCount(TimerId id, std::uint32_t count);

    // Period/prescaler update (takes effect at next update event if ARPE=1)
    void timerSetPeriod(TimerId id, std::uint32_t period);
    void timerSetPrescaler(TimerId id, std::uint16_t prescaler);

    // PWM output
    void timerPwmInit(const PwmConfig &config);
    void timerPwmStart(TimerId id, TimerChannel channel);
    void timerPwmStop(TimerId id, TimerChannel channel);
    void timerPwmSetDuty(TimerId id, TimerChannel channel, std::uint32_t duty);

    // Microsecond delay (blocking, uses TIM7)
    void timerDelayUs(std::uint32_t us);

    // RCC
    void rccEnableTimerClock(TimerId id);
    void rccDisableTimerClock(TimerId id);
}
```

## Implementation Details

### Timer Clock Calculation

To get a desired frequency:

```
timer_clock = APB_timer_clock  (84 MHz for APB1 timers on F407)
tick_freq   = timer_clock / (PSC + 1)
overflow_freq = tick_freq / (ARR + 1)
```

Example: 1 kHz interrupt on TIM6 (F407):
- PSC = 83 -> tick_freq = 84 MHz / 84 = 1 MHz
- ARR = 999 -> overflow_freq = 1 MHz / 1000 = 1 kHz

Example: 1 MHz tick for microsecond delay on TIM7 (F407):
- PSC = 83 -> tick_freq = 1 MHz
- ARR = 0xFFFF (free-running), read CNT for elapsed time

### Init Sequence (Basic Timer)

From RM0090 Section 18.3.2:

1. Enable timer clock via RCC (APB1ENR or APB2ENR)
2. Set PSC (prescaler)
3. Set ARR (auto-reload value)
4. Set CR1.ARPE if auto-reload preload desired
5. Generate update event (EGR.UG = 1) to load shadow registers
6. Clear SR.UIF (the UG bit sets UIF)
7. Enable update interrupt (DIER.UIE = 1)
8. Enable NVIC IRQ
9. Start counter (CR1.CEN = 1)

### Init Sequence (PWM Output)

From RM0090 Section 18.3.9:

1. Enable timer clock and GPIO clock
2. Configure GPIO pin as alternate function (AF1 for TIM2, AF2 for TIM3/4/5)
3. Set PSC and ARR
4. Configure CCMR1/CCMR2: set OCxM = 110 (PWM mode 1), set OCxPE (preload)
5. Set CCRx = duty cycle value
6. Enable output: CCER.CCxE = 1
7. Generate update event (EGR.UG = 1)
8. Start counter (CR1.CEN = 1)

### ISR Implementation

Each timer ISR:
1. Check SR.UIF (update interrupt flag)
2. Clear SR.UIF by writing 0 to it
3. Invoke the registered callback

```cpp
// Internal state per timer
struct TimerState
{
    TimerCallbackFn callback;
    void *arg;
};

static TimerState s_timerState[kTimerCount];
```

### GPIO Alternate Function Mapping

From STM32F407 datasheet Table 9 (AF mapping):

| Timer | Channel | AF | Common Pins |
|---|---|---|---|
| TIM2 | CH1 | AF1 | PA0, PA5, PA15 |
| TIM2 | CH2 | AF1 | PA1, PB3 |
| TIM2 | CH3 | AF1 | PA2, PB10 |
| TIM2 | CH4 | AF1 | PA3, PB11 |
| TIM3 | CH1 | AF2 | PA6, PB4, PC6 |
| TIM3 | CH2 | AF2 | PA7, PB5, PC7 |
| TIM3 | CH3 | AF2 | PB0, PC8 |
| TIM3 | CH4 | AF2 | PB1, PC9 |
| TIM4 | CH1 | AF2 | PB6, PD12 |
| TIM4 | CH2 | AF2 | PB7, PD13 |
| TIM4 | CH3 | AF2 | PB8, PD14 |
| TIM4 | CH4 | AF2 | PB9, PD15 |
| TIM5 | CH1 | AF2 | PA0, PH10 |
| TIM5 | CH2 | AF2 | PA1, PH11 |
| TIM5 | CH3 | AF2 | PA2, PH12 |
| TIM5 | CH4 | AF2 | PA3, PI0 |

Note: TIM6 and TIM7 have no output channels (basic timers).

### Microsecond Delay Implementation

Uses TIM7 as a free-running 1 MHz counter:

```
timerDelayUs(us):
    1. timerSetCount(Tim7, 0)
    2. while timerGetCount(Tim7) < us: nop
```

For delays > 65535 us (16-bit counter overflow), loop in 65000 us chunks.

TIM7 is initialized once at startup with PSC = (timer_clock_MHz - 1), ARR = 0xFFFF, no interrupt.

### RCC Clock Enable

APB1 timers (TIM2-TIM7): set bit in `RCC->APB1ENR`
APB2 timers (TIM1, TIM8-TIM11): set bit in `RCC->APB2ENR`

| Timer | RCC Register | Bit |
|---|---|---|
| TIM2 | APB1ENR | 0 |
| TIM3 | APB1ENR | 1 |
| TIM4 | APB1ENR | 2 |
| TIM5 | APB1ENR | 3 |
| TIM6 | APB1ENR | 4 |
| TIM7 | APB1ENR | 5 |

## File Layout

### New Files

- `hal/inc/hal/Timer.h` -- public API header
- `hal/src/stm32f4/Timer.cpp` -- STM32F4 register-level implementation
- `hal/src/zynq/Timer.cpp` -- Zynq stub (no-op)
- `hal/src/stm32f4/TimerRcc.cpp` -- RCC enable/disable for timers
- `test/hal/MockTimer.cpp` -- mock for host tests
- `test/hal/TimerTest.cpp` -- host unit tests
- `app/timer-test/main.cpp` -- hardware smoke test app
- `app/timer-test/CMakeLists.txt`

### Modified Files

- `hal/inc/hal/Rcc.h` -- add `rccEnableTimerClock` / `rccDisableTimerClock`
- `hal/CMakeLists.txt` -- add Timer.cpp, TimerRcc.cpp
- `test/hal/CMakeLists.txt` -- add MockTimer.cpp, TimerTest.cpp
- `app/CMakeLists.txt` -- add timer-test subdirectory

## Hardware Test Plan

Single-board test on STM32F407 (Board 1, J-Link):

1. **TIM6 periodic interrupt**: configure 1 kHz, count callbacks for 1 second, verify ~1000
2. **TIM7 microsecond delay**: measure delay accuracy using TIM6 as reference
3. **TIM3 PWM output**: 1 kHz PWM on PB4 (TIM3_CH1, AF2), verify with scope or LED brightness
4. **Start/stop**: verify timer stops counting after timerStop

Machine-parseable output using MSOS_CASE/MSOS_SUMMARY contract.

Console: USART1 on PA9 (as usual).

## Testing Strategy

Host tests follow the link-time mock substitution pattern:

- `MockTimer.cpp` records calls into global vectors
- `TimerTest.cpp` verifies:
  - Init records config (prescaler, period, ARR preload)
  - Start records callback registration
  - Stop clears state
  - PWM init/start/stop/setDuty record correct channel operations
  - Counter get/set work on mock state
  - RCC enable/disable record correct timer IDs
  - Invalid timer ID is handled gracefully

## Hardware Verification Results

Tested on STM32F407ZGT6 (Board 1, J-Link), 2026-03-06.

```
=== Timer Hardware Test (Phase 18) ===

  TIM6 callback count: 1085 (expected ~1000)
Test 1: TIM6 periodic interrupt (1 kHz): PASS
  TIM6 reference ticks during 10ms delay: 10000 (expected ~10000)
Test 2: TIM7 microsecond delay (10 ms): PASS
  TIM3 PWM started and stopped (PB4, AF2)
Test 3: TIM3 PWM output (PB4, 1 kHz): PASS
  LED blinking at 1 Hz on PC13 for 5 seconds...
  TIM2 callback count: 10 (expected ~10)
Test 4: LED blink via TIM2 (1 Hz, PC13): PASS
  TIM5 running count: 65, stopped: 66 -> 66
Test 5: TIM5 start/stop: PASS

MSOS_SUMMARY:timer:pass=5:total=5:result=PASS
```

Notes:
- TIM6 count of 1085 vs expected 1000 reflects ~8.5% overshoot from the busy-loop calibration (not timer error)
- TIM7 delay measurement was exactly 10000 ticks, confirming perfect 1 MHz tick rate at PSC=83
- TIM2 LED blink: exactly 10 callbacks at 2 Hz over 5 seconds, LED visually blinks at 1 Hz on PC13
- TIM5 start/stop confirmed counter freezes after CEN=0

### Logic Analyzer Verification (Saleae, 4 MHz sample rate on PB4)

| Expected | Measured Freq | Error | Measured Duty | Jitter |
|---|---|---|---|---|
| 1 kHz, 50% | 999.89 Hz | 0.01% | 50.00% | 0.250 us (at measurement floor) |
| 10 kHz, 25% | 9998.86 Hz | 0.01% | 25.00% | 0.250 us |
| 100 Hz, 75% | 99.84 Hz | 0.16% | 75.04% | steady-state exact |
| ISR toggle ~10 kHz | 9998.86 Hz | 0.01% | 50.00% | 0.500 us |

All PWM frequencies verified to <0.2% accuracy. Jitter at or below the 0.25 us measurement
resolution (1 sample at 4 MHz), confirming hardware timer generates cycle-accurate waveforms.
