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
