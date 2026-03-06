# Phase 17: Hardware Driver Validation Framework

## Goal

Build a repeatable hardware-backed validation flow for STM32F407 driver bring-up with:

- on-target smoke tests for `uart`, `spi`, `i2c`, `dma`
- machine-parseable serial output (`PASS/FAIL` + summary)
- host-side runner that flashes boards, captures logs, and asserts outcomes
- negative-path scenarios for I2C (`NACK`, `wrong address`, `no peer`, timeout-oriented case)
- optional register-trace diagnostics mode

## Scope

Included in this phase:

1. Firmware test output contract for machine parsing
2. Runner script for two-board orchestration
3. Negative-path scenario handling
4. Optional trace logging mode for diagnostics

Not included:

- CI/nightly integration stage (handled later)

## Machine-Parseable Output Contract

Each hardware smoke app emits these lines on console UART:

- Per-case:
  - `MSOS_CASE:<driver>:<case-name>:PASS`
  - `MSOS_CASE:<driver>:<case-name>:FAIL`
- Summary:
  - `MSOS_SUMMARY:<driver>:pass=<n>:total=<m>:result=PASS`
  - `MSOS_SUMMARY:<driver>:pass=<n>:total=<m>:result=FAIL`

Human-readable logs remain for manual debugging.

## Driver Coverage

## `uart`

- Existing board-to-board `uart2-test` remains primary smoke test.
- Output is extended with machine-parseable case and summary lines.

## `spi`

- Existing board-to-board `spi2-test` remains primary smoke test.
- Output is extended with machine-parseable case and summary lines.

## `i2c`

- Existing board-to-board `i2c-test` remains primary smoke test.
- Add explicit negative-path cases:
  - wrong address
  - no peer
  - timeout-oriented transfer attempt
- Output includes error/status details plus machine-parseable lines.

## `dma`

- Add dedicated `dma-test` app for hardware smoke and diagnostics output.
- Uses the same machine-parseable contract.

## Host Runner Design

`tools/hw_driver_runner.py`:

- flashes Board 2 service firmware then Board 1 test firmware
- opens serial for Board 1 (and optional Board 2 capture)
- parses `MSOS_CASE` / `MSOS_SUMMARY`
- enforces expected outcomes per scenario
- returns non-zero on failure for automation

Runner scenarios:

- positive path: all smoke tests must pass
- negative path: selected cases must fail with expected error class

## Optional Register-Trace Mode

Runner flag:

- `--register-trace`

Behavior:

- stores full raw serial logs under a timestamped artifact directory
- includes additional failure context in runner output
- intended for debug sessions and post-mortem analysis

## Testing Strategy

1. Unit tests for runner parsing and scenario evaluation in `test/tools/`
2. Host C++ tests remain mandatory (`python3 build.py -t`)
3. Cross-compile checks for all targets remain mandatory
4. Hardware execution is run by the host runner with explicit serial result parsing

## Artifacts Updated In This Phase

- `app/uart2-test/main.cpp`
- `app/spi2-test/main.cpp`
- `app/i2c-test/main.cpp`
- `app/dma-test/main.cpp` (+ CMake)
- `tools/hw_driver_runner.py`
- `test/tools/test_hw_driver_runner.py`
- `README.md`
- `docs/TASKS.md`

## Implementation Summary

Completed implementation includes:

- machine-parseable case/summary lines added to `uart2-test`, `spi2-test`, `i2c-test`
- new `dma-test` hardware smoke app using the same machine output contract
- host runner `tools/hw_driver_runner.py` with:
  - board flashing orchestration (Board 2 service then Board 1 test)
  - serial capture and parser/validator
  - non-zero failure exit behavior for automation
  - optional `--register-trace` raw log artifacts
- runner unit tests in `test/tools/test_hw_driver_runner.py`
- explicit I2C negative-path coverage (`wrong-address-nack`, `no-peer-timeout`)

## Debug Findings And Resolution

During on-target bring-up, UART scenario initially failed to emit summary lines due to
`WFI`-based waiting in `uart2-test` timeout logic. In the absence of a guaranteed wake
interrupt source, timeout progress could stall indefinitely.

Resolution:

- `app/uart2-test/main.cpp`: removed blocking `WFI` path in `waitForEcho(...)`
- replaced with bounded polling/spin so timeout always progresses and result lines are emitted

This fix ensures runner-visible PASS/FAIL behavior even when UART peer link is absent.

## Hardware Verification Snapshot

Verified on STM32F407 dual-board setup (Board 1 J-Link, Board 2 ST-Link), with
board-to-board links connected:

- `uart`: PASS (`5/5`)
- `spi`: PASS (`5/5`)
- `i2c`: PASS (`8/8`, includes BME680 chip ID check and negative-path cases)
- `dma`: PASS (`3/3`, DMA2 memory-to-memory: config-idle, memcpy-64, stop-after-start)

Host validation completed for this phase:

- host C++ tests (`python3 build.py -t`) pass
- cross-compile passes for `stm32f207zgt6`, `stm32f407zgt6`, `pynq-z2`
- runner parser tests pass (`test/tools/test_hw_driver_runner.py`)
