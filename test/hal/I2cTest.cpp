#include <gtest/gtest.h>

#include "hal/I2c.h"

#include "MockRegisters.h"

class I2cTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(I2cTest, InitRecordsIdAndSpeed)
{
    hal::I2cConfig config{};
    config.id = hal::I2cId::I2c1;
    config.speed = hal::I2cSpeed::Standard;

    hal::i2cInit(config);

    ASSERT_EQ(test::g_i2cInitCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cInitCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c1));
    EXPECT_EQ(test::g_i2cInitCalls[0].speed, static_cast<std::uint8_t>(hal::I2cSpeed::Standard));
}

TEST_F(I2cTest, InitRecordsFastMode)
{
    hal::I2cConfig config{};
    config.id = hal::I2cId::I2c2;
    config.speed = hal::I2cSpeed::Fast;

    hal::i2cInit(config);

    ASSERT_EQ(test::g_i2cInitCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cInitCalls[0].speed, static_cast<std::uint8_t>(hal::I2cSpeed::Fast));
}

TEST_F(I2cTest, InitRecordsFilterSettings)
{
    hal::I2cConfig config{};
    config.id = hal::I2cId::I2c3;
    config.analogFilter = false;
    config.digitalFilterCoeff = 5;

    hal::i2cInit(config);

    ASSERT_EQ(test::g_i2cInitCalls.size(), 1u);
    EXPECT_FALSE(test::g_i2cInitCalls[0].analogFilter);
    EXPECT_EQ(test::g_i2cInitCalls[0].digitalFilterCoeff, 5u);
}

// ---- Write ----

TEST_F(I2cTest, WriteRecordsIdAddrAndLength)
{
    std::uint8_t data[] = {0x10, 0x20};
    hal::I2cError err = hal::i2cWrite(hal::I2cId::I2c1, 0x50, data, 2);

    EXPECT_EQ(err, hal::I2cError::Ok);
    ASSERT_EQ(test::g_i2cWriteCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cWriteCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c1));
    EXPECT_EQ(test::g_i2cWriteCalls[0].addr, 0x50);
    EXPECT_EQ(test::g_i2cWriteCalls[0].length, 2u);
}

TEST_F(I2cTest, WriteReturnsInjectableError)
{
    test::g_i2cReturnError = static_cast<std::uint8_t>(hal::I2cError::Nack);

    std::uint8_t data[] = {0x01};
    hal::I2cError err = hal::i2cWrite(hal::I2cId::I2c1, 0x68, data, 1);

    EXPECT_EQ(err, hal::I2cError::Nack);
}

TEST_F(I2cTest, WriteSingleByte)
{
    std::uint8_t data[] = {0xFF};
    hal::I2cError err = hal::i2cWrite(hal::I2cId::I2c2, 0x3C, data, 1);

    EXPECT_EQ(err, hal::I2cError::Ok);
    ASSERT_EQ(test::g_i2cWriteCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cWriteCalls[0].length, 1u);
}

// ---- Read ----

TEST_F(I2cTest, ReadRecordsIdAddrAndLength)
{
    std::uint8_t buf[3] = {};
    test::g_i2cRxData = {0xAA, 0xBB, 0xCC};

    hal::I2cError err = hal::i2cRead(hal::I2cId::I2c1, 0x50, buf, 3);

    EXPECT_EQ(err, hal::I2cError::Ok);
    ASSERT_EQ(test::g_i2cReadCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cReadCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c1));
    EXPECT_EQ(test::g_i2cReadCalls[0].addr, 0x50);
    EXPECT_EQ(test::g_i2cReadCalls[0].length, 3u);
}

TEST_F(I2cTest, ReadFillsBufferFromInjectableData)
{
    std::uint8_t buf[2] = {};
    test::g_i2cRxData = {0xDE, 0xAD};

    hal::i2cRead(hal::I2cId::I2c1, 0x50, buf, 2);

    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[1], 0xAD);
}

TEST_F(I2cTest, ReadReturnsInjectableError)
{
    test::g_i2cReturnError = static_cast<std::uint8_t>(hal::I2cError::BusError);

    std::uint8_t buf[1] = {};
    hal::I2cError err = hal::i2cRead(hal::I2cId::I2c1, 0x50, buf, 1);

    EXPECT_EQ(err, hal::I2cError::BusError);
}

TEST_F(I2cTest, ReadSingleByte)
{
    std::uint8_t buf[1] = {};
    test::g_i2cRxData = {0x42};

    hal::i2cRead(hal::I2cId::I2c1, 0x68, buf, 1);

    EXPECT_EQ(buf[0], 0x42);
}

// ---- WriteRead ----

