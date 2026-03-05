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
