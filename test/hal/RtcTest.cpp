#include <gtest/gtest.h>

#include "hal/Rtc.h"

#include "MockRegisters.h"

class RtcTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(RtcTest, InitLsiRecordsClockSource)
{
    hal::rtcInit(hal::RtcClockSource::Lsi);

    ASSERT_EQ(test::g_rtcInitCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcInitCalls[0].clockSource,
              static_cast<std::uint8_t>(hal::RtcClockSource::Lsi));
}

TEST_F(RtcTest, InitLseRecordsClockSource)
{
    hal::rtcInit(hal::RtcClockSource::Lse);

    ASSERT_EQ(test::g_rtcInitCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcInitCalls[0].clockSource,
              static_cast<std::uint8_t>(hal::RtcClockSource::Lse));
}

// ---- Set/Get Time ----

TEST_F(RtcTest, SetTimeRecordsValues)
{
    hal::RtcTime t;
    t.hours = 14;
    t.minutes = 30;
    t.seconds = 45;

    hal::rtcSetTime(t);

    ASSERT_EQ(test::g_rtcSetTimeCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcSetTimeCalls[0].hours, 14u);
    EXPECT_EQ(test::g_rtcSetTimeCalls[0].minutes, 30u);
    EXPECT_EQ(test::g_rtcSetTimeCalls[0].seconds, 45u);
}

TEST_F(RtcTest, GetTimeReturnsSetValues)
{
    hal::RtcTime tSet;
    tSet.hours = 23;
    tSet.minutes = 59;
    tSet.seconds = 58;
    hal::rtcSetTime(tSet);

    hal::RtcTime tGet;
    hal::rtcGetTime(tGet);

    EXPECT_EQ(tGet.hours, 23u);
    EXPECT_EQ(tGet.minutes, 59u);
    EXPECT_EQ(tGet.seconds, 58u);
}

TEST_F(RtcTest, GetTimeReturnsDefaultsBeforeSet)
{
    hal::RtcTime t;
    hal::rtcGetTime(t);

    EXPECT_EQ(t.hours, 0u);
    EXPECT_EQ(t.minutes, 0u);
    EXPECT_EQ(t.seconds, 0u);
}

TEST_F(RtcTest, SetTimeMidnight)
{
    hal::RtcTime t;
    t.hours = 0;
    t.minutes = 0;
    t.seconds = 0;

    hal::rtcSetTime(t);

    hal::RtcTime tGet;
    hal::rtcGetTime(tGet);

    EXPECT_EQ(tGet.hours, 0u);
    EXPECT_EQ(tGet.minutes, 0u);
    EXPECT_EQ(tGet.seconds, 0u);
}

// ---- Set/Get Date ----

TEST_F(RtcTest, SetDateRecordsValues)
{
    hal::RtcDate d;
    d.year = 26;
    d.month = 3;
    d.day = 6;
    d.weekday = 5;  // Friday

    hal::rtcSetDate(d);

    ASSERT_EQ(test::g_rtcSetDateCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcSetDateCalls[0].year, 26u);
    EXPECT_EQ(test::g_rtcSetDateCalls[0].month, 3u);
    EXPECT_EQ(test::g_rtcSetDateCalls[0].day, 6u);
    EXPECT_EQ(test::g_rtcSetDateCalls[0].weekday, 5u);
}

TEST_F(RtcTest, GetDateReturnsSetValues)
{
    hal::RtcDate dSet;
    dSet.year = 99;
    dSet.month = 12;
    dSet.day = 31;
    dSet.weekday = 7;  // Sunday
    hal::rtcSetDate(dSet);

    hal::RtcDate dGet;
    hal::rtcGetDate(dGet);

    EXPECT_EQ(dGet.year, 99u);
    EXPECT_EQ(dGet.month, 12u);
    EXPECT_EQ(dGet.day, 31u);
    EXPECT_EQ(dGet.weekday, 7u);
}

TEST_F(RtcTest, GetDateReturnsDefaultsBeforeSet)
{
    hal::RtcDate d;
    hal::rtcGetDate(d);

    EXPECT_EQ(d.year, 0u);
    EXPECT_EQ(d.month, 1u);
    EXPECT_EQ(d.day, 1u);
    EXPECT_EQ(d.weekday, 1u);
}

// ---- Alarm ----

TEST_F(RtcTest, SetAlarmRecordsConfig)
{
    hal::RtcAlarmConfig cfg;
    cfg.hours = 7;
    cfg.minutes = 30;
    cfg.seconds = 0;
    cfg.maskHours = false;
    cfg.maskMinutes = false;
    cfg.maskSeconds = false;
    cfg.maskDate = true;

    bool fired = false;
    auto cb = [](void *arg) { *static_cast<bool *>(arg) = true; };

    hal::rtcSetAlarm(cfg, cb, &fired);

    ASSERT_EQ(test::g_rtcAlarmCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcAlarmCalls[0].hours, 7u);
    EXPECT_EQ(test::g_rtcAlarmCalls[0].minutes, 30u);
    EXPECT_EQ(test::g_rtcAlarmCalls[0].seconds, 0u);
    EXPECT_FALSE(test::g_rtcAlarmCalls[0].maskHours);
    EXPECT_FALSE(test::g_rtcAlarmCalls[0].maskMinutes);
    EXPECT_FALSE(test::g_rtcAlarmCalls[0].maskSeconds);
    EXPECT_TRUE(test::g_rtcAlarmCalls[0].maskDate);
    EXPECT_NE(test::g_rtcAlarmCalls[0].callback, nullptr);
    EXPECT_EQ(test::g_rtcAlarmCalls[0].arg, &fired);
}

