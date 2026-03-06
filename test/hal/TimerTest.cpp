#include <gtest/gtest.h>

#include "hal/Timer.h"

#include "MockRegisters.h"

class TimerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(TimerTest, InitRecordsConfig)
{
    hal::TimerConfig config{};
    config.id = hal::TimerId::Tim6;
    config.prescaler = 83;
    config.period = 999;
    config.autoReload = true;
    config.onePulse = false;

    hal::timerInit(config);

    ASSERT_EQ(test::g_timerInitCalls.size(), 1u);
    EXPECT_EQ(test::g_timerInitCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim6));
    EXPECT_EQ(test::g_timerInitCalls[0].prescaler, 83u);
    EXPECT_EQ(test::g_timerInitCalls[0].period, 999u);
    EXPECT_TRUE(test::g_timerInitCalls[0].autoReload);
    EXPECT_FALSE(test::g_timerInitCalls[0].onePulse);
}

TEST_F(TimerTest, InitOnePulseMode)
{
    hal::TimerConfig config{};
    config.id = hal::TimerId::Tim7;
    config.prescaler = 0;
    config.period = 100;
    config.onePulse = true;

    hal::timerInit(config);

    ASSERT_EQ(test::g_timerInitCalls.size(), 1u);
    EXPECT_TRUE(test::g_timerInitCalls[0].onePulse);
}

TEST_F(TimerTest, InitInvalidIdIgnored)
{
    hal::TimerConfig config{};
    config.id = static_cast<hal::TimerId>(99);

    hal::timerInit(config);

    EXPECT_EQ(test::g_timerInitCalls.size(), 0u);
}

// ---- Start / Stop ----

TEST_F(TimerTest, StartRecordsCallback)
{
    bool called = false;
    auto cb = [](void *arg) { *static_cast<bool *>(arg) = true; };

    hal::timerStart(hal::TimerId::Tim6, cb, &called);

    ASSERT_EQ(test::g_timerStartCalls.size(), 1u);
    EXPECT_EQ(test::g_timerStartCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim6));
    EXPECT_TRUE(test::g_timerStartCalls[0].hasCallback);
    EXPECT_NE(test::g_timerCallback, nullptr);
}

TEST_F(TimerTest, StartWithNullCallback)
{
    hal::timerStart(hal::TimerId::Tim2, nullptr, nullptr);

    ASSERT_EQ(test::g_timerStartCalls.size(), 1u);
    EXPECT_FALSE(test::g_timerStartCalls[0].hasCallback);
}

TEST_F(TimerTest, StartInvalidIdIgnored)
{
    hal::timerStart(static_cast<hal::TimerId>(99), nullptr, nullptr);

    EXPECT_EQ(test::g_timerStartCalls.size(), 0u);
}

TEST_F(TimerTest, StopIncrementsCount)
{
    hal::timerStop(hal::TimerId::Tim6);

    EXPECT_EQ(test::g_timerStopCount, 1u);
}

TEST_F(TimerTest, StopClearsCallback)
{
    auto cb = [](void *) {};
    hal::timerStart(hal::TimerId::Tim6, cb, nullptr);
    EXPECT_NE(test::g_timerCallback, nullptr);

    hal::timerStop(hal::TimerId::Tim6);
    EXPECT_EQ(test::g_timerCallback, nullptr);
}

TEST_F(TimerTest, StopInvalidIdIgnored)
{
    hal::timerStop(static_cast<hal::TimerId>(99));

    EXPECT_EQ(test::g_timerStopCount, 0u);
}

// ---- Counter access ----

TEST_F(TimerTest, GetCountReturnsValue)
{
    test::g_timerCount = 42;
    EXPECT_EQ(hal::timerGetCount(hal::TimerId::Tim2), 42u);
}

TEST_F(TimerTest, GetCountInvalidIdReturnsZero)
{
    test::g_timerCount = 42;
    EXPECT_EQ(hal::timerGetCount(static_cast<hal::TimerId>(99)), 0u);
}

TEST_F(TimerTest, SetCountUpdatesValue)
{
    hal::timerSetCount(hal::TimerId::Tim2, 1000);
    EXPECT_EQ(test::g_timerSetCountVal, 1000u);
    EXPECT_EQ(test::g_timerCount, 1000u);
}

