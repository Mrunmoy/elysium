#pragma once

// SVC syscall numbers and user-space wrapper functions.
//
// Unprivileged threads cannot call kernel APIs directly. Instead they
// execute SVC with the syscall number and arguments in r0-r3.  The
// SVC_Handler extracts the number, calls svcDispatch(), and writes the
// return value back to the caller's stacked r0.
//
// Privileged threads bypass this entirely and call kernel APIs directly.
//
// The kernel::user namespace provides inline wrappers that issue the SVC
// on ARM targets, and fall through to direct kernel calls on the host
// (for testing).

#include <cstdint>

namespace kernel
{

// ---- SVC number constants ----
namespace syscall
{
    static constexpr std::uint8_t kStartFirstThread = 0;
    static constexpr std::uint8_t kYield            = 1;
    static constexpr std::uint8_t kSleep            = 2;
    static constexpr std::uint8_t kTickCount        = 3;

    static constexpr std::uint8_t kMutexCreate      = 4;
    static constexpr std::uint8_t kMutexDestroy     = 5;
    static constexpr std::uint8_t kMutexLock        = 6;
    static constexpr std::uint8_t kMutexTryLock     = 7;
    static constexpr std::uint8_t kMutexUnlock      = 8;

    static constexpr std::uint8_t kSemaphoreCreate  = 9;
    static constexpr std::uint8_t kSemaphoreDestroy = 10;
    static constexpr std::uint8_t kSemaphoreWait    = 11;
    static constexpr std::uint8_t kSemaphoreTryWait = 12;
    static constexpr std::uint8_t kSemaphoreSignal  = 13;

    static constexpr std::uint8_t kMessageSend      = 14;
    static constexpr std::uint8_t kMessageReceive   = 15;
    static constexpr std::uint8_t kMessageReply     = 16;
    static constexpr std::uint8_t kMessageTrySend   = 17;
    static constexpr std::uint8_t kMessageTryReceive = 18;
    static constexpr std::uint8_t kMessageNotify    = 19;
    static constexpr std::uint8_t kMessageCheckNotify = 20;

    static constexpr std::uint8_t kHeapAlloc        = 21;
    static constexpr std::uint8_t kHeapFree         = 22;
    static constexpr std::uint8_t kHeapGetStats     = 23;

    static constexpr std::uint8_t kMaxSyscall       = 23;
}  // namespace syscall

// Forward declarations for host-build fallback
struct Message;
struct HeapStats;

}  // namespace kernel

// ---- User-space syscall wrappers ----
//
// On ARM: inline SVC instructions with register constraints.
// On host: direct calls to kernel APIs (for unit testing).

#ifdef __arm__

namespace kernel::user
{

inline void yield()
{
    __asm volatile("svc %0" : : "I"(syscall::kYield) : "memory");
}

inline void sleep(std::uint32_t ticks)
{
    register std::uint32_t r0 __asm("r0") = ticks;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSleep) : "memory");
}

inline std::uint32_t tickCount()
{
    register std::uint32_t r0 __asm("r0");
    __asm volatile("svc %1" : "=r"(r0) : "I"(syscall::kTickCount) : "memory");
    return r0;
}

// ---- Mutex ----

inline std::uint8_t mutexCreate(const char *name)
{
    register std::uint32_t r0 __asm("r0") = reinterpret_cast<std::uint32_t>(name);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMutexCreate) : "memory");
    return static_cast<std::uint8_t>(r0);
}

inline void mutexDestroy(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMutexDestroy) : "memory");
}

inline bool mutexLock(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMutexLock) : "memory");
    return r0 != 0;
}

inline bool mutexTryLock(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMutexTryLock) : "memory");
    return r0 != 0;
}

inline bool mutexUnlock(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMutexUnlock) : "memory");
    return r0 != 0;
}

// ---- Semaphore ----

inline std::uint8_t semaphoreCreate(std::uint32_t initialCount, std::uint32_t maxCount,
                                     const char *name)
{
    register std::uint32_t r0 __asm("r0") = initialCount;
    register std::uint32_t r1 __asm("r1") = maxCount;
    register std::uint32_t r2 __asm("r2") = reinterpret_cast<std::uint32_t>(name);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSemaphoreCreate),
                   "r"(r1), "r"(r2) : "memory");
    return static_cast<std::uint8_t>(r0);
}

inline void semaphoreDestroy(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSemaphoreDestroy) : "memory");
}

inline bool semaphoreWait(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSemaphoreWait) : "memory");
    return r0 != 0;
}

inline bool semaphoreTryWait(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSemaphoreTryWait) : "memory");
    return r0 != 0;
}

inline bool semaphoreSignal(std::uint8_t id)
{
    register std::uint32_t r0 __asm("r0") = id;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kSemaphoreSignal) : "memory");
    return r0 != 0;
}

// ---- IPC ----

