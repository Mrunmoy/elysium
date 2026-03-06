#include <gtest/gtest.h>

#include "hal/Adc.h"

#include "MockRegisters.h"

class AdcTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

TEST_F(AdcTest, InitRecordsIdAndConfiguration)
{
    hal::AdcConfig config{};
    config.id = hal::AdcId::Adc1;
    config.resolution = hal::AdcResolution::Bits10;
    config.align = hal::AdcAlign::Left;
    config.sampleTime = hal::AdcSampleTime::Cycles144;

    hal::adcInit(config);

    ASSERT_EQ(test::g_adcInitCalls.size(), 1u);
    EXPECT_EQ(test::g_adcInitCalls[0].id, static_cast<std::uint8_t>(hal::AdcId::Adc1));
    EXPECT_EQ(test::g_adcInitCalls[0].resolution,
              static_cast<std::uint8_t>(hal::AdcResolution::Bits10));
    EXPECT_EQ(test::g_adcInitCalls[0].align, static_cast<std::uint8_t>(hal::AdcAlign::Left));
    EXPECT_EQ(test::g_adcInitCalls[0].sampleTime,
              static_cast<std::uint8_t>(hal::AdcSampleTime::Cycles144));
}

TEST_F(AdcTest, InitInvalidIdIgnored)
{
    hal::AdcConfig config{};
    config.id = static_cast<hal::AdcId>(99);

    hal::adcInit(config);

    EXPECT_TRUE(test::g_adcInitCalls.empty());
}

TEST_F(AdcTest, ReadRecordsChannelAndTimeout)
{
    test::g_adcReadValue = 2710;
    test::g_adcReadStatus = msos::error::kOk;
    std::uint16_t value = 0;

    std::int32_t status = hal::adcRead(hal::AdcId::Adc2, 6, &value, 10000);

    EXPECT_EQ(status, msos::error::kOk);
    EXPECT_EQ(value, 2710u);
    ASSERT_EQ(test::g_adcReadCalls.size(), 1u);
    EXPECT_EQ(test::g_adcReadCalls[0].id, static_cast<std::uint8_t>(hal::AdcId::Adc2));
    EXPECT_EQ(test::g_adcReadCalls[0].channel, 6u);
    EXPECT_EQ(test::g_adcReadCalls[0].timeoutLoops, 10000u);
}

TEST_F(AdcTest, ReadInvalidIdReturnsInvalid)
{
    std::uint16_t value = 0;

    std::int32_t status = hal::adcRead(static_cast<hal::AdcId>(99), 0, &value, 10);

    EXPECT_EQ(status, msos::error::kInvalid);
    EXPECT_TRUE(test::g_adcReadCalls.empty());
}

TEST_F(AdcTest, ReadNullOutputReturnsInvalid)
{
    std::int32_t status = hal::adcRead(hal::AdcId::Adc1, 0, nullptr, 10);

    EXPECT_EQ(status, msos::error::kInvalid);
    EXPECT_TRUE(test::g_adcReadCalls.empty());
}

TEST_F(AdcTest, ReadOutOfRangeChannelReturnsInvalid)
{
    std::uint16_t value = 0;

    std::int32_t status = hal::adcRead(hal::AdcId::Adc1, 19, &value, 10);

    EXPECT_EQ(status, msos::error::kInvalid);
    EXPECT_TRUE(test::g_adcReadCalls.empty());
}

TEST_F(AdcTest, ReadZeroTimeoutReturnsInvalid)
{
    std::uint16_t value = 0;

    std::int32_t status = hal::adcRead(hal::AdcId::Adc1, 3, &value, 0);

    EXPECT_EQ(status, msos::error::kInvalid);
    EXPECT_TRUE(test::g_adcReadCalls.empty());
}

TEST_F(AdcTest, ReadReturnsInjectedTimeoutStatus)
{
    test::g_adcReadStatus = msos::error::kTimedOut;
    test::g_adcReadValue = 0;
    std::uint16_t value = 123;

    std::int32_t status = hal::adcRead(hal::AdcId::Adc3, 16, &value, 2000);

    EXPECT_EQ(status, msos::error::kTimedOut);
    EXPECT_EQ(value, 0u);
}

TEST_F(AdcTest, RequestStatusMappingUsesGlobalCodes)
{
    EXPECT_EQ(hal::adcRequestToStatus(true), msos::error::kOk);
    EXPECT_EQ(hal::adcRequestToStatus(false), msos::error::kInvalid);
}
