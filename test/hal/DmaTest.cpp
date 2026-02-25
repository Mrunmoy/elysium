#include <gtest/gtest.h>

#include "hal/Dma.h"

#include "MockRegisters.h"

class DmaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(DmaTest, InitRecordsControllerAndStream)
{
    hal::DmaConfig config{};
    config.controller = hal::DmaController::Dma1;
    config.stream = hal::DmaStream::Stream3;
    config.channel = hal::DmaChannel::Channel4;
    config.direction = hal::DmaDirection::PeriphToMemory;

    hal::dmaInit(config);

    ASSERT_EQ(test::g_dmaInitCalls.size(), 1u);
    EXPECT_EQ(test::g_dmaInitCalls[0].controller,
              static_cast<std::uint8_t>(hal::DmaController::Dma1));
    EXPECT_EQ(test::g_dmaInitCalls[0].stream,
              static_cast<std::uint8_t>(hal::DmaStream::Stream3));
    EXPECT_EQ(test::g_dmaInitCalls[0].channel,
              static_cast<std::uint8_t>(hal::DmaChannel::Channel4));
    EXPECT_EQ(test::g_dmaInitCalls[0].direction,
              static_cast<std::uint8_t>(hal::DmaDirection::PeriphToMemory));
}

TEST_F(DmaTest, InitRecordsDataSizes)
{
    hal::DmaConfig config{};
    config.controller = hal::DmaController::Dma2;
    config.stream = hal::DmaStream::Stream0;
    config.channel = hal::DmaChannel::Channel0;
    config.direction = hal::DmaDirection::MemoryToPeriph;
    config.peripheralSize = hal::DmaDataSize::HalfWord;
    config.memorySize = hal::DmaDataSize::Word;

    hal::dmaInit(config);

    ASSERT_EQ(test::g_dmaInitCalls.size(), 1u);
    EXPECT_EQ(test::g_dmaInitCalls[0].peripheralSize,
              static_cast<std::uint8_t>(hal::DmaDataSize::HalfWord));
    EXPECT_EQ(test::g_dmaInitCalls[0].memorySize,
              static_cast<std::uint8_t>(hal::DmaDataSize::Word));
}

TEST_F(DmaTest, InitRecordsIncrementAndPriority)
{
    hal::DmaConfig config{};
    config.controller = hal::DmaController::Dma1;
    config.stream = hal::DmaStream::Stream5;
    config.channel = hal::DmaChannel::Channel2;
    config.direction = hal::DmaDirection::PeriphToMemory;
    config.peripheralIncrement = true;
    config.memoryIncrement = false;
    config.priority = hal::DmaPriority::VeryHigh;
    config.circular = true;

    hal::dmaInit(config);

    ASSERT_EQ(test::g_dmaInitCalls.size(), 1u);
    EXPECT_TRUE(test::g_dmaInitCalls[0].peripheralIncrement);
    EXPECT_FALSE(test::g_dmaInitCalls[0].memoryIncrement);
    EXPECT_EQ(test::g_dmaInitCalls[0].priority,
              static_cast<std::uint8_t>(hal::DmaPriority::VeryHigh));
    EXPECT_TRUE(test::g_dmaInitCalls[0].circular);
}

// ---- Start ----

static void dummyDmaCb(void *, std::uint8_t) {}

TEST_F(DmaTest, StartRecordsAddressesAndCount)
{
    int ctx = 99;

    hal::dmaStart(hal::DmaController::Dma1, hal::DmaStream::Stream2,
                  0x40013C0C, 0x20001000, 256,
                  dummyDmaCb, &ctx);

    ASSERT_EQ(test::g_dmaStartCalls.size(), 1u);
    EXPECT_EQ(test::g_dmaStartCalls[0].controller,
              static_cast<std::uint8_t>(hal::DmaController::Dma1));
    EXPECT_EQ(test::g_dmaStartCalls[0].stream,
              static_cast<std::uint8_t>(hal::DmaStream::Stream2));
    EXPECT_EQ(test::g_dmaStartCalls[0].peripheralAddr, 0x40013C0Cu);
    EXPECT_EQ(test::g_dmaStartCalls[0].memoryAddr, 0x20001000u);
    EXPECT_EQ(test::g_dmaStartCalls[0].count, 256u);
    EXPECT_EQ(test::g_dmaStartCalls[0].callback, reinterpret_cast<void *>(dummyDmaCb));
    EXPECT_EQ(test::g_dmaStartCalls[0].arg, &ctx);
}

