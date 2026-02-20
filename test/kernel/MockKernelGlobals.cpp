// Mock kernel globals for host-side testing.
// Provides g_currentTcb, g_nextTcb, kernelThreadExit, and the
// internal::scheduler() accessor that Mutex.cpp/Semaphore.cpp need.

#include "kernel/CortexM.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"

namespace kernel
{
    ThreadControlBlock *g_currentTcb = nullptr;
    ThreadControlBlock *g_nextTcb = nullptr;

    void kernelThreadExit()
    {
        // No-op for host tests -- on hardware this terminates the thread
    }

    namespace internal
    {
        static Scheduler s_testScheduler;

        Scheduler &scheduler()
        {
            return s_testScheduler;
        }
    }  // namespace internal

}  // namespace kernel
