# Development Tasks

## In Progress: Global Error Codes

- [x] Create shared global error-code header (`common/inc/msos/ErrorCode.h`).
- [x] Wire shared header include paths into kernel/HAL/test builds.
- [x] Migrate kernel IPC status constants to shared global codes.
- [x] Add I2C-to-global-status mapping helper.
- [x] Add baseline tests for IPC/global-code wiring and I2C mapping.
- [x] Define canonical code ownership/rules (which layer returns which codes).
- [x] Roll out global status usage to additional modules (scheduler, sync primitives, shell commands, drivers).
- [x] Add docs section in `README.md` describing global error semantics.

## Next (Do Not Forget): Hardware-Backed Driver Validation

- [x] Add on-target smoke tests per driver (`uart`, `spi`, `i2c`, `dma`) with machine-parseable PASS/FAIL.
- [x] Build a host runner that flashes two STM32F407 boards, captures UART logs, and asserts expected results.
- [x] Add negative/error-path hardware scenarios (NACK, wrong address, no peer, timeout).
- [ ] Add nightly hardware test stage in CI (or local scheduled run script if CI hardware is unavailable).
- [x] Add optional register-trace logging mode for failure diagnostics.

## In Progress: DMA + Peripheral Integration

- [x] DMA-driven SPI transfers (`spiTransferDma`) with host tests and board-to-board hardware validation.
- [ ] DMA-driven UART TX/RX.
- [ ] DMA-driven ADC continuous conversion.

## In Progress: ADC Driver

- [x] Create design doc (`docs/design/phase-20-adc-single-shot.md`).
- [x] Add HAL API: `adcInit`, `adcRead` (single-shot, polled).
- [x] Implement STM32F4 ADC1/ADC2/ADC3 single-shot driver.
- [x] Add ADC RCC clock helpers (`rccEnableAdcClock`, `rccDisableAdcClock`).
- [x] Add host tests and mocks (`test/hal/AdcTest.cpp`, `test/hal/MockAdc.cpp`).
- [x] Add Zynq ADC stub for cross-target build parity.
- [x] Add on-target ADC hardware validation app/cases.
