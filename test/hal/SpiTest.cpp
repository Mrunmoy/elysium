#include <gtest/gtest.h>

#include "hal/Spi.h"

#include "MockRegisters.h"

class SpiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(SpiTest, InitRecordsIdAndMode)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi1;
    config.mode = hal::SpiMode::Mode0;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_EQ(test::g_spiInitCalls[0].id, static_cast<std::uint8_t>(hal::SpiId::Spi1));
    EXPECT_EQ(test::g_spiInitCalls[0].mode, static_cast<std::uint8_t>(hal::SpiMode::Mode0));
}

TEST_F(SpiTest, InitRecordsPrescalerAndDataSize)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi2;
    config.prescaler = hal::SpiBaudPrescaler::Div64;
    config.dataSize = hal::SpiDataSize::Bits16;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_EQ(test::g_spiInitCalls[0].prescaler,
              static_cast<std::uint8_t>(hal::SpiBaudPrescaler::Div64));
    EXPECT_EQ(test::g_spiInitCalls[0].dataSize,
              static_cast<std::uint8_t>(hal::SpiDataSize::Bits16));
}

TEST_F(SpiTest, InitRecordsBitOrderAndMaster)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi3;
    config.bitOrder = hal::SpiBitOrder::LsbFirst;
    config.master = false;
    config.softwareNss = false;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_EQ(test::g_spiInitCalls[0].bitOrder,
              static_cast<std::uint8_t>(hal::SpiBitOrder::LsbFirst));
    EXPECT_FALSE(test::g_spiInitCalls[0].master);
    EXPECT_FALSE(test::g_spiInitCalls[0].softwareNss);
}

TEST_F(SpiTest, InitMode3RecordsCpolCpha)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi1;
    config.mode = hal::SpiMode::Mode3;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_EQ(test::g_spiInitCalls[0].mode, static_cast<std::uint8_t>(hal::SpiMode::Mode3));
}

// ---- Polled Transfer ----

TEST_F(SpiTest, TransferRecordsCallWithTxAndRx)
{
    std::uint8_t tx[] = {0xAA, 0xBB};
    std::uint8_t rx[2] = {};

    test::g_spiRxData = {0x11, 0x22};

    hal::spiTransfer(hal::SpiId::Spi1, tx, rx, 2);

    ASSERT_EQ(test::g_spiTransferCalls.size(), 1u);
    EXPECT_EQ(test::g_spiTransferCalls[0].id, static_cast<std::uint8_t>(hal::SpiId::Spi1));
    EXPECT_EQ(test::g_spiTransferCalls[0].length, 2u);
    EXPECT_TRUE(test::g_spiTransferCalls[0].hasTxData);
    EXPECT_TRUE(test::g_spiTransferCalls[0].hasRxData);
}

TEST_F(SpiTest, TransferFillsRxFromInjectableBuffer)
{
    std::uint8_t tx[] = {0x00, 0x00, 0x00};
    std::uint8_t rx[3] = {};

    test::g_spiRxData = {0xDE, 0xAD, 0xBE};

    hal::spiTransfer(hal::SpiId::Spi1, tx, rx, 3);

    EXPECT_EQ(rx[0], 0xDE);
    EXPECT_EQ(rx[1], 0xAD);
    EXPECT_EQ(rx[2], 0xBE);
}

TEST_F(SpiTest, TransferWithNullTx)
{
    std::uint8_t rx[2] = {};
    test::g_spiRxData = {0x42, 0x43};

    hal::spiTransfer(hal::SpiId::Spi1, nullptr, rx, 2);

    ASSERT_EQ(test::g_spiTransferCalls.size(), 1u);
    EXPECT_FALSE(test::g_spiTransferCalls[0].hasTxData);
    EXPECT_EQ(rx[0], 0x42);
    EXPECT_EQ(rx[1], 0x43);
}

