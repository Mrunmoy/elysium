#include <gtest/gtest.h>

#include "hal/Gpio.h"

#include "MockRegisters.h"

class GpioTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetMockState();
    }
};

TEST_F(GpioTest, InitRecordsPortAndPin)
{
    hal::GpioConfig config{};
    config.port = hal::Port::D;
    config.pin = 12;
    config.mode = hal::PinMode::Output;
    config.speed = hal::OutputSpeed::Low;
    config.outputType = hal::OutputType::PushPull;

    hal::gpioInit(config);

    ASSERT_EQ(test::g_gpioInitCalls.size(), 1u);
    EXPECT_EQ(test::g_gpioInitCalls[0].port, static_cast<std::uint8_t>(hal::Port::D));
    EXPECT_EQ(test::g_gpioInitCalls[0].pin, 12);
    EXPECT_EQ(test::g_gpioInitCalls[0].mode, static_cast<std::uint8_t>(hal::PinMode::Output));
}

TEST_F(GpioTest, InitRecordsAlternateFunction)
{
    hal::GpioConfig config{};
    config.port = hal::Port::A;
    config.pin = 2;
    config.mode = hal::PinMode::AlternateFunction;
    config.alternateFunction = 7;

    hal::gpioInit(config);

    ASSERT_EQ(test::g_gpioInitCalls.size(), 1u);
    EXPECT_EQ(
        test::g_gpioInitCalls[0].mode,
        static_cast<std::uint8_t>(hal::PinMode::AlternateFunction));
    EXPECT_EQ(test::g_gpioInitCalls[0].alternateFunction, 7);
}

TEST_F(GpioTest, SetRecordsAction)
{
    hal::gpioSet(hal::Port::D, 12);

    ASSERT_EQ(test::g_gpioPinActions.size(), 1u);
    EXPECT_EQ(test::g_gpioPinActions[0].type, test::GpioPinAction::Type::Set);
    EXPECT_EQ(test::g_gpioPinActions[0].port, static_cast<std::uint8_t>(hal::Port::D));
    EXPECT_EQ(test::g_gpioPinActions[0].pin, 12);
}

TEST_F(GpioTest, ClearRecordsAction)
{
    hal::gpioClear(hal::Port::B, 5);

    ASSERT_EQ(test::g_gpioPinActions.size(), 1u);
    EXPECT_EQ(test::g_gpioPinActions[0].type, test::GpioPinAction::Type::Clear);
    EXPECT_EQ(test::g_gpioPinActions[0].port, static_cast<std::uint8_t>(hal::Port::B));
    EXPECT_EQ(test::g_gpioPinActions[0].pin, 5);
}

TEST_F(GpioTest, ToggleRecordsAction)
{
    hal::gpioToggle(hal::Port::C, 3);

    ASSERT_EQ(test::g_gpioPinActions.size(), 1u);
    EXPECT_EQ(test::g_gpioPinActions[0].type, test::GpioPinAction::Type::Toggle);
}

TEST_F(GpioTest, ReadReturnsConfiguredValue)
{
    test::g_gpioReadValue = true;
    EXPECT_TRUE(hal::gpioRead(hal::Port::A, 0));

    test::g_gpioReadValue = false;
    EXPECT_FALSE(hal::gpioRead(hal::Port::A, 0));
}

TEST_F(GpioTest, MultipleActionsRecordInOrder)
{
    hal::gpioSet(hal::Port::D, 12);
    hal::gpioClear(hal::Port::D, 12);
    hal::gpioToggle(hal::Port::D, 12);

    ASSERT_EQ(test::g_gpioPinActions.size(), 3u);
    EXPECT_EQ(test::g_gpioPinActions[0].type, test::GpioPinAction::Type::Set);
    EXPECT_EQ(test::g_gpioPinActions[1].type, test::GpioPinAction::Type::Clear);
    EXPECT_EQ(test::g_gpioPinActions[2].type, test::GpioPinAction::Type::Toggle);
}