TEST_F(TimerTest, SetCountInvalidIdIgnored)
{
    hal::timerSetCount(static_cast<hal::TimerId>(99), 1000);
    EXPECT_EQ(test::g_timerSetCountVal, 0u);
}

// ---- Period / Prescaler update ----

TEST_F(TimerTest, SetPeriodRecordsValue)
{
    hal::timerSetPeriod(hal::TimerId::Tim3, 4999);
    EXPECT_EQ(test::g_timerSetPeriodVal, 4999u);
}

TEST_F(TimerTest, SetPeriodInvalidIdIgnored)
{
    hal::timerSetPeriod(static_cast<hal::TimerId>(99), 4999);
    EXPECT_EQ(test::g_timerSetPeriodVal, 0u);
}

TEST_F(TimerTest, SetPrescalerRecordsValue)
{
    hal::timerSetPrescaler(hal::TimerId::Tim4, 167);
    EXPECT_EQ(test::g_timerSetPrescalerVal, 167u);
}

TEST_F(TimerTest, SetPrescalerInvalidIdIgnored)
{
    hal::timerSetPrescaler(static_cast<hal::TimerId>(99), 167);
    EXPECT_EQ(test::g_timerSetPrescalerVal, 0u);
}

// ---- PWM ----

TEST_F(TimerTest, PwmInitRecordsConfig)
{
    hal::PwmConfig config{};
    config.id = hal::TimerId::Tim3;
    config.channel = hal::TimerChannel::Ch1;
    config.prescaler = 83;
    config.period = 999;
    config.duty = 500;
    config.activeHigh = true;

    hal::timerPwmInit(config);

    ASSERT_EQ(test::g_pwmInitCalls.size(), 1u);
    EXPECT_EQ(test::g_pwmInitCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim3));
    EXPECT_EQ(test::g_pwmInitCalls[0].channel, static_cast<std::uint8_t>(hal::TimerChannel::Ch1));
    EXPECT_EQ(test::g_pwmInitCalls[0].prescaler, 83u);
    EXPECT_EQ(test::g_pwmInitCalls[0].period, 999u);
    EXPECT_EQ(test::g_pwmInitCalls[0].duty, 500u);
    EXPECT_TRUE(test::g_pwmInitCalls[0].activeHigh);
}

TEST_F(TimerTest, PwmInitInvalidIdIgnored)
{
    hal::PwmConfig config{};
    config.id = static_cast<hal::TimerId>(99);

    hal::timerPwmInit(config);

    EXPECT_EQ(test::g_pwmInitCalls.size(), 0u);
}

TEST_F(TimerTest, PwmStartRecordsAction)
{
    hal::timerPwmStart(hal::TimerId::Tim3, hal::TimerChannel::Ch2);

    ASSERT_EQ(test::g_pwmChannelActions.size(), 1u);
    EXPECT_EQ(test::g_pwmChannelActions[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim3));
    EXPECT_EQ(test::g_pwmChannelActions[0].channel,
              static_cast<std::uint8_t>(hal::TimerChannel::Ch2));
    EXPECT_TRUE(test::g_pwmChannelActions[0].start);
}

TEST_F(TimerTest, PwmStopRecordsAction)
{
    hal::timerPwmStop(hal::TimerId::Tim4, hal::TimerChannel::Ch3);

    ASSERT_EQ(test::g_pwmChannelActions.size(), 1u);
    EXPECT_EQ(test::g_pwmChannelActions[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim4));
    EXPECT_EQ(test::g_pwmChannelActions[0].channel,
              static_cast<std::uint8_t>(hal::TimerChannel::Ch3));
    EXPECT_FALSE(test::g_pwmChannelActions[0].start);
}

TEST_F(TimerTest, PwmStartInvalidIdIgnored)
{
    hal::timerPwmStart(static_cast<hal::TimerId>(99), hal::TimerChannel::Ch1);

    EXPECT_EQ(test::g_pwmChannelActions.size(), 0u);
}

