#include <gtest/gtest.h>

#include "hal/Watchdog.h"

#include "MockRegisters.h"

class WatchdogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

TEST_F(WatchdogTest, InitRecordsPrescalerAndReload)
{
    hal::WatchdogConfig config{};
    config.prescaler = hal::WatchdogPrescaler::Div32;
    config.reloadValue = 1000;

    hal::watchdogInit(config);

    ASSERT_EQ(test::g_watchdogInitCalls.size(), 1u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].prescaler,
              static_cast<std::uint8_t>(hal::WatchdogPrescaler::Div32));
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 1000);
}

TEST_F(WatchdogTest, InitRecordsDiv4Prescaler)
{
    hal::WatchdogConfig config{};
    config.prescaler = hal::WatchdogPrescaler::Div4;
    config.reloadValue = 4095;

    hal::watchdogInit(config);

    ASSERT_EQ(test::g_watchdogInitCalls.size(), 1u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].prescaler,
              static_cast<std::uint8_t>(hal::WatchdogPrescaler::Div4));
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 4095);
}

TEST_F(WatchdogTest, InitRecordsDiv256Prescaler)
{
    hal::WatchdogConfig config{};
    config.prescaler = hal::WatchdogPrescaler::Div256;
    config.reloadValue = 2048;

    hal::watchdogInit(config);

    ASSERT_EQ(test::g_watchdogInitCalls.size(), 1u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].prescaler,
              static_cast<std::uint8_t>(hal::WatchdogPrescaler::Div256));
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 2048);
}

TEST_F(WatchdogTest, FeedIncrementsFeedCount)
{
    hal::watchdogFeed();

    EXPECT_EQ(test::g_watchdogFeedCount, 1u);
}

TEST_F(WatchdogTest, MultipleFeedsAccumulate)
{
    hal::watchdogFeed();
    hal::watchdogFeed();
    hal::watchdogFeed();

    EXPECT_EQ(test::g_watchdogFeedCount, 3u);
}

TEST_F(WatchdogTest, FeedCountResetsOnSetUp)
{
    // Previous test fed 3 times, but SetUp() should have cleared it
    EXPECT_EQ(test::g_watchdogFeedCount, 0u);
}

TEST_F(WatchdogTest, MultipleInitsRecordInOrder)
{
    hal::WatchdogConfig config1{};
    config1.prescaler = hal::WatchdogPrescaler::Div8;
    config1.reloadValue = 100;

    hal::WatchdogConfig config2{};
    config2.prescaler = hal::WatchdogPrescaler::Div64;
    config2.reloadValue = 500;

    hal::watchdogInit(config1);
    hal::watchdogInit(config2);

    ASSERT_EQ(test::g_watchdogInitCalls.size(), 2u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].prescaler,
              static_cast<std::uint8_t>(hal::WatchdogPrescaler::Div8));
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 100);
    EXPECT_EQ(test::g_watchdogInitCalls[1].prescaler,
              static_cast<std::uint8_t>(hal::WatchdogPrescaler::Div64));
    EXPECT_EQ(test::g_watchdogInitCalls[1].reloadValue, 500);
}

TEST_F(WatchdogTest, ZeroReloadValueRecorded)
{
    hal::WatchdogConfig config{};
    config.prescaler = hal::WatchdogPrescaler::Div4;
    config.reloadValue = 0;

    hal::watchdogInit(config);

    ASSERT_EQ(test::g_watchdogInitCalls.size(), 1u);
    EXPECT_EQ(test::g_watchdogInitCalls[0].reloadValue, 0);
}
