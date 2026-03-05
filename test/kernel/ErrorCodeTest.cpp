#include <gtest/gtest.h>

#include "msos/ErrorCode.h"

#include <cstdint>

TEST(ErrorCodeTest, IsCanonicalStatus_AcceptsOkAndNegativeValues)
{
    EXPECT_TRUE(msos::error::isCanonicalStatus(msos::error::kOk));
    EXPECT_TRUE(msos::error::isCanonicalStatus(msos::error::kInvalid));
    EXPECT_TRUE(msos::error::isCanonicalStatus(msos::error::kNoAck));
    EXPECT_FALSE(msos::error::isCanonicalStatus(1));
}

TEST(ErrorCodeTest, BoolToStatus_MapsSuccessAndFailure)
{
    EXPECT_EQ(msos::error::boolToStatus(true, msos::error::kTimedOut), msos::error::kOk);
    EXPECT_EQ(msos::error::boolToStatus(false, msos::error::kTimedOut), msos::error::kTimedOut);
}

TEST(ErrorCodeTest, HandleToStatus_UsesInvalidSentinel)
{
    constexpr std::uint8_t kInvalid = 0xFF;
    EXPECT_EQ(msos::error::handleToStatus<std::uint8_t>(0, kInvalid, msos::error::kNoMem),
              msos::error::kOk);
    EXPECT_EQ(msos::error::handleToStatus<std::uint8_t>(kInvalid, kInvalid, msos::error::kNoMem),
              msos::error::kNoMem);
}

TEST(ErrorCodeTest, IsOk_TrueForZero)
{
    EXPECT_TRUE(msos::error::isOk(msos::error::kOk));
    EXPECT_FALSE(msos::error::isOk(msos::error::kInvalid));
    EXPECT_FALSE(msos::error::isOk(msos::error::kPerm));
    EXPECT_FALSE(msos::error::isOk(1));
}

TEST(ErrorCodeTest, IsError_TrueForNegative)
{
    EXPECT_TRUE(msos::error::isError(msos::error::kInvalid));
    EXPECT_TRUE(msos::error::isError(msos::error::kNoAck));
    EXPECT_FALSE(msos::error::isError(msos::error::kOk));
    EXPECT_FALSE(msos::error::isError(1));
}
