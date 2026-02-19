// Mock crash dump implementation for host-side testing.
// Provides no-op stubs for crashDumpInit() and faultHandlerC() since
// these access hardware registers that don't exist on x86.

#include "kernel/CrashDump.h"

namespace kernel
{
    void crashDumpInit()
    {
        // No-op on host -- real implementation configures SCB registers
    }

    void triggerTestFault(FaultType)
    {
        // No-op on host
    }

    extern "C" void faultHandlerC(std::uint32_t *, std::uint32_t)
    {
        // No-op on host
    }

}  // namespace kernel
