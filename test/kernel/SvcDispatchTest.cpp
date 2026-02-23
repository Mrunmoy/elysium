// Tests for SVC dispatch logic.
//
// These tests call svcDispatch() directly with a simulated exception stack
// frame, verifying that each syscall number routes to the correct kernel
// function and returns the expected value.

#include "kernel/Syscall.h"
#include "kernel/Kernel.h"
#include "kernel/Thread.h"
#include "kernel/Mutex.h"
#include "kernel/Semaphore.h"
#include "kernel/Ipc.h"
#include "kernel/Heap.h"
#include "MockKernel.h"

#include <gtest/gtest.h>
#include <cstring>

// The dispatch function under test (defined in SvcDispatch.cpp)
extern "C" std::uint32_t svcDispatch(std::uint8_t svcNum, std::uint32_t *frame);

class SvcDispatchTest : public ::testing::Test
{
protected:
    // Simulated exception frame: [r0, r1, r2, r3, r12, lr, pc, xpsr]
    std::uint32_t m_frame[8];

    // Heap buffer for heap syscall tests
    alignas(8) std::uint8_t m_heapBuf[1024];

    void SetUp() override
    {
        std::memset(m_frame, 0, sizeof(m_frame));
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::mutexReset();
        kernel::semaphoreReset();
        kernel::ipcReset();
    }
};

// ---- Yield (SVC 1) ----

TEST_F(SvcDispatchTest, Yield_CallsKernelYield)
{
    // Need at least one thread for scheduler context
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "test";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId id = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(id);
    kernel::internal::scheduler().switchContext();

    svcDispatch(kernel::syscall::kYield, m_frame);
    EXPECT_FALSE(test::g_contextSwitchTriggers.empty());
}

// ---- Sleep (SVC 2) ----

TEST_F(SvcDispatchTest, Sleep_BlocksCurrentThread)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "test";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId id = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(id);
    kernel::internal::scheduler().switchContext();

    m_frame[0] = 100;  // r0 = ticks
    svcDispatch(kernel::syscall::kSleep, m_frame);

    // Thread should be blocked
    auto *tcb = kernel::threadGetTcb(id);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Blocked);
}

// ---- TickCount (SVC 3) ----

TEST_F(SvcDispatchTest, TickCount_ReturnsCurrentTick)
{
    test::g_tickCount = 42;
    std::uint32_t result = svcDispatch(kernel::syscall::kTickCount, m_frame);
    EXPECT_EQ(result, 42u);
}

// ---- Mutex Create (SVC 4) ----

TEST_F(SvcDispatchTest, MutexCreate_ReturnsValidId)
{
    m_frame[0] = 0;  // name = nullptr
    std::uint32_t result = svcDispatch(kernel::syscall::kMutexCreate, m_frame);
    EXPECT_NE(result, static_cast<std::uint32_t>(kernel::kInvalidMutexId));
}

// ---- Mutex Destroy (SVC 5) ----

TEST_F(SvcDispatchTest, MutexDestroy_DestroysMutex)
{
    kernel::MutexId id = kernel::mutexCreate("test");
    ASSERT_NE(id, kernel::kInvalidMutexId);

    m_frame[0] = id;
    svcDispatch(kernel::syscall::kMutexDestroy, m_frame);

    auto *mcb = kernel::mutexGetBlock(id);
    EXPECT_FALSE(mcb->active);
}

// ---- Mutex Lock (SVC 6) ----

TEST_F(SvcDispatchTest, MutexLock_LocksAndReturnsTrue)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "locker";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::MutexId mid = kernel::mutexCreate("m");
    m_frame[0] = mid;
    std::uint32_t result = svcDispatch(kernel::syscall::kMutexLock, m_frame);
    EXPECT_EQ(result, 1u);

    auto *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->owner, tid);
}

// ---- Mutex TryLock (SVC 7) ----

TEST_F(SvcDispatchTest, MutexTryLock_SucceedsWhenFree)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "trylocker";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::MutexId mid = kernel::mutexCreate("m");
    m_frame[0] = mid;
    std::uint32_t result = svcDispatch(kernel::syscall::kMutexTryLock, m_frame);
    EXPECT_EQ(result, 1u);
}

// ---- Mutex Unlock (SVC 8) ----

