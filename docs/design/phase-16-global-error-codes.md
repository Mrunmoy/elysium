# Phase 16: Global Error Codes

## Goal

Define one canonical error-code contract shared by kernel, HAL, and applications, similar to Linux `errno`.

## Canonical Status Rules

- Success is `msos::error::kOk` (`0`).
- All failures are negative integers.
- Positive return values are reserved for non-status APIs (for example, counts or IDs).
- Shared codes live in `common/inc/msos/ErrorCode.h`.

## Ownership Rules by API Shape

1. Status APIs (`std::int32_t`) return only canonical global codes.
2. Legacy bool APIs may stay bool short-term, but boundary layers must translate with
   `msos::error::boolToStatus(...)`.
3. Legacy handle/ID APIs may keep sentinel returns short-term, but boundary layers must translate with
   `msos::error::handleToStatus(...)`.
4. Domain-specific codes are allowed only when generic codes cannot capture the fault
   (example: `kNoAck` for I2C NACK).

## Initial Enforcement (This Change)

- Added canonical helpers in `ErrorCode.h`:
  - `isCanonicalStatus(...)`
  - `boolToStatus(...)`
  - `handleToStatus(...)`
- Added tests in `test/kernel/ErrorCodeTest.cpp` so policy behavior is pinned.

## Rollout Plan

1. Convert scheduler/sync/shell and driver-facing status returns to global `std::int32_t` status APIs.
2. Keep compatibility wrappers for existing bool/ID call sites while migrating.
3. Remove compatibility wrappers once all public APIs are status-based.
