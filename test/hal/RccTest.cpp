#include <gtest/gtest.h>

#include "hal/Rcc.h"

#include "MockRegisters.h"

class RccTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- SPI clock ----

TEST_F(RccTest, EnableSpiClockRecordsCall)
{
    hal::rccEnableSpiClock(hal::SpiId::Spi1);

    ASSERT_EQ(test::g_rccEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccEnableCalls[0].peripheral, "spi");
    EXPECT_EQ(test::g_rccEnableCalls[0].id, static_cast<std::uint8_t>(hal::SpiId::Spi1));
}

TEST_F(RccTest, DisableSpiClockRecordsCall)
{
    hal::rccDisableSpiClock(hal::SpiId::Spi2);

    ASSERT_EQ(test::g_rccDisableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccDisableCalls[0].peripheral, "spi");
    EXPECT_EQ(test::g_rccDisableCalls[0].id, static_cast<std::uint8_t>(hal::SpiId::Spi2));
}

// ---- I2C clock ----

TEST_F(RccTest, EnableI2cClockRecordsCall)
{
    hal::rccEnableI2cClock(hal::I2cId::I2c1);

    ASSERT_EQ(test::g_rccEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccEnableCalls[0].peripheral, "i2c");
    EXPECT_EQ(test::g_rccEnableCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c1));
}

TEST_F(RccTest, DisableI2cClockRecordsCall)
{
    hal::rccDisableI2cClock(hal::I2cId::I2c3);

    ASSERT_EQ(test::g_rccDisableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccDisableCalls[0].peripheral, "i2c");
    EXPECT_EQ(test::g_rccDisableCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c3));
}

// ---- DMA clock ----

TEST_F(RccTest, EnableDmaClockRecordsCall)
{
    hal::rccEnableDmaClock(hal::DmaController::Dma1);

    ASSERT_EQ(test::g_rccEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccEnableCalls[0].peripheral, "dma");
    EXPECT_EQ(test::g_rccEnableCalls[0].id, static_cast<std::uint8_t>(hal::DmaController::Dma1));
}

TEST_F(RccTest, DisableDmaClockRecordsCall)
{
    hal::rccDisableDmaClock(hal::DmaController::Dma2);

    ASSERT_EQ(test::g_rccDisableCalls.size(), 1u);
    EXPECT_EQ(test::g_rccDisableCalls[0].peripheral, "dma");
    EXPECT_EQ(test::g_rccDisableCalls[0].id, static_cast<std::uint8_t>(hal::DmaController::Dma2));
}

// ---- Multiple calls ----

TEST_F(RccTest, MultipleEnablesRecordInOrder)
{
    hal::rccEnableSpiClock(hal::SpiId::Spi1);
    hal::rccEnableI2cClock(hal::I2cId::I2c2);
    hal::rccEnableDmaClock(hal::DmaController::Dma1);

    ASSERT_EQ(test::g_rccEnableCalls.size(), 3u);
    EXPECT_EQ(test::g_rccEnableCalls[0].peripheral, "spi");
    EXPECT_EQ(test::g_rccEnableCalls[1].peripheral, "i2c");
    EXPECT_EQ(test::g_rccEnableCalls[2].peripheral, "dma");
}

TEST_F(RccTest, MockStateResetsCleanly)
{
    hal::rccEnableSpiClock(hal::SpiId::Spi1);
    hal::rccDisableI2cClock(hal::I2cId::I2c1);

    test::resetMockState();

    EXPECT_TRUE(test::g_rccEnableCalls.empty());
    EXPECT_TRUE(test::g_rccDisableCalls.empty());
}