TEST_F(SpiTest, TransferWithNullRx)
{
    std::uint8_t tx[] = {0x01, 0x02};

    hal::spiTransfer(hal::SpiId::Spi2, tx, nullptr, 2);

    ASSERT_EQ(test::g_spiTransferCalls.size(), 1u);
    EXPECT_TRUE(test::g_spiTransferCalls[0].hasTxData);
    EXPECT_FALSE(test::g_spiTransferCalls[0].hasRxData);
}

// ---- TransferByte ----

TEST_F(SpiTest, TransferByteReturnsRxData)
{
    test::g_spiRxData = {0x55};

    std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, 0xAA);

    EXPECT_EQ(rx, 0x55);
    ASSERT_EQ(test::g_spiTransferCalls.size(), 1u);
    EXPECT_EQ(test::g_spiTransferCalls[0].length, 1u);
}

TEST_F(SpiTest, TransferByteReturnsZeroWhenNoRxData)
{
    std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, 0xFF);
    EXPECT_EQ(rx, 0u);
}

// ---- Async Transfer ----

TEST_F(SpiTest, AsyncTransferRecordsCall)
{
    std::uint8_t tx[] = {0x01};
    std::uint8_t rx[1] = {};

    auto dummyCb = [](void *) {};

    hal::spiTransferAsync(hal::SpiId::Spi1, tx, rx, 1, dummyCb, nullptr);

    EXPECT_EQ(test::g_spiAsyncCount, 1u);
    ASSERT_EQ(test::g_spiTransferCalls.size(), 1u);
    EXPECT_EQ(test::g_spiTransferCalls[0].length, 1u);
}

TEST_F(SpiTest, AsyncTransferInvokesCallback)
{
    bool called = false;
    auto cb = [](void *arg) { *static_cast<bool *>(arg) = true; };

    std::uint8_t tx[] = {0x00};
    hal::spiTransferAsync(hal::SpiId::Spi1, tx, nullptr, 1, cb, &called);

    EXPECT_TRUE(called);
}

static void dummySpiCb(void *) {}

TEST_F(SpiTest, AsyncTransferStoresCallbackAndArg)
{
    int ctx = 0;

    hal::spiTransferAsync(hal::SpiId::Spi1, nullptr, nullptr, 0, dummySpiCb, &ctx);

    EXPECT_EQ(test::g_spiAsyncCallback, reinterpret_cast<void *>(dummySpiCb));
    EXPECT_EQ(test::g_spiAsyncArg, &ctx);
}

// ---- Slave RX Interrupt ----

static void dummySlaveRxCb(void *, std::uint8_t) {}

TEST_F(SpiTest, SlaveRxInterruptEnableRecordsCall)
{
    int ctx = 42;

    hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi2, dummySlaveRxCb, &ctx);

    ASSERT_EQ(test::g_spiSlaveRxEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_spiSlaveRxEnableCalls[0].id, static_cast<std::uint8_t>(hal::SpiId::Spi2));
    EXPECT_EQ(test::g_spiSlaveRxCallback, reinterpret_cast<void *>(dummySlaveRxCb));
    EXPECT_EQ(test::g_spiSlaveRxArg, &ctx);
    EXPECT_TRUE(test::g_spiSlaveRxActive);
}

TEST_F(SpiTest, SlaveRxInterruptDisableRecordsCall)
{
    hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi1, dummySlaveRxCb, nullptr);

    hal::spiSlaveRxInterruptDisable(hal::SpiId::Spi1);

    EXPECT_EQ(test::g_spiSlaveRxDisableCount, 1u);
    EXPECT_FALSE(test::g_spiSlaveRxActive);
    EXPECT_EQ(test::g_spiSlaveRxCallback, nullptr);
    EXPECT_EQ(test::g_spiSlaveRxArg, nullptr);
}

TEST_F(SpiTest, SlaveSetTxByteRecordsCall)
{
    hal::spiSlaveSetTxByte(hal::SpiId::Spi2, 0xA5);

    ASSERT_EQ(test::g_spiSlaveSetTxBytes.size(), 1u);
    EXPECT_EQ(test::g_spiSlaveSetTxBytes[0], 0xA5);
}

