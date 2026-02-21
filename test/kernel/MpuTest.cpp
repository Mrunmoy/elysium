// Unit tests for MPU utility functions and configuration computation.

#include "kernel/Mpu.h"
#include "MockMpu.h"

#include <gtest/gtest.h>

#include <cstdint>

class MpuTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMpuMockState();
    }
};

// --- mpuRoundUpSize ---

TEST_F(MpuTest, MpuRoundUpSize_PowerOf2Unchanged)
{
    EXPECT_EQ(kernel::mpuRoundUpSize(32), 32u);
    EXPECT_EQ(kernel::mpuRoundUpSize(64), 64u);
    EXPECT_EQ(kernel::mpuRoundUpSize(256), 256u);
    EXPECT_EQ(kernel::mpuRoundUpSize(1024), 1024u);
    EXPECT_EQ(kernel::mpuRoundUpSize(128 * 1024), 128u * 1024u);
}

TEST_F(MpuTest, MpuRoundUpSize_NonPowerOf2RoundsUp)
{
    EXPECT_EQ(kernel::mpuRoundUpSize(33), 64u);
    EXPECT_EQ(kernel::mpuRoundUpSize(100), 128u);
    EXPECT_EQ(kernel::mpuRoundUpSize(500), 512u);
    EXPECT_EQ(kernel::mpuRoundUpSize(1000), 1024u);
    EXPECT_EQ(kernel::mpuRoundUpSize(1), 32u);    // minimum 32
    EXPECT_EQ(kernel::mpuRoundUpSize(16), 32u);   // below minimum
}

// --- mpuSizeEncoding ---

TEST_F(MpuTest, MpuSizeEncoding_512Bytes)
{
    // SIZE = log2(512) - 1 = 9 - 1 = 8
    EXPECT_EQ(kernel::mpuSizeEncoding(512), 8u);
}

TEST_F(MpuTest, MpuSizeEncoding_1KB)
{
    // SIZE = log2(1024) - 1 = 10 - 1 = 9
    EXPECT_EQ(kernel::mpuSizeEncoding(1024), 9u);
}

TEST_F(MpuTest, MpuSizeEncoding_128KB)
{
    // SIZE = log2(131072) - 1 = 17 - 1 = 16
    EXPECT_EQ(kernel::mpuSizeEncoding(128 * 1024), 16u);
}

TEST_F(MpuTest, MpuSizeEncoding_32Bytes)
{
    // SIZE = log2(32) - 1 = 5 - 1 = 4
    EXPECT_EQ(kernel::mpuSizeEncoding(32), 4u);
}

TEST_F(MpuTest, MpuSizeEncoding_NonPowerOf2ReturnsZero)
{
    EXPECT_EQ(kernel::mpuSizeEncoding(100), 0u);
    EXPECT_EQ(kernel::mpuSizeEncoding(0), 0u);
    EXPECT_EQ(kernel::mpuSizeEncoding(16), 0u);  // below minimum 32
}

// --- mpuValidateStack ---

TEST_F(MpuTest, MpuValidateStack_ValidReturnsTrue)
{
    alignas(512) std::uint8_t stack[512] = {};
    EXPECT_TRUE(kernel::mpuValidateStack(stack, 512));
}

TEST_F(MpuTest, MpuValidateStack_NonPowerOf2ReturnsFalse)
{
    alignas(512) std::uint8_t stack[512] = {};
    EXPECT_FALSE(kernel::mpuValidateStack(stack, 300));
}

TEST_F(MpuTest, MpuValidateStack_MisalignedReturnsFalse)
{
    alignas(1024) std::uint8_t stack[1024] = {};
    // Misalign by 1 byte
    EXPECT_FALSE(kernel::mpuValidateStack(stack + 1, 512));
}

TEST_F(MpuTest, MpuValidateStack_TooSmallReturnsFalse)
{
    alignas(16) std::uint8_t stack[16] = {};
    EXPECT_FALSE(kernel::mpuValidateStack(stack, 16));
}

TEST_F(MpuTest, MpuValidateStack_1KBStackValid)
{
    alignas(1024) std::uint8_t stack[1024] = {};
    EXPECT_TRUE(kernel::mpuValidateStack(stack, 1024));
}

// --- mpuComputeThreadConfig ---

TEST_F(MpuTest, ComputeThreadConfig_512ByteStack)
{
    alignas(512) std::uint8_t stack[512] = {};
    auto config = kernel::mpuComputeThreadConfig(stack, 512);

    // RBAR should have VALID bit (bit 4) set and region 4
    EXPECT_EQ(config.stackRbar & 0x1Fu, (1u << 4) | 4u);

    // RASR should have enable bit set
    EXPECT_NE(config.stackRasr & 1u, 0u);

    // SIZE field (bits 5:1) should encode 512 = size encoding 8
    std::uint8_t sizeField = (config.stackRasr >> 1) & 0x1Fu;
    EXPECT_EQ(sizeField, 8u);
}

TEST_F(MpuTest, ComputeThreadConfig_1KStack)
{
    alignas(1024) std::uint8_t stack[1024] = {};
    auto config = kernel::mpuComputeThreadConfig(stack, 1024);

    // SIZE field should encode 1024 = size encoding 9
    std::uint8_t sizeField = (config.stackRasr >> 1) & 0x1Fu;
    EXPECT_EQ(sizeField, 9u);
}

TEST_F(MpuTest, ComputeThreadConfig_SetsCorrectRegionNumber)
{
    alignas(512) std::uint8_t stack[512] = {};
    auto config = kernel::mpuComputeThreadConfig(stack, 512);

    // Region number is in RBAR bits [3:0], should be 4 (ThreadStack)
    EXPECT_EQ(config.stackRbar & 0x0Fu,
              static_cast<std::uint32_t>(kernel::MpuRegion::ThreadStack));
}

TEST_F(MpuTest, ComputeThreadConfig_SetsXnBit)
{
    alignas(512) std::uint8_t stack[512] = {};
    auto config = kernel::mpuComputeThreadConfig(stack, 512);

    // XN bit is bit 28 of RASR
    EXPECT_NE(config.stackRasr & (1u << 28), 0u);
}

TEST_F(MpuTest, ComputeThreadConfig_SetsFullAccess)
{
    alignas(512) std::uint8_t stack[512] = {};
    auto config = kernel::mpuComputeThreadConfig(stack, 512);

    // AP field is bits [26:24], full access = 0b011
    std::uint8_t ap = (config.stackRasr >> 24) & 0x07u;
    EXPECT_EQ(ap, 0x03u);
}

// --- mpuInit ---

TEST_F(MpuTest, MpuInit_EnablesMpu)
{
    kernel::mpuInit();
    EXPECT_TRUE(test::g_mpuInitCalled);
}
