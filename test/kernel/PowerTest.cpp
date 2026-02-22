// Power management tests.
//
// Tests the arch layer power management functions (WFI, sleep-on-exit,
// deep sleep) via the mock arch layer.

#include "kernel/Arch.h"
#include "kernel/Thread.h"
#include "kernel/Scheduler.h"
#include "MockKernel.h"

#include <gtest/gtest.h>

class PowerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetKernelMockState();
    }
};

// ---- WFI (Wait For Interrupt) ----

TEST_F(PowerTest, WaitForInterrupt_IncrementsCounter)
{
    EXPECT_EQ(test::g_wfiCount, 0u);
    kernel::arch::waitForInterrupt();
    EXPECT_EQ(test::g_wfiCount, 1u);
}

TEST_F(PowerTest, WaitForInterrupt_MultipleCallsAccumulate)
{
    kernel::arch::waitForInterrupt();
    kernel::arch::waitForInterrupt();
    kernel::arch::waitForInterrupt();
    EXPECT_EQ(test::g_wfiCount, 3u);
}

// ---- Sleep-On-Exit ----

TEST_F(PowerTest, SleepOnExit_DefaultDisabled)
{
    EXPECT_FALSE(test::g_sleepOnExit);
}

TEST_F(PowerTest, SleepOnExit_EnableSets)
{
    kernel::arch::enableSleepOnExit();
    EXPECT_TRUE(test::g_sleepOnExit);
}

TEST_F(PowerTest, SleepOnExit_DisableClears)
{
    kernel::arch::enableSleepOnExit();
    EXPECT_TRUE(test::g_sleepOnExit);
    kernel::arch::disableSleepOnExit();
    EXPECT_FALSE(test::g_sleepOnExit);
}

// ---- Deep Sleep ----

TEST_F(PowerTest, DeepSleep_DefaultDisabled)
{
    EXPECT_FALSE(test::g_deepSleep);
}

TEST_F(PowerTest, DeepSleep_EnableSets)
{
    kernel::arch::enableDeepSleep();
    EXPECT_TRUE(test::g_deepSleep);
}

TEST_F(PowerTest, DeepSleep_DisableClears)
{
    kernel::arch::enableDeepSleep();
    EXPECT_TRUE(test::g_deepSleep);
    kernel::arch::disableDeepSleep();
    EXPECT_FALSE(test::g_deepSleep);
}

// ---- Combined state ----

TEST_F(PowerTest, SleepOnExit_IndependentOfDeepSleep)
{
    kernel::arch::enableSleepOnExit();
    kernel::arch::enableDeepSleep();
    EXPECT_TRUE(test::g_sleepOnExit);
    EXPECT_TRUE(test::g_deepSleep);

    kernel::arch::disableSleepOnExit();
    EXPECT_FALSE(test::g_sleepOnExit);
    EXPECT_TRUE(test::g_deepSleep);
}

TEST_F(PowerTest, ResetClearsAllPowerState)
{
    kernel::arch::waitForInterrupt();
    kernel::arch::enableSleepOnExit();
    kernel::arch::enableDeepSleep();

    test::resetKernelMockState();

    EXPECT_EQ(test::g_wfiCount, 0u);
    EXPECT_FALSE(test::g_sleepOnExit);
    EXPECT_FALSE(test::g_deepSleep);
}
