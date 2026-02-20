// Architecture-agnostic fault information for crash dumps.
//
// Defines the FaultInfo struct that arch-specific code populates and
// portable code consumes. Each architecture provides four functions:
//   archPopulateFaultInfo -- fill FaultInfo from hardware state
//   archDecodeFaultBits   -- print decoded fault register meanings
//   archCrashDumpInit     -- enable fault detection hardware
//   archTriggerTestFault  -- trigger a test fault (arch-specific instructions)

#pragma once

#include "kernel/CrashDump.h"

#include <cstdint>

namespace kernel
{
    struct FaultInfo
    {
        std::uint32_t pc;
        std::uint32_t lr;
        std::uint32_t sp;
        std::uint32_t r0, r1, r2, r3, r12;
        std::uint32_t statusReg;       // xPSR (Cortex-M) or CPSR (Cortex-A)

        std::uint32_t faultReg[4];     // M: CFSR, HFSR, MMFAR, BFAR
        const char *faultRegNames[4];  // "CFSR ", "HFSR ", etc.

        const char *faultType;         // "HardFault", "DataAbort", etc.
        std::uint32_t excInfo;         // EXC_RETURN (M) or exception mode (A)
    };

    // Arch-specific: populate FaultInfo from hardware state.
    void archPopulateFaultInfo(FaultInfo &info, std::uint32_t *stackFrame,
                               std::uint32_t excReturn);

    // Arch-specific: decode fault register bits, printing via faultPrint.
    void archDecodeFaultBits(const FaultInfo &info);

    // Arch-specific: initialize fault detection hardware.
    void archCrashDumpInit();

    // Arch-specific: trigger a test fault using CPU-specific instructions.
    void archTriggerTestFault(FaultType type);

}  // namespace kernel
