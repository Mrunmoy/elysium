# Code Review: Crash Dump Three-Layer Refactor

**Commit:** 8eeee3365a97eb13bac1fb40f1dd39eb6cc0feb8
**Date:** 2026-02-21
**Files Reviewed:** 12
**Reviewer:** Embedded Systems Code Review Agent

---

## Summary

This commit refactors the crash dump system from a single monolithic `CrashDump.cpp` into three
architecture layers: a portable common layer (`CrashDumpCommon.cpp`), a CPU-specific arch layer
(`CrashDumpArch.cpp` per arch directory), and a board-specific output layer (`CrashDumpBoard.cpp`
per board directory). A new `MSOS_BOARD_DIR` CMake variable parallels `MSOS_ARCH_DIR` for board
selection. The structural design is clean and the layering goal is achieved. However, the
Cortex-M4 arch file is a byte-for-byte copy of the M3 file (missing M4-specific FPU handling),
the design document was not updated, no unit tests were added for the new code, and the
`FaultInfo` struct fields violate the project naming convention.

---

## Critical Issues

None found.

---

## Major Issues

### CR-001 Cortex-M4 CrashDumpArch.cpp Is Identical to M3 -- FPU Frame Not Handled

- **File:** `kernel/src/arch/cortex-m4/CrashDumpArch.cpp`
- **Line(s):** 1-272 (entire file)
- **Severity:** MAJOR
- **Description:** The M4 file is a byte-for-byte copy of the M3 file. The Cortex-M4 with FPU
  enabled uses an extended exception frame when `EXC_RETURN[4] == 0`: the hardware pushes an
  additional 18 words (S0-S15 + FPSCR + reserved) onto the stack before the standard 8-word
  frame. The current code unconditionally treats `stackFrame[0..7]` as R0-xPSR. If a fault
  occurs in a context where the FPU frame is active, the register values extracted will be
  **wrong** -- each field will be offset by 18 words -- and the `sp` value reported will point
  into the middle of the stacked FPU data rather than the base of the exception frame. The
  `excReturn` argument (`EXC_RETURN`) needed to detect this case is already available but is
  never inspected.

  Additionally, the M4-specific CFSR bit 5 (`MLSPERR` -- lazy FP stacking error) is not decoded
  in `decodeCfsr()`. This means a lazy-stacking fault produces no decoded output beyond the raw
  CFSR hex value.

- **Recommendation:** In `archPopulateFaultInfo()` for M4, inspect `excReturn & (1U << 4)`. If
  that bit is zero (extended FPU frame present), advance `stackFrame` by 18 words before
  indexing. Also add `kMmMlsperr = 1U << 5` and decode it in `decodeCfsr()` with the guard:
  ```cpp
  if (cfsr & kMmMlsperr)
  {
      kernel::faultPrint("  -> MLSPERR: Lazy FP stacking error\r\n");
  }
  ```

---

### CR-002 some structures have `m_` Prefix in member name (Coding Standard Violation)

- **Recommendation:** Rename all struct fields to remove `m_` prefix.
---

### CR-003 Design Document Not Updated to Reflect Three-Layer Architecture

- **File:** `doc/design/CrashDump.md`
- **Line(s):** 163, 174-180, 354, 379-389
- **Severity:** MAJOR
- **Description:** The design document still describes the pre-refactor monolithic architecture.
  Specifically:
  - The architecture diagram (line 163) references the old `CrashDump.cpp`.
  - The "Files" table (lines 174-180) lists only `FaultHandlers.s`, `CrashDump.cpp`, and
    `CrashDump.h` -- the three new files (`CrashDumpCommon.cpp`, `CrashDumpArch.cpp`,
    `CrashDumpBoard.cpp`) and new headers (`CrashDumpArch.h`, `CrashDumpBoard.h`,
    `CrashDumpInternal.h`) are absent.
  - The CMake snippet (lines 379-389) shows the old `src/core/CrashDump.cpp` instead of the
    new three-source structure.
  - Line 354 references `test/kernel/CrashDumpTest.cpp` as a planned test file -- this file
    does not exist and was not created in this commit.

  The `CLAUDE.md` development workflow requires the Document phase to be completed before the
  work is considered done.

