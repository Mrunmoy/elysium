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

TEST_F(I2cTest, WriteInvalidIdReturnsInvalid)
{
    std::uint8_t data[] = {0x12};
    hal::I2cError err = hal::i2cWrite(static_cast<hal::I2cId>(99), 0x50, data, 1);

    EXPECT_EQ(err, hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cWriteCalls.empty());
}

TEST_F(I2cTest, WriteNullDataWithLengthReturnsInvalid)
{
    hal::I2cError err = hal::i2cWrite(hal::I2cId::I2c1, 0x50, nullptr, 1);
    EXPECT_EQ(err, hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cWriteCalls.empty());
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

TEST_F(I2cTest, ReadInvalidIdReturnsInvalid)
{
    std::uint8_t buf[1] = {};
    hal::I2cError err = hal::i2cRead(static_cast<hal::I2cId>(99), 0x50, buf, 1);

    EXPECT_EQ(err, hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cReadCalls.empty());
}

TEST_F(I2cTest, ReadNullBufferWithLengthReturnsInvalid)
{
    hal::I2cError err = hal::i2cRead(hal::I2cId::I2c1, 0x50, nullptr, 1);
    EXPECT_EQ(err, hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cReadCalls.empty());
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

TEST_F(I2cTest, WriteReadInvalidIdReturnsInvalid)
{
    std::uint8_t tx[] = {0x00};
    std::uint8_t rx[1] = {};
    hal::I2cError err = hal::i2cWriteRead(static_cast<hal::I2cId>(99), 0x50,
                                          tx, 1, rx, 1);

    EXPECT_EQ(err, hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cWriteReadCalls.empty());
}

TEST_F(I2cTest, WriteReadReturnsInjectableError)
{
    test::g_i2cReturnError = static_cast<std::uint8_t>(hal::I2cError::ArbitrationLost);

    std::uint8_t tx[] = {0x00};
    std::uint8_t rx[1] = {};
    hal::I2cError err = hal::i2cWriteRead(hal::I2cId::I2c1, 0x50, tx, 1, rx, 1);

    EXPECT_EQ(err, hal::I2cError::ArbitrationLost);
}

TEST_F(I2cTest, WriteReadNullPointersReturnInvalid)
{
    std::uint8_t rx[1] = {};
    std::uint8_t tx[1] = {0x00};

    EXPECT_EQ(hal::i2cWriteRead(hal::I2cId::I2c1, 0x50, nullptr, 1, rx, 1),
              hal::I2cError::Invalid);
    EXPECT_EQ(hal::i2cWriteRead(hal::I2cId::I2c1, 0x50, tx, 1, nullptr, 1),
              hal::I2cError::Invalid);
    EXPECT_TRUE(test::g_i2cWriteReadCalls.empty());
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

TEST_F(I2cTest, AsyncWriteZeroLengthDoesNothing)
{
    bool called = false;
    auto cb = [](void *arg, hal::I2cError) { *static_cast<bool *>(arg) = true; };

    std::uint8_t data[] = {0xAA};
    hal::i2cWriteAsync(hal::I2cId::I2c1, 0x50, data, 0, cb, &called);

    EXPECT_FALSE(called);
    EXPECT_EQ(test::g_i2cAsyncWriteCount, 0u);
    EXPECT_TRUE(test::g_i2cWriteCalls.empty());
}

TEST_F(I2cTest, AsyncWriteInvalidIdInvokesInvalidCallback)
{
    struct CallbackState
    {
        bool called = false;
        hal::I2cError error = hal::I2cError::Ok;
    };

    auto cb = [](void *arg, hal::I2cError err)
    {
        auto *state = static_cast<CallbackState *>(arg);
        state->called = true;
        state->error = err;
    };

    CallbackState state;
    std::uint8_t data[] = {0xAA};
    hal::i2cWriteAsync(static_cast<hal::I2cId>(99), 0x50, data, 1, cb, &state);

    EXPECT_TRUE(state.called);
    EXPECT_EQ(state.error, hal::I2cError::Invalid);
    EXPECT_EQ(test::g_i2cAsyncWriteCount, 0u);
    EXPECT_TRUE(test::g_i2cWriteCalls.empty());
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

TEST_F(I2cTest, AsyncReadZeroLengthDoesNothing)
{
    bool called = false;
    auto cb = [](void *arg, hal::I2cError) { *static_cast<bool *>(arg) = true; };

    std::uint8_t buf[2] = {};
    hal::i2cReadAsync(hal::I2cId::I2c1, 0x50, buf, 0, cb, &called);

    EXPECT_FALSE(called);
    EXPECT_EQ(test::g_i2cAsyncReadCount, 0u);
    EXPECT_TRUE(test::g_i2cReadCalls.empty());
}

TEST_F(I2cTest, AsyncReadNullBufferInvokesInvalidCallback)
{
    struct CallbackState
    {
        bool called = false;
        hal::I2cError error = hal::I2cError::Ok;
    };

    auto cb = [](void *arg, hal::I2cError err)
    {
        auto *state = static_cast<CallbackState *>(arg);
        state->called = true;
        state->error = err;
    };

    CallbackState state;
    hal::i2cReadAsync(hal::I2cId::I2c1, 0x50, nullptr, 1, cb, &state);

    EXPECT_TRUE(state.called);
    EXPECT_EQ(state.error, hal::I2cError::Invalid);
    EXPECT_EQ(test::g_i2cAsyncReadCount, 0u);
    EXPECT_TRUE(test::g_i2cReadCalls.empty());
}

// ---- Slave init ----

TEST_F(I2cTest, I2cSlaveInitRecordsOwnAddr)
{
    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, nullptr);

    ASSERT_EQ(test::g_i2cSlaveInitCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cSlaveInitCalls[0].id,
              static_cast<std::uint8_t>(hal::I2cId::I2c1));
    EXPECT_EQ(test::g_i2cSlaveInitCalls[0].ownAddr, 0x44);
}

TEST_F(I2cTest, I2cSlaveInitRecordsCallbacks)
{
    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, nullptr);

    ASSERT_EQ(test::g_i2cSlaveInitCalls.size(), 1u);
    EXPECT_NE(test::g_i2cSlaveInitCalls[0].rxCallback, nullptr);
    EXPECT_NE(test::g_i2cSlaveInitCalls[0].txCallback, nullptr);
    EXPECT_EQ(test::g_i2cSlaveRxCallback,
              test::g_i2cSlaveInitCalls[0].rxCallback);
    EXPECT_EQ(test::g_i2cSlaveTxCallback,
              test::g_i2cSlaveInitCalls[0].txCallback);
}

TEST_F(I2cTest, I2cSlaveInitStoresArg)
{
    int dummy = 42;
    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    hal::i2cSlaveInit(hal::I2cId::I2c2, 0x50, rxCb, txCb, &dummy);

    ASSERT_EQ(test::g_i2cSlaveInitCalls.size(), 1u);
    EXPECT_EQ(test::g_i2cSlaveInitCalls[0].arg, &dummy);
    EXPECT_EQ(test::g_i2cSlaveArg, &dummy);
}

TEST_F(I2cTest, I2cSlaveMultipleInits)
{
    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, nullptr);
    hal::i2cSlaveInit(hal::I2cId::I2c2, 0x55, rxCb, txCb, nullptr);

    ASSERT_EQ(test::g_i2cSlaveInitCalls.size(), 2u);
    EXPECT_EQ(test::g_i2cSlaveInitCalls[0].ownAddr, 0x44);
    EXPECT_EQ(test::g_i2cSlaveInitCalls[1].ownAddr, 0x55);
}

// ---- Slave enable/disable ----

TEST_F(I2cTest, I2cSlaveEnableRecordsCall)
{
    hal::i2cSlaveEnable(hal::I2cId::I2c1);

    EXPECT_EQ(test::g_i2cSlaveEnableCount, 1u);
    EXPECT_TRUE(test::g_i2cSlaveActive);
}

TEST_F(I2cTest, I2cSlaveDisableRecordsCall)
{
    test::g_i2cSlaveActive = true;
    hal::i2cSlaveDisable(hal::I2cId::I2c1);

    EXPECT_EQ(test::g_i2cSlaveDisableCount, 1u);
    EXPECT_FALSE(test::g_i2cSlaveActive);
}

TEST_F(I2cTest, I2cSlaveEnableThenDisableClearsState)
{
    hal::i2cSlaveEnable(hal::I2cId::I2c1);
    EXPECT_TRUE(test::g_i2cSlaveActive);

    hal::i2cSlaveDisable(hal::I2cId::I2c1);
    EXPECT_FALSE(test::g_i2cSlaveActive);

    EXPECT_EQ(test::g_i2cSlaveEnableCount, 1u);
    EXPECT_EQ(test::g_i2cSlaveDisableCount, 1u);
}

TEST_F(I2cTest, I2cSlaveInvalidIdDoesNothing)
{
    hal::i2cSlaveEnable(static_cast<hal::I2cId>(99));
    hal::i2cSlaveDisable(static_cast<hal::I2cId>(99));

    EXPECT_EQ(test::g_i2cSlaveEnableCount, 0u);
    EXPECT_EQ(test::g_i2cSlaveDisableCount, 0u);
    EXPECT_FALSE(test::g_i2cSlaveActive);
}

// ---- Slave callback simulation ----

TEST_F(I2cTest, I2cSlaveRxCallbackInvoked)
{
    bool called = false;
    std::size_t rxLen = 0;

    auto rxCb = [](void *arg, const std::uint8_t * /* data */, std::size_t length)
    {
        auto *info = static_cast<std::pair<bool *, std::size_t *> *>(arg);
        *info->first = true;
        *info->second = length;
    };
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    std::pair<bool *, std::size_t *> info(&called, &rxLen);
    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, &info);

    // Simulate ISR invoking the stored callback
    auto fn = reinterpret_cast<hal::I2cSlaveRxCallbackFn>(test::g_i2cSlaveRxCallback);
    std::uint8_t data[] = {0xAB, 0xCD};
    fn(test::g_i2cSlaveArg, data, 2);

    EXPECT_TRUE(called);
    EXPECT_EQ(rxLen, 2u);
}