inline std::int32_t messageSend(std::uint8_t dest, const Message &msg, Message *reply)
{
    register std::uint32_t r0 __asm("r0") = dest;
    register std::uint32_t r1 __asm("r1") = reinterpret_cast<std::uint32_t>(&msg);
    register std::uint32_t r2 __asm("r2") = reinterpret_cast<std::uint32_t>(reply);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageSend),
                   "r"(r1), "r"(r2) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::int32_t messageReceive(Message *msg)
{
    register std::uint32_t r0 __asm("r0") = reinterpret_cast<std::uint32_t>(msg);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageReceive) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::int32_t messageReply(std::uint8_t dest, const Message &reply)
{
    register std::uint32_t r0 __asm("r0") = dest;
    register std::uint32_t r1 __asm("r1") = reinterpret_cast<std::uint32_t>(&reply);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageReply),
                   "r"(r1) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::int32_t messageTrySend(std::uint8_t dest, const Message &msg)
{
    register std::uint32_t r0 __asm("r0") = dest;
    register std::uint32_t r1 __asm("r1") = reinterpret_cast<std::uint32_t>(&msg);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageTrySend),
                   "r"(r1) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::int32_t messageTryReceive(Message *msg)
{
    register std::uint32_t r0 __asm("r0") = reinterpret_cast<std::uint32_t>(msg);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageTryReceive) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::int32_t messageNotify(std::uint8_t dest, std::uint32_t bits)
{
    register std::uint32_t r0 __asm("r0") = dest;
    register std::uint32_t r1 __asm("r1") = bits;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kMessageNotify),
                   "r"(r1) : "memory");
    return static_cast<std::int32_t>(r0);
}

inline std::uint32_t messageCheckNotify()
{
    register std::uint32_t r0 __asm("r0");
    __asm volatile("svc %1" : "=r"(r0) : "I"(syscall::kMessageCheckNotify) : "memory");
    return r0;
}

// ---- Heap ----

inline void *heapAlloc(std::uint32_t size)
{
    register std::uint32_t r0 __asm("r0") = size;
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kHeapAlloc) : "memory");
    return reinterpret_cast<void *>(r0);
}

inline void heapFree(void *ptr)
{
    register std::uint32_t r0 __asm("r0") = reinterpret_cast<std::uint32_t>(ptr);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kHeapFree) : "memory");
}

inline void heapGetStats(HeapStats *stats)
{
    register std::uint32_t r0 __asm("r0") = reinterpret_cast<std::uint32_t>(stats);
    __asm volatile("svc %1" : "+r"(r0) : "I"(syscall::kHeapGetStats) : "memory");
}

}  // namespace kernel::user

#else  // Host build: direct kernel calls for testing

#include "kernel/Kernel.h"
#include "kernel/Mutex.h"
#include "kernel/Semaphore.h"
#include "kernel/Ipc.h"
#include "kernel/Heap.h"

namespace kernel::user
{

inline void yield() { kernel::yield(); }
inline void sleep(std::uint32_t ticks) { kernel::sleep(ticks); }
inline std::uint32_t tickCount() { return kernel::tickCount(); }

inline std::uint8_t mutexCreate(const char *name) { return kernel::mutexCreate(name); }
inline void mutexDestroy(std::uint8_t id) { kernel::mutexDestroy(id); }
inline bool mutexLock(std::uint8_t id) { return kernel::mutexLock(id); }
inline bool mutexTryLock(std::uint8_t id) { return kernel::mutexTryLock(id); }
inline bool mutexUnlock(std::uint8_t id) { return kernel::mutexUnlock(id); }

inline std::uint8_t semaphoreCreate(std::uint32_t init, std::uint32_t max, const char *name)
{ return kernel::semaphoreCreate(init, max, name); }
inline void semaphoreDestroy(std::uint8_t id) { kernel::semaphoreDestroy(id); }
inline bool semaphoreWait(std::uint8_t id) { return kernel::semaphoreWait(id); }
inline bool semaphoreTryWait(std::uint8_t id) { return kernel::semaphoreTryWait(id); }
inline bool semaphoreSignal(std::uint8_t id) { return kernel::semaphoreSignal(id); }

inline std::int32_t messageSend(std::uint8_t dest, const Message &msg, Message *reply)
{ return kernel::messageSend(dest, msg, reply); }
inline std::int32_t messageReceive(Message *msg) { return kernel::messageReceive(msg); }
inline std::int32_t messageReply(std::uint8_t dest, const Message &reply)
{ return kernel::messageReply(dest, reply); }
inline std::int32_t messageTrySend(std::uint8_t dest, const Message &msg)
{ return kernel::messageTrySend(dest, msg); }
inline std::int32_t messageTryReceive(Message *msg) { return kernel::messageTryReceive(msg); }
inline std::int32_t messageNotify(std::uint8_t dest, std::uint32_t bits)
{ return kernel::messageNotify(dest, bits); }
inline std::uint32_t messageCheckNotify() { return kernel::messageCheckNotify(); }

inline void *heapAlloc(std::uint32_t size) { return kernel::heapAlloc(size); }
inline void heapFree(void *ptr) { kernel::heapFree(ptr); }
inline void heapGetStats(HeapStats *stats) { if (stats) *stats = kernel::heapGetStats(); }

}  // namespace kernel::user

#endif  // __arm__