TEST_F(SvcDispatchTest, MutexUnlock_UnlocksAndReturnsTrue)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "unlocker";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::MutexId mid = kernel::mutexCreate("m");
    kernel::mutexLock(mid);

    m_frame[0] = mid;
    std::uint32_t result = svcDispatch(kernel::syscall::kMutexUnlock, m_frame);
    EXPECT_EQ(result, 1u);

    auto *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->owner, kernel::kInvalidThreadId);
}

// ---- Semaphore Create (SVC 9) ----

TEST_F(SvcDispatchTest, SemaphoreCreate_ReturnsValidId)
{
    m_frame[0] = 1;   // initialCount
    m_frame[1] = 10;  // maxCount
    m_frame[2] = 0;   // name = nullptr
    std::uint32_t result = svcDispatch(kernel::syscall::kSemaphoreCreate, m_frame);
    EXPECT_NE(result, static_cast<std::uint32_t>(kernel::kInvalidSemaphoreId));
}

// ---- Semaphore Destroy (SVC 10) ----

TEST_F(SvcDispatchTest, SemaphoreDestroy_DestroysSemaphore)
{
    kernel::SemaphoreId id = kernel::semaphoreCreate(1, 10, "test");
    ASSERT_NE(id, kernel::kInvalidSemaphoreId);

    m_frame[0] = id;
    svcDispatch(kernel::syscall::kSemaphoreDestroy, m_frame);

    auto *scb = kernel::semaphoreGetBlock(id);
    EXPECT_FALSE(scb->active);
}

// ---- Semaphore TryWait (SVC 12) ----

TEST_F(SvcDispatchTest, SemaphoreTryWait_DecrementCount)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "semtest";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::SemaphoreId sid = kernel::semaphoreCreate(1, 10, "s");
    m_frame[0] = sid;
    std::uint32_t result = svcDispatch(kernel::syscall::kSemaphoreTryWait, m_frame);
    EXPECT_EQ(result, 1u);

    auto *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);
}

// ---- Semaphore Signal (SVC 13) ----

TEST_F(SvcDispatchTest, SemaphoreSignal_IncrementCount)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(0, 10, "s");
    m_frame[0] = sid;
    std::uint32_t result = svcDispatch(kernel::syscall::kSemaphoreSignal, m_frame);
    EXPECT_EQ(result, 1u);

    auto *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 1u);
}

// ---- MessageTryReceive (SVC 18) ----

TEST_F(SvcDispatchTest, MessageTryReceive_EmptyReturnsError)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "ipctest";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::Message msg;
    m_frame[0] = reinterpret_cast<std::uintptr_t>(&msg);
    std::uint32_t result = svcDispatch(kernel::syscall::kMessageTryReceive, m_frame);
    EXPECT_EQ(static_cast<std::int32_t>(result), kernel::kIpcErrEmpty);
}

// ---- MessageCheckNotify (SVC 20) ----

TEST_F(SvcDispatchTest, MessageCheckNotify_ReturnsZeroWhenNone)
{
    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "notifytest";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    std::uint32_t result = svcDispatch(kernel::syscall::kMessageCheckNotify, m_frame);
    EXPECT_EQ(result, 0u);
}

// ---- Heap Alloc (SVC 21) ----

TEST_F(SvcDispatchTest, HeapAlloc_ReturnsNonNull)
{
    kernel::heapInit(m_heapBuf, m_heapBuf + sizeof(m_heapBuf));

    m_frame[0] = 64;  // size
    std::uint32_t result = svcDispatch(kernel::syscall::kHeapAlloc, m_frame);
    EXPECT_NE(result, 0u);

    // Clean up
    kernel::heapFree(reinterpret_cast<void *>(static_cast<std::uintptr_t>(result)));
    kernel::heapReset();
}

// ---- Heap Free (SVC 22) ----

TEST_F(SvcDispatchTest, HeapFree_FreesAllocatedBlock)
{
    kernel::heapInit(m_heapBuf, m_heapBuf + sizeof(m_heapBuf));

    void *ptr = kernel::heapAlloc(64);
    ASSERT_NE(ptr, nullptr);

    // On ARM frame[0] holds the full pointer. On 64-bit host we call
    // heapFree directly since the SVC frame is only 32-bit wide.
    kernel::heapFree(ptr);

    auto stats = kernel::heapGetStats();
    EXPECT_EQ(stats.allocCount, 0u);

    kernel::heapReset();
}

// ---- Heap GetStats (SVC 23) ----