TEST_F(RtcTest, SetAlarmEverySecond)
{
    hal::RtcAlarmConfig cfg;
    cfg.hours = 0;
    cfg.minutes = 0;
    cfg.seconds = 0;
    cfg.maskHours = true;
    cfg.maskMinutes = true;
    cfg.maskSeconds = true;
    cfg.maskDate = true;

    hal::rtcSetAlarm(cfg, nullptr, nullptr);

    ASSERT_EQ(test::g_rtcAlarmCalls.size(), 1u);
    EXPECT_TRUE(test::g_rtcAlarmCalls[0].maskHours);
    EXPECT_TRUE(test::g_rtcAlarmCalls[0].maskMinutes);
    EXPECT_TRUE(test::g_rtcAlarmCalls[0].maskSeconds);
    EXPECT_TRUE(test::g_rtcAlarmCalls[0].maskDate);
}

TEST_F(RtcTest, SetAlarmMatchSecondsOnly)
{
    hal::RtcAlarmConfig cfg;
    cfg.hours = 0;
    cfg.minutes = 0;
    cfg.seconds = 30;
    cfg.maskHours = true;
    cfg.maskMinutes = true;
    cfg.maskSeconds = false;
    cfg.maskDate = true;

    hal::rtcSetAlarm(cfg, nullptr, nullptr);

    ASSERT_EQ(test::g_rtcAlarmCalls.size(), 1u);
    EXPECT_FALSE(test::g_rtcAlarmCalls[0].maskSeconds);
    EXPECT_EQ(test::g_rtcAlarmCalls[0].seconds, 30u);
}

TEST_F(RtcTest, CancelAlarmIncrementsCount)
{
    hal::rtcCancelAlarm();
    hal::rtcCancelAlarm();

    EXPECT_EQ(test::g_rtcCancelAlarmCount, 2u);
}

// ---- IsReady ----

TEST_F(RtcTest, IsReadyReturnsTrueByDefault)
{
    EXPECT_TRUE(hal::rtcIsReady());
}

TEST_F(RtcTest, IsReadyReturnsInjectedValue)
{
    test::g_rtcReady = false;
    EXPECT_FALSE(hal::rtcIsReady());
}

// ---- Multiple operations ----

TEST_F(RtcTest, SetTimeMultipleTimes)
{
    hal::RtcTime t1;
    t1.hours = 1;
    t1.minutes = 2;
    t1.seconds = 3;
    hal::rtcSetTime(t1);

    hal::RtcTime t2;
    t2.hours = 10;
    t2.minutes = 20;
    t2.seconds = 30;
    hal::rtcSetTime(t2);

    ASSERT_EQ(test::g_rtcSetTimeCalls.size(), 2u);

    hal::RtcTime tGet;
    hal::rtcGetTime(tGet);
    EXPECT_EQ(tGet.hours, 10u);
    EXPECT_EQ(tGet.minutes, 20u);
    EXPECT_EQ(tGet.seconds, 30u);
}

TEST_F(RtcTest, SetDateMultipleTimes)
{
    hal::RtcDate d1;
    d1.year = 25;
    d1.month = 1;
    d1.day = 1;
    d1.weekday = 3;
    hal::rtcSetDate(d1);

    hal::RtcDate d2;
    d2.year = 26;
    d2.month = 6;
    d2.day = 15;
    d2.weekday = 1;
    hal::rtcSetDate(d2);

    ASSERT_EQ(test::g_rtcSetDateCalls.size(), 2u);

    hal::RtcDate dGet;
    hal::rtcGetDate(dGet);
    EXPECT_EQ(dGet.year, 26u);
    EXPECT_EQ(dGet.month, 6u);
}

TEST_F(RtcTest, InitThenSetTimeThenAlarm)
{
    hal::rtcInit(hal::RtcClockSource::Lsi);

    hal::RtcTime t;
    t.hours = 12;
    t.minutes = 0;
    t.seconds = 0;
    hal::rtcSetTime(t);

    hal::RtcAlarmConfig cfg;
    cfg.hours = 12;
    cfg.minutes = 0;
    cfg.seconds = 5;
    cfg.maskHours = false;
    cfg.maskMinutes = false;
    cfg.maskSeconds = false;
    cfg.maskDate = true;
    hal::rtcSetAlarm(cfg, nullptr, nullptr);

    EXPECT_EQ(test::g_rtcInitCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcSetTimeCalls.size(), 1u);
    EXPECT_EQ(test::g_rtcAlarmCalls.size(), 1u);
}