TEST_F(TimerTest, PwmSetDutyRecordsValue)
{
    hal::timerPwmSetDuty(hal::TimerId::Tim2, hal::TimerChannel::Ch4, 750);

    ASSERT_EQ(test::g_pwmSetDutyCalls.size(), 1u);
    EXPECT_EQ(test::g_pwmSetDutyCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim2));
    EXPECT_EQ(test::g_pwmSetDutyCalls[0].channel,
              static_cast<std::uint8_t>(hal::TimerChannel::Ch4));
    EXPECT_EQ(test::g_pwmSetDutyCalls[0].duty, 750u);
}

TEST_F(TimerTest, PwmSetDutyInvalidIdIgnored)
{
    hal::timerPwmSetDuty(static_cast<hal::TimerId>(99), hal::TimerChannel::Ch1, 100);

    EXPECT_EQ(test::g_pwmSetDutyCalls.size(), 0u);
}

// ---- Microsecond delay ----

TEST_F(TimerTest, DelayUsRecordsValue)
{
    hal::timerDelayUs(1000);
    EXPECT_EQ(test::g_timerDelayUsValue, 1000u);
}

TEST_F(TimerTest, DelayUsZero)
{
    hal::timerDelayUs(0);
    EXPECT_EQ(test::g_timerDelayUsValue, 0u);
}

// ---- RCC clock enable/disable ----

TEST_F(TimerTest, RccEnableRecordsTimerId)
{
    hal::rccEnableTimerClock(hal::TimerId::Tim2);
    hal::rccEnableTimerClock(hal::TimerId::Tim7);

    ASSERT_EQ(test::g_timerRccCalls.size(), 2u);
    EXPECT_EQ(test::g_timerRccCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim2));
    EXPECT_TRUE(test::g_timerRccCalls[0].enable);
    EXPECT_EQ(test::g_timerRccCalls[1].id, static_cast<std::uint8_t>(hal::TimerId::Tim7));
    EXPECT_TRUE(test::g_timerRccCalls[1].enable);
}

TEST_F(TimerTest, RccDisableRecordsTimerId)
{
    hal::rccDisableTimerClock(hal::TimerId::Tim5);

    ASSERT_EQ(test::g_timerRccCalls.size(), 1u);
    EXPECT_EQ(test::g_timerRccCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim5));
    EXPECT_FALSE(test::g_timerRccCalls[0].enable);
}

TEST_F(TimerTest, RccEnableInvalidIdIgnored)
{
    hal::rccEnableTimerClock(static_cast<hal::TimerId>(99));

    EXPECT_EQ(test::g_timerRccCalls.size(), 0u);
}

TEST_F(TimerTest, RccDisableInvalidIdIgnored)
{
    hal::rccDisableTimerClock(static_cast<hal::TimerId>(99));

    EXPECT_EQ(test::g_timerRccCalls.size(), 0u);
}

// ---- Multiple inits ----

TEST_F(TimerTest, MultipleTimerInits)
{
    hal::TimerConfig c1{};
    c1.id = hal::TimerId::Tim6;
    c1.prescaler = 83;
    c1.period = 999;

    hal::TimerConfig c2{};
    c2.id = hal::TimerId::Tim7;
    c2.prescaler = 83;
    c2.period = 0xFFFF;

    hal::timerInit(c1);
    hal::timerInit(c2);

    ASSERT_EQ(test::g_timerInitCalls.size(), 2u);
    EXPECT_EQ(test::g_timerInitCalls[0].id, static_cast<std::uint8_t>(hal::TimerId::Tim6));
    EXPECT_EQ(test::g_timerInitCalls[1].id, static_cast<std::uint8_t>(hal::TimerId::Tim7));
}

// ---- PWM start then stop sequence ----

TEST_F(TimerTest, PwmStartStopSequence)
{
    hal::timerPwmStart(hal::TimerId::Tim3, hal::TimerChannel::Ch1);
    hal::timerPwmStop(hal::TimerId::Tim3, hal::TimerChannel::Ch1);

    ASSERT_EQ(test::g_pwmChannelActions.size(), 2u);
    EXPECT_TRUE(test::g_pwmChannelActions[0].start);
    EXPECT_FALSE(test::g_pwmChannelActions[1].start);
}