- **Recommendation:** Update `doc/design/CrashDump.md` to document the three-layer architecture,
  the new file list, the `FaultInfo` struct interface, the new CMake variables
  (`MSOS_ARCH_DIR`, `MSOS_BOARD_DIR`), and the porting guide for adding a new arch/board.
  Create design documents for kernel components such as `Kernel`, `Thread`, `Scheduler`, etc., to ensure
  we have proper design documents explaining each implementation.
  Keep the phase wise design document separate because that tells us how we are designing the phases of the project.

---

### CR-004 No Unit Tests for Crash Dump Formatting Logic

- **File:** `test/kernel/CMakeLists.txt`, `test/kernel/` (missing `CrashDumpTest.cpp`)
- **Line(s):** N/A
- **Severity:** MAJOR
- **Description:** The `CLAUDE.md` development workflow mandates that every kernel component has
  corresponding unit tests. The crash dump refactoring introduces new formatting logic in
  `CrashDumpCommon.cpp` and new decoding logic in `CrashDumpArch.cpp`, but no test file was
  added. The `MockCrashDump.h` file declares test infrastructure (`g_crashOutput`,
  `g_mockCfsr`, `resetCrashDumpMockState()`) that has existed since an earlier commit but these
  symbols are never defined and the header is never included by any test. This infrastructure
  is dead code.

  The design doc (line 354) describes a `CrashDumpTest.cpp` with specific test cases:
  calling `faultHandlerC()` with known stack frames, asserting register output, asserting fault
  bit decoding, asserting thread context. None of these tests exist.

- **Recommendation:**
  1. Implement `MockCrashDump.h`'s promised infrastructure (define `g_crashOutput`,
     `g_mockCfsr`, `g_mockHfsr`, `g_mockMmfar`, `g_mockBfar`, and `resetCrashDumpMockState()`)
     in `MockCrashDump.cpp`.
  2. Make `boardFaultPutChar()` mock append to `g_crashOutput` instead of being a no-op.
  3. Create `test/kernel/CrashDumpTest.cpp` with tests that:
     - Call `faultHandlerC()` with a known stack frame and assert formatted output.
     - Verify CFSR bit decoding (IACCVIOL, PRECISERR, DIVBYZERO, etc.) for known input values.
     - Verify the crash dump includes thread name and ID when `g_currentTcb` is set.
  4. Add `CrashDumpTest.cpp` and `CrashDumpCommon.cpp` (real, not mock) to `kernel_tests`
     in `test/kernel/CMakeLists.txt`.

---

## Minor Issues

### CR-005 `faultTypeName()` Uses Stacked xPSR to Determine Fault Type (Pre-Existing Bug Carried Over)

- **File:** `kernel/src/arch/cortex-m3/CrashDumpArch.cpp`,
  `kernel/src/arch/cortex-m4/CrashDumpArch.cpp`
- **Line(s):** 172-189, 220
- **Severity:** MINOR (pre-existing bug, not introduced by this commit)
- **Description:** `faultTypeName()` reads `xPSR[7:0]` (IPSR field) from the hardware-stacked
  `xPSR` to identify the fault type (cases 3=HardFault, 4=MemManage, 5=BusFault,
  6=UsageFault). The stacked `xPSR` contains the IPSR of the **interrupted context**, not
  the fault handler. When a fault occurs in Thread mode, the stacked xPSR has IPSR = 0
  (Thread mode), so `faultTypeName()` always returns `"Unknown"` for faults from Thread mode.
  It would only return the correct name if a fault occurred inside an ISR (e.g., a HardFault
  inside IRQ 3 would have stacked xPSR.IPSR = 19).

  This bug was present in the original `CrashDump.cpp` and was carried over unchanged. The
  "Fault:" line in the crash dump output will therefore print `"Unknown"` for the common case
  of a thread-mode fault, even when the fault type is visible in CFSR/HFSR.

