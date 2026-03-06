#include <gtest/gtest.h>

#include "hal/Rcc.h"
#include "hal/Rng.h"
#include "msos/ErrorCode.h"

#include "MockRegisters.h"

class RngTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

TEST_F(RngTest, InitIncrementsCallCount)
{
    hal::rngInit();

    EXPECT_EQ(test::g_rngInitCount, 1u);
}

TEST_F(RngTest, InitReturnsOkByDefault)
{
    auto result = hal::rngInit();

    EXPECT_EQ(result, msos::error::kOk);
}

TEST_F(RngTest, InitReturnsInjectedError)
{
    test::g_rngInitReturnCode = msos::error::kNoSys;

    auto result = hal::rngInit();

    EXPECT_EQ(result, msos::error::kNoSys);
}

TEST_F(RngTest, ReadReturnsInjectedValue)
{
    test::g_rngValues.push_back(0xDEADBEEF);

    std::uint32_t value = 0;
    auto result = hal::rngRead(value);

    EXPECT_EQ(result, msos::error::kOk);
    EXPECT_EQ(value, 0xDEADBEEF);
    EXPECT_EQ(test::g_rngReadCount, 1u);
}

TEST_F(RngTest, ReadReturnsInjectedError)
{
    test::g_rngReadReturnCode = msos::error::kIo;

    std::uint32_t value = 0x12345678;
    auto result = hal::rngRead(value);

    EXPECT_EQ(result, msos::error::kIo);
    // value should be unchanged on error
    EXPECT_EQ(value, 0x12345678u);
}

TEST_F(RngTest, MultipleReadsReturnSequentialValues)
{
    test::g_rngValues.push_back(0x11111111);
    test::g_rngValues.push_back(0x22222222);
    test::g_rngValues.push_back(0x33333333);

    std::uint32_t v1 = 0, v2 = 0, v3 = 0;
    hal::rngRead(v1);
    hal::rngRead(v2);
    hal::rngRead(v3);

    EXPECT_EQ(v1, 0x11111111u);
    EXPECT_EQ(v2, 0x22222222u);
    EXPECT_EQ(v3, 0x33333333u);
    EXPECT_EQ(test::g_rngReadCount, 3u);
}

TEST_F(RngTest, ReadBeyondInjectedValuesReturnsZero)
{
    test::g_rngValues.push_back(0xCAFEBABE);

    std::uint32_t v1 = 0, v2 = 0;
    hal::rngRead(v1);
    hal::rngRead(v2);

    EXPECT_EQ(v1, 0xCAFEBABEu);
    EXPECT_EQ(v2, 0u);
}

TEST_F(RngTest, DeinitIncrementsCallCount)
{
    hal::rngDeinit();

    EXPECT_EQ(test::g_rngDeinitCount, 1u);
}

TEST_F(RngTest, ResetClearsMockState)
{
    test::g_rngInitCount = 5;
    test::g_rngReadCount = 3;
    test::g_rngDeinitCount = 2;
    test::g_rngValues.push_back(42);
    test::g_rngReadPos = 1;
    test::g_rngReadReturnCode = -5;
    test::g_rngInitReturnCode = -1;

    test::resetMockState();

    EXPECT_EQ(test::g_rngInitCount, 0u);
    EXPECT_EQ(test::g_rngReadCount, 0u);
    EXPECT_EQ(test::g_rngDeinitCount, 0u);
    EXPECT_TRUE(test::g_rngValues.empty());
    EXPECT_EQ(test::g_rngReadPos, 0u);
    EXPECT_EQ(test::g_rngReadReturnCode, 0);
    EXPECT_EQ(test::g_rngInitReturnCode, 0);
}

TEST_F(RngTest, RccEnableRecordsRngClock)
{
    hal::rccEnableRngClock();

    ASSERT_EQ(test::g_rccEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccEnableCalls[0].peripheral, "rng");
}

TEST_F(RngTest, RccDisableRecordsRngClock)
{
    hal::rccDisableRngClock();

    ASSERT_EQ(test::g_rccDisableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccDisableCalls[0].peripheral, "rng");
}
