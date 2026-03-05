#include <gtest/gtest.h>

#include "kernel/Kernel.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"
#include "hal/Watchdog.h"
#include "startup/SystemClock.h"

#include "MockCrashDump.h"
#include "MockKernel.h"
#include "MockMpu.h"

namespace
{
    void dummyThread(void *arg)
    {
        (void)arg;
    }

    alignas(512) std::uint32_t g_stackA[128];
    alignas(512) std::uint32_t g_stackB[128];
}  // namespace

class KernelIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetKernelMockState();
        test::resetCrashDumpMockState();
        test::resetMpuMockState();
        kernel::init();
    }

    kernel::ThreadId createThread(const char *name, std::uint32_t *stack, std::uint8_t priority)
    {
        return kernel::createThread(dummyThread, nullptr, name, stack, sizeof(g_stackA), priority);
    }

    void makeSchedulerCurrent()
    {
        kernel::ThreadId id = kernel::internal::scheduler().switchContext();
        kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
        kernel::g_currentTcb = tcb;
        kernel::g_nextTcb = tcb;
    }
};

TEST_F(KernelIntegrationTest, StartScheduler_ConfiguresSysTickAndCurrentTcb)
{
    kernel::ThreadId worker = createThread("worker", g_stackA, 5);
    ASSERT_NE(worker, kernel::kInvalidThreadId);

    kernel::startScheduler();

    kernel::ThreadControlBlock *workerTcb = kernel::threadGetTcb(worker);
    ASSERT_NE(workerTcb, nullptr);
    EXPECT_EQ(kernel::g_currentTcb, workerTcb);
    EXPECT_EQ(kernel::g_nextTcb, workerTcb);
    ASSERT_FALSE(test::g_sysTickConfigs.empty());
    EXPECT_EQ(test::g_sysTickConfigs.back().ticks, SystemCoreClock / 1000);
    EXPECT_TRUE(test::g_schedulerStarted);
}

TEST_F(KernelIntegrationTest, SysTickHandler_WakesSleepingThreadAndPreempts)
{
    kernel::ThreadId high = createThread("high", g_stackA, 5);
    kernel::ThreadId low = createThread("low", g_stackB, 20);
    ASSERT_NE(high, kernel::kInvalidThreadId);
    ASSERT_NE(low, kernel::kInvalidThreadId);

    makeSchedulerCurrent();
    ASSERT_EQ(kernel::internal::scheduler().currentThreadId(), high);

    // High-priority thread sleeps and yields CPU to low-priority thread.
    kernel::sleep(1);
    kernel::ThreadControlBlock *highTcb = kernel::threadGetTcb(high);
    kernel::ThreadControlBlock *lowTcb = kernel::threadGetTcb(low);
    ASSERT_NE(highTcb, nullptr);
    ASSERT_NE(lowTcb, nullptr);
    ASSERT_EQ(highTcb->state, kernel::ThreadState::Blocked);
    ASSERT_EQ(kernel::internal::scheduler().currentThreadId(), low);

    test::g_contextSwitchTriggers.clear();
    std::uint32_t before = kernel::tickCount();
    SysTick_Handler();

    EXPECT_EQ(kernel::tickCount(), before + 1);
    EXPECT_EQ(kernel::internal::scheduler().currentThreadId(), high);
    EXPECT_EQ(highTcb->state, kernel::ThreadState::Running);
    EXPECT_EQ(lowTcb->state, kernel::ThreadState::Ready);
    EXPECT_EQ(kernel::g_nextTcb, highTcb);
    EXPECT_FALSE(test::g_contextSwitchTriggers.empty());
}

TEST_F(KernelIntegrationTest, WatchdogStart_InitializesHalAndMarksRunning)
{
    EXPECT_FALSE(kernel::watchdogRunning());
    EXPECT_TRUE(test::g_watchdogInitCalls.empty());

    kernel::watchdogStart(1234, 99);

    EXPECT_TRUE(kernel::watchdogRunning());
    ASSERT_EQ(test::g_watchdogInitCalls.size(), 1u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 1234u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].prescaler, hal::WatchdogPrescaler::Div256);
}