- **Recommendation:** Determine fault type from SCB registers rather than stacked xPSR. The
  correct approach is to inspect the HFSR first, then CFSR:
  ```cpp
  const char *faultTypeName(std::uint32_t hfsr, std::uint32_t cfsr)
  {
      if (hfsr & kHfForced)    return "HardFault (forced)";
      if (hfsr & kHfVecttbl)  return "HardFault (vector table)";
      if (cfsr & 0xFF)         return "MemManage";
      if (cfsr & 0xFF00)       return "BusFault";
      if (cfsr & 0xFFFF0000)   return "UsageFault";
      return "HardFault";
  }
  ```
  Alternatively, pass the active IPSR read via `MRS` in the assembly stubs instead of the
  stacked xPSR IPSR.

---

### CR-006 `FaultInfo` Local Variable Is Uninitialized Before `archPopulateFaultInfo()`

- **File:** `kernel/src/core/CrashDumpCommon.cpp`
- **Line(s):** 85-86
- **Severity:** MINOR
- **Description:** `FaultInfo info;` is declared without initialization. Since `FaultInfo`
  is a POD struct, its fields have indeterminate values until `archPopulateFaultInfo()` writes
  them. For Cortex-M3/M4 this is fine because the function sets all fields. However, for a
  future architecture that only populates a subset of the `faultRegNames[4]` array, the loop
  at line 133-136 would pass a garbage pointer to `faultPrint()`, which dereferences it in
  `while (*str)`. On hardware this would trigger a nested fault.

- **Recommendation:** Value-initialize the struct to prevent this hazard for future ports:
  ```cpp
  FaultInfo info{};
  ```
  Note: this zero-initializes pointer fields to `nullptr`. If a future arch leaves
  `faultRegNames[i]` as `nullptr`, `faultPrint(nullptr)` would still crash. Add a count
  field or null-check in the loop:
  ```cpp
  for (int i = 0; i < 4; ++i)
  {
      if (info.m_faultRegNames[i] != nullptr)
      {
          faultPrintReg(info.m_faultRegNames[i], info.m_faultReg[i]);
      }
  }
  ```

---

### CR-007 Line Length Violation (101 Characters)

- **File:** `kernel/src/arch/cortex-m3/CrashDumpArch.cpp`,
  `kernel/src/arch/cortex-m4/CrashDumpArch.cpp`
- **Line(s):** 138 (both files)
- **Severity:** MINOR
- **Description:** The project coding standard requires a 100-column limit. Line 138 is 101
  characters:
  ```
              kernel::faultPrint("  -> INVSTATE: Invalid EPSR.T bit (ARM mode on Thumb-only CPU)\r\n");
  ```

- **Recommendation:** Split the string literal or shorten the message:
  ```cpp
  kernel::faultPrint("  -> INVSTATE: Invalid EPSR.T bit\r\n");
  ```
  or use a continuation:
  ```cpp
  kernel::faultPrint(
      "  -> INVSTATE: Invalid EPSR.T bit (ARM mode on Thumb-only CPU)\r\n");
  ```

---

### CR-008 `reinterpret_cast<std::uint32_t>(pointer)` in CrashDumpCommon.cpp

- **File:** `kernel/src/core/CrashDumpCommon.cpp`
- **Line(s):** 146
- **Severity:** MINOR
- **Description:** The project memory documents the known gotcha: on a 64-bit host,
  `reinterpret_cast<std::uint32_t>(pointer)` truncates the upper 32 bits and is
  technically undefined behavior under the C++ standard. The project convention is to
  cast through `std::uintptr_t` first. While `CrashDumpCommon.cpp` is not compiled for
  host tests (the mock replaces it), the pattern is still wrong idiomatically and would
  fail with `-Werror` on a 64-bit cross-compilation toolchain if the target ABI changes.

  ```cpp
  faultPrintHex(reinterpret_cast<std::uint32_t>(tcb->m_stackBase)); // line 146
  ```

- **Recommendation:** Follow the documented project pattern:
  ```cpp
  faultPrintHex(static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(tcb->m_stackBase)));
  ```