TEST_F(I2cTest, I2cSlaveTxCallbackInvoked)
{
    bool called = false;

    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *arg, std::uint8_t *data, std::size_t *length, std::size_t maxLength)
    {
        *static_cast<bool *>(arg) = true;
        if (maxLength >= 2)
        {
            data[0] = 0xDE;
            data[1] = 0xAD;
            *length = 2;
        }
    };

    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, &called);

    // Simulate ISR invoking the stored TX callback
    auto fn = reinterpret_cast<hal::I2cSlaveTxCallbackFn>(test::g_i2cSlaveTxCallback);
    std::uint8_t buf[4] = {};
    std::size_t len = 0;
    fn(test::g_i2cSlaveArg, buf, &len, 4);

    EXPECT_TRUE(called);
    EXPECT_EQ(len, 2u);
    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[1], 0xAD);
}

// ---- Slave mock state reset ----

TEST_F(I2cTest, I2cSlaveStateResetsCleanly)
{
    auto rxCb = [](void *, const std::uint8_t *, std::size_t) {};
    auto txCb = [](void *, std::uint8_t *, std::size_t *, std::size_t) {};

    hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, rxCb, txCb, nullptr);
    hal::i2cSlaveEnable(hal::I2cId::I2c1);

    test::resetMockState();

    EXPECT_TRUE(test::g_i2cSlaveInitCalls.empty());
    EXPECT_EQ(test::g_i2cSlaveEnableCount, 0u);
    EXPECT_EQ(test::g_i2cSlaveDisableCount, 0u);
    EXPECT_FALSE(test::g_i2cSlaveActive);
    EXPECT_EQ(test::g_i2cSlaveRxCallback, nullptr);
    EXPECT_EQ(test::g_i2cSlaveTxCallback, nullptr);
    EXPECT_EQ(test::g_i2cSlaveArg, nullptr);
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
