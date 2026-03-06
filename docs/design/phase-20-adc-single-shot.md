# Phase 20: ADC Single-Shot Driver

## Goal

Add a minimal register-level ADC HAL for STM32F4 that supports:

- controller initialization (`adcInit`)
- single-shot polled conversion (`adcRead`)
- global status code returns for error paths

This phase intentionally keeps scope small and deterministic for TDD.

## Scope

Included:

- new HAL API header `hal/inc/hal/Adc.h`
- STM32F4 implementation for ADC1/ADC2/ADC3
- RCC ADC clock enable/disable helpers
- host mocks + unit tests for API contract and edge cases
- Zynq stub implementation

Not included:

- continuous conversion mode
- scan sequences longer than one rank
- ADC interrupt mode
- ADC DMA integration (`adcStartContinuous`)

## API

```cpp
void adcInit(const AdcConfig &config);
std::int32_t adcRead(AdcId id, std::uint8_t channel, std::uint16_t *outValue,
                     std::uint32_t timeoutLoops);
```

Status codes:

- `kOk`: conversion completed
- `kInvalid`: invalid id/channel/ptr/timeout argument
- `kTimedOut`: EOC not observed within timeout loop budget

## STM32F4 Mapping

Reference basis:

- STM32F407 reference manual RM0090 (ADC chapter)
- STM32F4 HAL/LL code in `vendor/stm32f4xx-hal-driver/` for bit definitions and sequencing

Registers used:

- ADC base:
  - ADC1 `0x40012000`
  - ADC2 `0x40012100`
  - ADC3 `0x40012200`
- ADC common:
  - `ADC->CCR` via `0x40012300 + 0x04`

Important fields:

- `CR1.RES[25:24]` resolution
- `CR2.ALIGN` alignment
- `CR2.ADON` enable
- `CR2.SWSTART` software trigger
- `SR.EOC` conversion complete
- `SQR1.L` regular sequence length (set to 0 for one conversion)
- `SQR3.SQ1` channel select
- `SMPR1/SMPR2` sample time by channel
- `CCR.ADCPRE` common prescaler (set to PCLK2/4)

RCC bits (APB2ENR):

- ADC1 bit 8
- ADC2 bit 9
- ADC3 bit 10

## Behavior and Edge Cases

- `adcInit` ignores invalid `AdcId`
- `adcRead` rejects:
  - invalid `AdcId`
  - null output pointer
  - channel > 18
  - `timeoutLoops == 0`
- `adcRead` programs one-rank regular conversion each call and polls EOC
- if timeout elapses, returns `kTimedOut` and leaves caller output unchanged by driver logic

## Test Plan

Host tests in `test/hal/AdcTest.cpp`:

- init records full configuration
- invalid init id is ignored
- read records id/channel/timeout
- read propagates success value
- invalid arg matrix (id/null/channel/timeout)
- timeout propagation
- status mapping helper

RCC tests extended:

- ADC clock enable/disable call recording
- mixed call-order check includes ADC

## Hardware Verification (STM32F407, J-Link)

Validated on target with `app/adc-test` using internal ADC channels only
(no external analog wiring):

- channel 17 (`VREFINT`)
- channel 16 (temperature sensor)

Command used:

```bash
python3 build.py -f --target stm32f407zgt6 --app adc-test --probe jlink
```

Observed machine output:

- `MSOS_CASE:adc:vrefint-read:PASS`
- `MSOS_CASE:adc:temp-read:PASS`
- `MSOS_CASE:adc:invalid-channel:PASS`
- `MSOS_SUMMARY:adc:pass=3:total=3:result=PASS`

## Follow-Up Work

- Add continuous mode + DMA (`adcStartContinuous`)
- Integrate ADC results into hardware validation runner