---

### CR-009 `FaultInfo` with Fixed Array of 4 Fault Registers Is Not Portable

- **File:** `kernel/inc/kernel/CrashDumpArch.h`
- **Line(s):** 26-27
- **Severity:** MINOR
- **Description:** `FaultInfo` hardcodes `faultReg[4]` and `faultRegNames[4]`, and the loop
  in `CrashDumpCommon.cpp` hardcodes `i < 4`. Cortex-A9 (the stated next porting target) has
  a completely different fault register structure (DFSR, DFAR, IFSR, IFAR on ARMv7-A -- also
  4 registers, so it happens to fit, but the naming comment "M: CFSR, HFSR, MMFAR, BFAR"
  is already arch-specific). For a future port with more or fewer fault registers, the struct
  and loop must both change simultaneously with no compile-time enforcement.

- **Recommendation:** Add a `numFaultRegs` field:
  ```cpp
  std::uint8_t m_numFaultRegs;  // how many of faultReg/faultRegNames are valid
  ```
  and change the loop to `for (int i = 0; i < info.m_numFaultRegs; ++i)`.

---

### CR-010 `boardFaultFlush()` Called After Every `faultPrint()` Invocation

- **File:** `kernel/src/core/CrashDumpCommon.cpp`
- **Line(s):** 47-54
- **Severity:** MINOR
- **Description:** `faultPrint()` calls `boardFaultFlush()` (wait for TC) after transmitting
  every string argument. `faultPrintReg()` calls `faultPrint()` three times and
  `faultPrintHex()` once, resulting in four TC-wait cycles per register line. At 115200 baud
  this adds roughly ~0.7 ms per `faultPrint()` call beyond the actual character transmission
  time. For a complete crash dump (~13 registers + 4 status regs + 12 text lines), the total
  extra wait exceeds 30 ms. This is harmless but needlessly slow.

- **Recommendation:** Remove `boardFaultFlush()` from `faultPrint()` and call it only once
  in `faultHandlerC()` just before `boardFaultBlink()`:
  ```cpp
  void faultPrint(const char *str)
  {
      while (*str)
      {
          boardFaultPutChar(*str++);
      }
      // Removed: boardFaultFlush();
  }
  ```
  Then add a single `boardFaultFlush()` call at the end of `faultHandlerC()` before
  `boardFaultBlink()` to drain the shift register.

---

### CR-011 `CrashDumpArch.h` Is a Public Header but Contains Internal `FaultInfo` Struct

- **File:** `kernel/inc/kernel/CrashDumpArch.h`
- **Line(s):** 10-12, 18-31
- **Severity:** MINOR
- **Description:** `CrashDumpArch.h` is in the public include directory (`kernel/inc/kernel/`)
  and is included by `MockCrashDump.cpp` (via the public include path). However, `FaultInfo`
  is an implementation detail of the crash dump internal pipeline -- it is not part of any
  public API. Placing it in a public header pollutes the kernel namespace and exposes
  implementation details to consumers. `CrashDumpInternal.h` already exists for internal
  cross-layer declarations but only exposes `faultPrint` and `faultPrintHex`.

- **Recommendation:** Move `FaultInfo` and the `arch*` function declarations to
  `CrashDumpInternal.h`. Since `MockCrashDump.cpp` needs `FaultInfo` for mock stubs,
  add a `PRIVATE` include path in `test/kernel/CMakeLists.txt` pointing to
  `kernel/src/core/` (already done for other internal headers), and include
  `CrashDumpInternal.h` in `MockCrashDump.cpp` instead.

---

### CR-012 `reg()` Helper Function Duplicated in Three Separate Files

- **File:** `kernel/src/arch/cortex-m3/CrashDumpArch.cpp` line 71,
  `kernel/src/arch/cortex-m4/CrashDumpArch.cpp` line 71,
  `kernel/src/board/stm32f207zgt6/CrashDumpBoard.cpp` line 45,
  `kernel/src/board/stm32f407zgt6/CrashDumpBoard.cpp` line 45
