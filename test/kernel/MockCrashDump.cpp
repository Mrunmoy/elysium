// Mock crash dump implementation for host-side testing.
// Provides no-op stubs for all crash dump functions since these
// access hardware registers that don't exist on x86.

#include "kernel/CrashDump.h"
#include "kernel/CrashDumpArch.h"
#include "kernel/CrashDumpBoard.h"

namespace kernel
{
    void crashDumpInit()
    {
        // No-op on host -- real implementation calls archCrashDumpInit()
    }

    void triggerTestFault(FaultType)
    {
        // No-op on host
    }

    extern "C" void faultHandlerC(std::uint32_t *, std::uint32_t)
    {
        // No-op on host
    }

    void archPopulateFaultInfo(FaultInfo &, std::uint32_t *, std::uint32_t)
    {
        // No-op on host
    }

    void archDecodeFaultBits(const FaultInfo &)
    {
        // No-op on host
    }

    void archCrashDumpInit()
    {
        // No-op on host
    }

    void archTriggerTestFault(FaultType)
    {
        // No-op on host
    }

    void boardEnsureOutput()
    {
        // No-op on host
    }

    void boardFaultPutChar(char)
    {
        // No-op on host
    }

    void boardFaultFlush()
    {
        // No-op on host
    }

    [[noreturn]] void boardFaultBlink()
    {
        // Spin forever on host (matches [[noreturn]] contract)
        while (true)
        {
        }
    }

    void faultPrint(const char *)
    {
        // No-op on host
    }

    void faultPrintHex(std::uint32_t)
    {
        // No-op on host
    }

}  // namespace kernel