TEST_F(I2cTest, WriteReadRecordsBothLengths)
{
    std::uint8_t tx[] = {0x00};
    std::uint8_t rx[2] = {};
    test::g_i2cRxData = {0x11, 0x22};

    hal::I2cError err = hal::i2cWriteRead(hal::I2cId::I2c1, 0x50, tx, 1, rx, 2);

    EXPECT_EQ(err, hal::I2cError::Ok);
    ASSERT_EQ(test::g_i2cWriteReadCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cWriteReadCalls[0].id, static_cast<std::uint8_t>(hal::I2cId::I2c1));
    EXPECT_EQ(test::g_i2cWriteReadCalls[0].addr, 0x50);
    EXPECT_EQ(test::g_i2cWriteReadCalls[0].txLength, 1u);
    EXPECT_EQ(test::g_i2cWriteReadCalls[0].rxLength, 2u);
}

TEST_F(I2cTest, WriteReadFillsRxBuffer)
{
    std::uint8_t tx[] = {0x0D};
    std::uint8_t rx[3] = {};
    test::g_i2cRxData = {0xA0, 0xB0, 0xC0};

    hal::i2cWriteRead(hal::I2cId::I2c1, 0x68, tx, 1, rx, 3);

    EXPECT_EQ(rx[0], 0xA0);
    EXPECT_EQ(rx[1], 0xB0);
    EXPECT_EQ(rx[2], 0xC0);
}

TEST_F(I2cTest, WriteReadReturnsInjectableError)
{
    test::g_i2cReturnError = static_cast<std::uint8_t>(hal::I2cError::ArbitrationLost);

    std::uint8_t tx[] = {0x00};
    std::uint8_t rx[1] = {};
    hal::I2cError err = hal::i2cWriteRead(hal::I2cId::I2c1, 0x50, tx, 1, rx, 1);

    EXPECT_EQ(err, hal::I2cError::ArbitrationLost);
}

// ---- Async write ----

TEST_F(I2cTest, AsyncWriteRecordsCall)
{
    auto cb = [](void *, hal::I2cError) {};
    std::uint8_t data[] = {0x01, 0x02};

    hal::i2cWriteAsync(hal::I2cId::I2c1, 0x50, data, 2, cb, nullptr);

    EXPECT_EQ(test::g_i2cAsyncWriteCount, 1u);
    ASSERT_EQ(test::g_i2cWriteCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cWriteCalls[0].length, 2u);
}

TEST_F(I2cTest, AsyncWriteInvokesCallback)
{
    bool called = false;
    auto cb = [](void *arg, hal::I2cError) { *static_cast<bool *>(arg) = true; };

    std::uint8_t data[] = {0x00};
    hal::i2cWriteAsync(hal::I2cId::I2c1, 0x50, data, 1, cb, &called);

    EXPECT_TRUE(called);
}

// ---- Async read ----

TEST_F(I2cTest, AsyncReadRecordsCall)
{
    auto cb = [](void *, hal::I2cError) {};
    std::uint8_t buf[4] = {};

    hal::i2cReadAsync(hal::I2cId::I2c2, 0x68, buf, 4, cb, nullptr);

    EXPECT_EQ(test::g_i2cAsyncReadCount, 1u);
    ASSERT_EQ(test::g_i2cReadCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cReadCalls[0].length, 4u);
}

TEST_F(I2cTest, AsyncReadFillsBuffer)
{
    std::uint8_t buf[2] = {};
    test::g_i2cRxData = {0xCA, 0xFE};

    hal::i2cReadAsync(hal::I2cId::I2c1, 0x50, buf, 2, nullptr, nullptr);

    EXPECT_EQ(buf[0], 0xCA);
    EXPECT_EQ(buf[1], 0xFE);
}

// ---- Mock state reset ----

TEST_F(I2cTest, MockStateResetsCleanly)
{
    hal::I2cConfig config{};
    config.id = hal::I2cId::I2c1;
    hal::i2cInit(config);
    test::g_i2cReturnError = 1;
    test::g_i2cRxData = {0x01};
    test::g_i2cAsyncWriteCount = 3;

    test::resetMockState();

    EXPECT_TRUE(test::g_i2cInitCalls.empty());
    EXPECT_TRUE(test::g_i2cWriteCalls.empty());
    EXPECT_TRUE(test::g_i2cReadCalls.empty());
    EXPECT_TRUE(test::g_i2cWriteReadCalls.empty());
    EXPECT_TRUE(test::g_i2cRxData.empty());
    EXPECT_EQ(test::g_i2cRxReadPos, 0u);
    EXPECT_EQ(test::g_i2cReturnError, 0u);
    EXPECT_EQ(test::g_i2cAsyncWriteCount, 0u);
    EXPECT_EQ(test::g_i2cAsyncReadCount, 0u);
}
