// SVC dispatch: routes syscall numbers to kernel functions.
//
// Called from SVC_Handler assembly with:
//   svcNum  -- the SVC immediate byte (1-23)
//   frame   -- pointer to the exception stack frame [r0,r1,r2,r3,r12,lr,pc,xpsr]
//
// Returns a uint32_t that the assembly writes back into stacked r0.
// SVC 0 (startFirstThread) is handled entirely in assembly and never reaches here.

#include "kernel/Syscall.h"
#include "kernel/Arch.h"
#include "kernel/Kernel.h"
#include "kernel/Mutex.h"
#include "kernel/Semaphore.h"
#include "kernel/Ipc.h"
#include "kernel/Heap.h"

#include <cstdint>

extern "C" std::uint32_t svcDispatch(std::uint8_t svcNum, std::uint32_t *frame)
{
    kernel::arch::setSyscallContext(true);

    // frame[0]=r0, frame[1]=r1, frame[2]=r2, frame[3]=r3
    std::uint32_t result = 0;
    switch (svcNum)
    {
    case kernel::syscall::kYield:
        kernel::yield();
        break;

    case kernel::syscall::kSleep:
        kernel::sleep(frame[0]);
        break;

    case kernel::syscall::kTickCount:
        result = kernel::tickCount();
        break;

    // ---- Mutex ----

    case kernel::syscall::kMutexCreate:
        result = kernel::mutexCreate(reinterpret_cast<const char *>(
            static_cast<std::uintptr_t>(frame[0])));
        break;

    case kernel::syscall::kMutexDestroy:
        kernel::mutexDestroy(static_cast<kernel::MutexId>(frame[0]));
        break;

    case kernel::syscall::kMutexLock:
        result = kernel::mutexLock(static_cast<kernel::MutexId>(frame[0])) ? 1u : 0u;
        break;

    case kernel::syscall::kMutexTryLock:
        result = kernel::mutexTryLock(static_cast<kernel::MutexId>(frame[0])) ? 1u : 0u;
        break;

    case kernel::syscall::kMutexUnlock:
        result = kernel::mutexUnlock(static_cast<kernel::MutexId>(frame[0])) ? 1u : 0u;
        break;

    // ---- Semaphore ----

    case kernel::syscall::kSemaphoreCreate:
        result = kernel::semaphoreCreate(
            frame[0], frame[1],
            reinterpret_cast<const char *>(static_cast<std::uintptr_t>(frame[2])));
        break;

    case kernel::syscall::kSemaphoreDestroy:
        kernel::semaphoreDestroy(static_cast<kernel::SemaphoreId>(frame[0]));
        break;

    case kernel::syscall::kSemaphoreWait:
        result = kernel::semaphoreWait(static_cast<kernel::SemaphoreId>(frame[0])) ? 1u : 0u;
        break;

    case kernel::syscall::kSemaphoreTryWait:
        result = kernel::semaphoreTryWait(static_cast<kernel::SemaphoreId>(frame[0])) ? 1u : 0u;
        break;

    case kernel::syscall::kSemaphoreSignal:
        result = kernel::semaphoreSignal(static_cast<kernel::SemaphoreId>(frame[0])) ? 1u : 0u;
        break;

    // ---- IPC ----

    case kernel::syscall::kMessageSend:
        result = static_cast<std::uint32_t>(kernel::messageSend(
            static_cast<kernel::ThreadId>(frame[0]),
            *reinterpret_cast<const kernel::Message *>(static_cast<std::uintptr_t>(frame[1])),
            reinterpret_cast<kernel::Message *>(static_cast<std::uintptr_t>(frame[2]))));
        break;

    case kernel::syscall::kMessageReceive:
        result = static_cast<std::uint32_t>(kernel::messageReceive(
            reinterpret_cast<kernel::Message *>(static_cast<std::uintptr_t>(frame[0]))));
        break;

    case kernel::syscall::kMessageReply:
        result = static_cast<std::uint32_t>(kernel::messageReply(
            static_cast<kernel::ThreadId>(frame[0]),
            *reinterpret_cast<const kernel::Message *>(static_cast<std::uintptr_t>(frame[1]))));
        break;

    case kernel::syscall::kMessageTrySend:
        result = static_cast<std::uint32_t>(kernel::messageTrySend(
            static_cast<kernel::ThreadId>(frame[0]),
            *reinterpret_cast<const kernel::Message *>(static_cast<std::uintptr_t>(frame[1]))));
        break;

    case kernel::syscall::kMessageTryReceive:
        result = static_cast<std::uint32_t>(kernel::messageTryReceive(
            reinterpret_cast<kernel::Message *>(static_cast<std::uintptr_t>(frame[0]))));
        break;

    case kernel::syscall::kMessageNotify:
        result = static_cast<std::uint32_t>(kernel::messageNotify(
            static_cast<kernel::ThreadId>(frame[0]), frame[1]));
        break;

    case kernel::syscall::kMessageCheckNotify:
        result = kernel::messageCheckNotify();
        break;

    // ---- Heap ----

    case kernel::syscall::kHeapAlloc:
    {
        void *ptr = kernel::heapAlloc(frame[0]);
        result = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ptr));
        break;
    }

    case kernel::syscall::kHeapFree:
        kernel::heapFree(reinterpret_cast<void *>(static_cast<std::uintptr_t>(frame[0])));
        break;

    case kernel::syscall::kHeapGetStats:
    {
        auto *stats = reinterpret_cast<kernel::HeapStats *>(
            static_cast<std::uintptr_t>(frame[0]));
        if (stats != nullptr)
        {
            *stats = kernel::heapGetStats();
        }
        break;
    }

    default:
        break;
    }

    kernel::arch::setSyscallContext(false);
    return result;
}
