// Mock kernel globals for host-side testing.
// Provides g_currentTcb, g_nextTcb, and kernelThreadExit that are
// normally defined in Kernel.cpp (cross-compile only).

#include "kernel/CortexM.h"
#include "kernel/Thread.h"

namespace kernel
{
    ThreadControlBlock *g_currentTcb = nullptr;
    ThreadControlBlock *g_nextTcb = nullptr;

    void kernelThreadExit()
    {
        // No-op for host tests -- on hardware this terminates the thread
    }
}  // namespace kernel
