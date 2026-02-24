#include <gtest/gtest.h>

#include "hal/Uart.h"

#include "MockRegisters.h"

class UartTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

// ---- Init ----

TEST_F(UartTest, InitRecordsIdAndBaud)
{
    hal::UartConfig config{};
    config.id = hal::UartId::Usart1;
    config.baudRate = 115200;

    hal::uartInit(config);

    ASSERT_EQ(test::g_uartInitCalls.size(), 1u);
    EXPECT_EQ(test::g_uartInitCalls[0].id, static_cast<std::uint8_t>(hal::UartId::Usart1));
    EXPECT_EQ(test::g_uartInitCalls[0].baudRate, 115200u);
}

TEST_F(UartTest, InitRecordsDifferentBaud)
{
    hal::UartConfig config{};
    config.id = hal::UartId::Usart2;
    config.baudRate = 9600;

    hal::uartInit(config);

    ASSERT_EQ(test::g_uartInitCalls.size(), 1u);
    EXPECT_EQ(test::g_uartInitCalls[0].id, static_cast<std::uint8_t>(hal::UartId::Usart2));
    EXPECT_EQ(test::g_uartInitCalls[0].baudRate, 9600u);
}

// ---- PutChar / WriteString ----

TEST_F(UartTest, PutCharRecordsIdAndChar)
{
    hal::uartPutChar(hal::UartId::Usart1, 'A');

    ASSERT_EQ(test::g_uartPutCharCalls.size(), 1u);
    EXPECT_EQ(test::g_uartPutCharCalls[0].id, static_cast<std::uint8_t>(hal::UartId::Usart1));
    EXPECT_EQ(test::g_uartPutCharCalls[0].c, 'A');
}

TEST_F(UartTest, WriteStringRecordsAllChars)
{
    hal::uartWriteString(hal::UartId::Usart1, "Hi");

    ASSERT_EQ(test::g_uartPutCharCalls.size(), 2u);
    EXPECT_EQ(test::g_uartPutCharCalls[0].c, 'H');
    EXPECT_EQ(test::g_uartPutCharCalls[1].c, 'i');
}

// ---- TryGetChar ----

TEST_F(UartTest, TryGetCharReturnsFalseWhenEmpty)
{
    char c = 0;
    EXPECT_FALSE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
}

TEST_F(UartTest, TryGetCharReturnsTrueWithData)
{
    test::g_uartRxBuffer = {'X'};
    char c = 0;
    EXPECT_TRUE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
    EXPECT_EQ(c, 'X');
}

TEST_F(UartTest, TryGetCharReadsInOrder)
{
    test::g_uartRxBuffer = {'A', 'B', 'C'};

    char c = 0;
    EXPECT_TRUE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
    EXPECT_EQ(c, 'A');
    EXPECT_TRUE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
    EXPECT_EQ(c, 'B');
    EXPECT_TRUE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
    EXPECT_EQ(c, 'C');
    EXPECT_FALSE(hal::uartTryGetChar(hal::UartId::Usart1, &c));
}

// ---- GetChar ----

TEST_F(UartTest, GetCharReturnsBufferedData)
{
    test::g_uartRxBuffer = {'Z'};
    EXPECT_EQ(hal::uartGetChar(hal::UartId::Usart1), 'Z');
}

// ---- RxInterruptEnable ----

static void dummyNotify(void *) {}

TEST_F(UartTest, RxInterruptEnableRecordsCallbackAndArg)
{
    int dummy = 42;
    hal::uartRxInterruptEnable(hal::UartId::Usart1, dummyNotify, &dummy);

    ASSERT_EQ(test::g_uartRxEnableCalls.size(), 1u);
    EXPECT_EQ(test::g_uartRxEnableCalls[0].id, static_cast<std::uint8_t>(hal::UartId::Usart1));
    EXPECT_EQ(test::g_uartRxEnableCalls[0].notifyFn, reinterpret_cast<void *>(dummyNotify));
    EXPECT_EQ(test::g_uartRxEnableCalls[0].notifyArg, &dummy);
}

TEST_F(UartTest, RxInterruptEnableSetsEnabledFlag)
{
    EXPECT_FALSE(test::g_uartRxInterruptEnabled);
    hal::uartRxInterruptEnable(hal::UartId::Usart1, nullptr, nullptr);
    EXPECT_TRUE(test::g_uartRxInterruptEnabled);
}

// ---- RxInterruptDisable ----

TEST_F(UartTest, RxInterruptDisableClearsFlag)
{
    hal::uartRxInterruptEnable(hal::UartId::Usart1, nullptr, nullptr);
    EXPECT_TRUE(test::g_uartRxInterruptEnabled);

    hal::uartRxInterruptDisable(hal::UartId::Usart1);
    EXPECT_FALSE(test::g_uartRxInterruptEnabled);
}

TEST_F(UartTest, RxInterruptDisableIncrementsCount)
{
    hal::uartRxInterruptDisable(hal::UartId::Usart1);
    hal::uartRxInterruptDisable(hal::UartId::Usart2);

    EXPECT_EQ(test::g_uartRxInterruptDisableCount, 2u);
}

// ---- RxBufferCount ----

TEST_F(UartTest, RxBufferCountReturnsZeroWhenEmpty)
{
    EXPECT_EQ(hal::uartRxBufferCount(hal::UartId::Usart1), 0u);
}

TEST_F(UartTest, RxBufferCountTracksAvailableData)
{
    test::g_uartRxBuffer = {'a', 'b', 'c'};
    EXPECT_EQ(hal::uartRxBufferCount(hal::UartId::Usart1), 3u);

    char c;
    hal::uartTryGetChar(hal::UartId::Usart1, &c);
    EXPECT_EQ(hal::uartRxBufferCount(hal::UartId::Usart1), 2u);
}

// ---- Mock state reset ----

TEST_F(UartTest, MockStateResetsCleanly)
{
    hal::uartPutChar(hal::UartId::Usart1, 'X');
    hal::uartRxInterruptEnable(hal::UartId::Usart1, nullptr, nullptr);
    hal::uartRxInterruptDisable(hal::UartId::Usart1);

    test::resetMockState();

    EXPECT_TRUE(test::g_uartPutCharCalls.empty());
    EXPECT_TRUE(test::g_uartRxEnableCalls.empty());
    EXPECT_FALSE(test::g_uartRxInterruptEnabled);
    EXPECT_EQ(test::g_uartRxInterruptDisableCount, 0u);
    EXPECT_TRUE(test::g_uartRxBuffer.empty());
    EXPECT_EQ(test::g_uartRxReadPos, 0u);
}