TEST_F(DmaTest, StartWithNullCallback)
{
    hal::dmaStart(hal::DmaController::Dma2, hal::DmaStream::Stream7,
                  0x40004400, 0x20002000, 64,
                  nullptr, nullptr);

    ASSERT_EQ(test::g_dmaStartCalls.size(), 1u);
    EXPECT_EQ(test::g_dmaStartCalls[0].callback, nullptr);
    EXPECT_EQ(test::g_dmaStartCalls[0].arg, nullptr);
}

// ---- Stop ----

TEST_F(DmaTest, StopIncrementsCount)
{
    hal::dmaStop(hal::DmaController::Dma1, hal::DmaStream::Stream0);
    hal::dmaStop(hal::DmaController::Dma2, hal::DmaStream::Stream1);

    EXPECT_EQ(test::g_dmaStopCount, 2u);
}

// ---- IsBusy ----

TEST_F(DmaTest, IsBusyReturnsFalseByDefault)
{
    EXPECT_FALSE(hal::dmaIsBusy(hal::DmaController::Dma1, hal::DmaStream::Stream0));
}

TEST_F(DmaTest, IsBusyReturnsInjectableValue)
{
    test::g_dmaBusy = true;
    EXPECT_TRUE(hal::dmaIsBusy(hal::DmaController::Dma1, hal::DmaStream::Stream0));
}

// ---- Remaining ----

TEST_F(DmaTest, RemainingReturnsZeroByDefault)
{
    EXPECT_EQ(hal::dmaRemaining(hal::DmaController::Dma1, hal::DmaStream::Stream0), 0u);
}

TEST_F(DmaTest, RemainingReturnsInjectableValue)
{
    test::g_dmaRemaining = 42;
    EXPECT_EQ(hal::dmaRemaining(hal::DmaController::Dma1, hal::DmaStream::Stream0), 42u);
}

// ---- Interrupts ----

TEST_F(DmaTest, InterruptEnableIncrementsCount)
{
    hal::dmaInterruptEnable(hal::DmaController::Dma1, hal::DmaStream::Stream0);
    hal::dmaInterruptEnable(hal::DmaController::Dma2, hal::DmaStream::Stream3);

    EXPECT_EQ(test::g_dmaInterruptEnableCount, 2u);
}

TEST_F(DmaTest, InterruptDisableIncrementsCount)
{
    hal::dmaInterruptDisable(hal::DmaController::Dma1, hal::DmaStream::Stream0);

    EXPECT_EQ(test::g_dmaInterruptDisableCount, 1u);
}

// ---- Mock state reset ----

TEST_F(DmaTest, MockStateResetsCleanly)
{
    hal::DmaConfig config{};
    config.controller = hal::DmaController::Dma1;
    config.stream = hal::DmaStream::Stream0;
    config.channel = hal::DmaChannel::Channel0;
    config.direction = hal::DmaDirection::PeriphToMemory;
    hal::dmaInit(config);
    hal::dmaStop(hal::DmaController::Dma1, hal::DmaStream::Stream0);
    test::g_dmaBusy = true;
    test::g_dmaRemaining = 100;

    test::resetMockState();

    EXPECT_TRUE(test::g_dmaInitCalls.empty());
    EXPECT_TRUE(test::g_dmaStartCalls.empty());
    EXPECT_EQ(test::g_dmaStopCount, 0u);
    EXPECT_FALSE(test::g_dmaBusy);
    EXPECT_EQ(test::g_dmaRemaining, 0u);
}
