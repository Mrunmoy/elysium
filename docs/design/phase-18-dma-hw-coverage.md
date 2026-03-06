# Phase 18: DMA Hardware Coverage Expansion

## Goal

Expand on-target DMA validation beyond smoke checks to cover:

- transfer width variants (`byte`, `halfword`, `word`)
- address/alignment usage patterns
- increment mode behavior (`source fixed`, `dest increment`)
- repeatability and stop-path behavior
- machine-parseable output compatible with `tools/hw_driver_runner.py`

## Scope

This phase updates only `app/dma-test` firmware behavior and documentation.
No DMA HAL API changes are introduced.

## Test Matrix

`app/dma-test` now runs 9 hardware-backed cases on STM32F407 DMA2 Stream0:

1. `config-idle`
2. `memcpy-byte-1`
3. `memcpy-byte-64`
4. `memcpy-halfword-32`
5. `memcpy-word-16`
6. `fixed-source-fill-48`
7. `memcpy-byte-unaligned-31`
8. `repeatability-20x32`
9. `stop-after-start`

Each case emits:

- `MSOS_CASE:dma:<case-name>:PASS|FAIL`

And one summary line:

- `MSOS_SUMMARY:dma:pass=<n>:total=<m>:result=PASS|FAIL`

## Implementation Notes

- Added reusable DMA helpers in `app/dma-test/main.cpp`:
  - per-case DMA reconfiguration (`configureDma(...)`)
  - bounded transfer completion wait (`runTransfer(...)`)
  - deterministic pattern generation and verification
- Kept UART console output human-readable while preserving machine parsing contract.

## Hardware Verification

Validated via host runner:

```bash
python3 tools/hw_driver_runner.py \
  --target stm32f407zgt6 \
  --drivers dma \
  --board1-probe jlink \
  --board2-probe stlink \
  --board1-port /dev/ttyUSB0 \
  --timeout 40 \
  --skip-build \
  --register-trace
```

Observed result on target:

- `MSOS_SUMMARY:dma:pass=9:total=9:result=PASS`

## Artifacts Updated

- `app/dma-test/main.cpp`
- `docs/design/phase-18-dma-hw-coverage.md`
- `README.md`