TEST_F(SvcDispatchTest, HeapGetStats_FillsStatsStruct)
{
    kernel::heapInit(m_heapBuf, m_heapBuf + sizeof(m_heapBuf));

    // Call heapGetStats directly rather than through the 32-bit SVC
    // frame, since pointers may be 64-bit on the host.
    auto stats = kernel::heapGetStats();

    EXPECT_GT(stats.totalSize, 0u);
    EXPECT_EQ(stats.allocCount, 0u);

    kernel::heapReset();
}

// ---- Invalid SVC number ----

TEST_F(SvcDispatchTest, InvalidSvcNumber_ReturnsZero)
{
    std::uint32_t result = svcDispatch(255, m_frame);
    EXPECT_EQ(result, 0u);
}

// ---- Handler-mode tests (SVC runs in handler mode on ARM) ----
//
// On Cortex-M, SVC_Handler runs in handler mode where ICSR.VECTACTIVE != 0,
// so inIsrContext() returns true. Blocking kernel functions (sleep, mutexLock,
// semaphoreWait, messageSend) check inIsrContext() and early-return as no-ops
// from ISR context. The g_inSyscall flag set by svcDispatch() tells
// inIsrContext() to return false, allowing these functions to block on behalf
// of the calling thread.
//
// These tests simulate handler mode (g_isrContext=true) and verify that
// syscalls through svcDispatch() still work correctly.

TEST_F(SvcDispatchTest, Sleep_InHandlerMode_ViaDispatch_BlocksThread)
{
    test::g_isrContext = true;  // SVC handler runs in handler mode

    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "sleeptest";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId id = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(id);
    kernel::internal::scheduler().switchContext();

    m_frame[0] = 100;  // r0 = ticks
    svcDispatch(kernel::syscall::kSleep, m_frame);

    // Thread should be blocked despite running in handler mode
    auto *tcb = kernel::threadGetTcb(id);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Blocked);
}

TEST_F(SvcDispatchTest, Sleep_InIsrContext_DirectCall_IsNoop)
{
    test::g_isrContext = true;  // Simulate true ISR context (not SVC)

    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "sleepnoop";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId id = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(id);
    kernel::internal::scheduler().switchContext();

    // Direct call to sleep (NOT through svcDispatch) -- should be a no-op
    // because g_inSyscall is false and g_isrContext is true
    kernel::sleep(100);

    auto *tcb = kernel::threadGetTcb(id);
    EXPECT_NE(tcb->state, kernel::ThreadState::Blocked);
}

TEST_F(SvcDispatchTest, MutexLock_InHandlerMode_ViaDispatch_Succeeds)
{
    test::g_isrContext = true;  // SVC handler runs in handler mode

    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "mlock";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::MutexId mid = kernel::mutexCreate("m");
    m_frame[0] = mid;
    std::uint32_t result = svcDispatch(kernel::syscall::kMutexLock, m_frame);

    // mutexLock should succeed (return true) despite handler mode
    EXPECT_EQ(result, 1u);

    auto *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->owner, tid);
}

TEST_F(SvcDispatchTest, SemaphoreWait_InHandlerMode_ViaDispatch_Succeeds)
{
    test::g_isrContext = true;  // SVC handler runs in handler mode

    kernel::internal::scheduler().init();
    alignas(512) static std::uint32_t stack[128];
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "swait";
    cfg.stack = stack;
    cfg.stackSize = sizeof(stack);
    cfg.priority = 16;
    cfg.privileged = true;
    kernel::ThreadId tid = kernel::threadCreate(cfg);
    kernel::internal::scheduler().addThread(tid);
    kernel::internal::scheduler().switchContext();

    kernel::SemaphoreId sid = kernel::semaphoreCreate(1, 10, "s");
    m_frame[0] = sid;
    std::uint32_t result = svcDispatch(kernel::syscall::kSemaphoreWait, m_frame);

    // semaphoreWait should succeed (return true) despite handler mode
    EXPECT_EQ(result, 1u);

    auto *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);
}

TEST_F(SvcDispatchTest, SvcDispatch_ClearsInSyscallOnReturn)
{
    // Verify g_inSyscall is false before and after dispatch
    EXPECT_FALSE(kernel::g_inSyscall);
    svcDispatch(kernel::syscall::kYield, m_frame);
    EXPECT_FALSE(kernel::g_inSyscall);
}