TEST_F(SpiTest, SlaveRxEnableThenDisableClearsState)
{
    hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi1, dummySlaveRxCb, nullptr);
    EXPECT_TRUE(test::g_spiSlaveRxActive);

    hal::spiSlaveRxInterruptDisable(hal::SpiId::Spi1);
    EXPECT_FALSE(test::g_spiSlaveRxActive);
    EXPECT_EQ(test::g_spiSlaveRxCallback, nullptr);
}

TEST_F(SpiTest, SlaveRxCallbackInvokedWithByte)
{
    std::uint8_t received = 0;
    auto cb = [](void *arg, std::uint8_t rxByte) {
        *static_cast<std::uint8_t *>(arg) = rxByte;
    };

    hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi1, cb, &received);

    // Simulate ISR invoking the callback
    auto fn = reinterpret_cast<hal::SpiSlaveRxCallbackFn>(test::g_spiSlaveRxCallback);
    fn(test::g_spiSlaveRxArg, 0x42);

    EXPECT_EQ(received, 0x42);
}

TEST_F(SpiTest, SlaveSetTxByteRecordsMultipleCalls)
{
    hal::spiSlaveSetTxByte(hal::SpiId::Spi1, 0x11);
    hal::spiSlaveSetTxByte(hal::SpiId::Spi1, 0x22);
    hal::spiSlaveSetTxByte(hal::SpiId::Spi1, 0x33);

    ASSERT_EQ(test::g_spiSlaveSetTxBytes.size(), 3u);
    EXPECT_EQ(test::g_spiSlaveSetTxBytes[0], 0x11);
    EXPECT_EQ(test::g_spiSlaveSetTxBytes[1], 0x22);
    EXPECT_EQ(test::g_spiSlaveSetTxBytes[2], 0x33);
}

TEST_F(SpiTest, InitRecordsSoftwareNssForSlave)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi2;
    config.master = false;
    config.softwareNss = true;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_FALSE(test::g_spiInitCalls[0].master);
    EXPECT_TRUE(test::g_spiInitCalls[0].softwareNss);
}

TEST_F(SpiTest, InitRecordsMasterSoftwareNss)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi1;
    config.master = true;
    config.softwareNss = true;

    hal::spiInit(config);

    ASSERT_EQ(test::g_spiInitCalls.size(), 1u);
    EXPECT_TRUE(test::g_spiInitCalls[0].master);
    EXPECT_TRUE(test::g_spiInitCalls[0].softwareNss);
}

// ---- Mock state reset ----

TEST_F(SpiTest, MockStateResetsCleanly)
{
    hal::SpiConfig config{};
    config.id = hal::SpiId::Spi1;
    hal::spiInit(config);
    test::g_spiRxData = {0x01};
    test::g_spiAsyncCount = 5;
    hal::spiSlaveRxInterruptEnable(hal::SpiId::Spi1, dummySlaveRxCb, nullptr);
    hal::spiSlaveSetTxByte(hal::SpiId::Spi1, 0xFF);

    test::resetMockState();

    EXPECT_TRUE(test::g_spiInitCalls.empty());
    EXPECT_TRUE(test::g_spiTransferCalls.empty());
    EXPECT_TRUE(test::g_spiRxData.empty());
    EXPECT_EQ(test::g_spiRxReadPos, 0u);
    EXPECT_EQ(test::g_spiAsyncCount, 0u);
    EXPECT_EQ(test::g_spiAsyncCallback, nullptr);
    EXPECT_TRUE(test::g_spiSlaveRxEnableCalls.empty());
    EXPECT_EQ(test::g_spiSlaveRxDisableCount, 0u);
    EXPECT_EQ(test::g_spiSlaveRxCallback, nullptr);
    EXPECT_FALSE(test::g_spiSlaveRxActive);
    EXPECT_TRUE(test::g_spiSlaveSetTxBytes.empty());
}