- **Line(s):** 71 (arch files), 45 (board files)
- **Severity:** MINOR
- **Description:** The `reg()` helper:
  ```cpp
  volatile std::uint32_t &reg(std::uint32_t addr)
  {
      return *reinterpret_cast<volatile std::uint32_t *>(addr);
  }
  ```
  is copy-pasted into four separate anonymous namespaces. While duplication within anonymous
  namespaces is intentional (no linkage conflicts), it still increases maintenance surface.

- **Recommendation:** Consider extracting this into a shared inline header under
  `kernel/src/core/` or `kernel/inc/kernel/` (e.g., `HwReg.h`). A `constexpr`-friendly
  approach:
  ```cpp
  // kernel/inc/kernel/HwReg.h
  #pragma once
  #include <cstdint>
  namespace kernel
  {
      inline volatile std::uint32_t &hwReg(std::uint32_t addr)
      {
          return *reinterpret_cast<volatile std::uint32_t *>(addr);
      }
  }
  ```

---

## Positive Observations

- **Excellent layering motivation.** The commit message clearly explains the design rationale
  (prepare for Cortex-A9 porting). The three-layer split maps directly to the three axes of
  variation: portable logic, CPU ISA/registers, SoC peripheral layout.

- **No `#ifdef` anywhere.** All platform separation is done via directory structure and CMake
  variable substitution, exactly as mandated by `CLAUDE.md`. This is the correct approach.

- **`MSOS_BOARD_DIR` parallels `MSOS_ARCH_DIR` cleanly.** The CMake pattern is consistent
  and easy to extend. Adding a third board is a one-line change in `CMakeLists.txt`.

- **`boardFaultBlink()` correctly marked `[[noreturn]]`.** The `[[noreturn]]` attribute is
  applied in both the header declaration and both board implementations. The mock implementation
  correctly satisfies the contract with an infinite loop.

- **`boardEnsureOutput()` uses a correct early-exit guard.** Reading `kUsart1Cr1 & kUsartUe`
  before re-initializing UART is the right pattern for an idempotent init function safe to
  call in a fault context.

- **`FaultHandlers.s` EXC_RETURN bit-2 test is correct.** The `tst lr, #4` / `ite eq` /
  `mrseq r0, msp` / `mrsne r0, psp` sequence correctly selects MSP vs PSP based on
  EXC_RETURN[2]. All four handlers are structurally identical and correct.

- **All hardware register addresses and bit positions are correct.** USART1 (0x40011000),
  RCC (0x40023800), GPIOA (0x40020000), GPIOC (0x40020800), CFSR (0xE000ED28), HFSR
  (0xE000ED2C), MMFAR (0xE000ED34), BFAR (0xE000ED38), SHCSR (0xE000ED24), CCR
  (0xE000ED14) -- all verified against the STM32F2/F4 and ARMv7-M reference manuals.

- **`UDIV` divide-by-zero test is correct for both M3 and M4.** The Cortex-M3 does include
  the SDIV/UDIV Thumb-2 instructions. The inline `__asm volatile` prevents the compiler from
  optimizing away the division. The `0xDEFE` undefined instruction encoding is permanently
  undefined per the ARMv7-M ARM. Both test fault mechanisms are solid.

- **`CrashDumpInternal.h` correctly scoped.** Internal helpers (`faultPrint`, `faultPrintHex`)
  live in `kernel/src/core/` (a `PRIVATE` include path in the CMake target) and are not
  exposed through the public `kernel/inc/` tree.

- **Mock completeness.** The updated `MockCrashDump.cpp` provides no-op stubs for all
  symbols from all three new translation units (`CrashDumpCommon.cpp`, `CrashDumpArch.cpp`,
  `CrashDumpBoard.cpp`), ensuring the host test build links cleanly without any of the
  hardware-dependent code.

---

## Statistics

- **Critical:** 0
- **Major:** 4 (CR-001, CR-002, CR-003, CR-004)
- **Minor:** 8 (CR-005 through CR-012)
- **Files reviewed:** 12
- **Lines added:** 1,139
- **Lines removed:** 492
