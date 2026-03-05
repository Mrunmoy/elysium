// I2C1 Slave Echo Server -- Phase 15 board-to-board integration test
//
// Runs on Board 2 (ST-Link V2). Echoes received I2C data back on read.
// Uses interrupt-driven slave mode at address 0x44.
// LED on PC13 toggles on each received message for visual activity indicator.
//
// Board: STM32F407ZGT6
//   I2C1: PB6 (SCL, AF4, open-drain), PB7 (SDA, AF4, open-drain)
//   LED: PC13 (active low)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/I2c.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>
#include <cstring>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    hal::UartId g_consoleUart;

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
    }

    void initConsole()
    {
        const board::BoardConfig &cfg = board::config();

        if (cfg.hasConsoleTx)
        {
            hal::rccEnableGpioClock(hal::Port(cfg.consoleTx.port - 'A'));

            hal::GpioConfig txConfig{};
            txConfig.port = hal::Port(cfg.consoleTx.port - 'A');
            txConfig.pin = cfg.consoleTx.pin;
            txConfig.mode = hal::PinMode::AlternateFunction;
            txConfig.speed = hal::OutputSpeed::VeryHigh;
            txConfig.alternateFunction = cfg.consoleTx.af;
            hal::gpioInit(txConfig);
        }

        g_consoleUart = board::consoleUartId();
        hal::rccEnableUartClock(g_consoleUart);

        hal::UartConfig uartConfig{};
        uartConfig.id = g_consoleUart;
        uartConfig.baudRate = cfg.consoleBaud;
        hal::uartInit(uartConfig);
    }

    void initLed()
    {
        hal::rccEnableGpioClock(hal::Port::C);

        hal::GpioConfig ledConfig{};
        ledConfig.port = hal::Port::C;
        ledConfig.pin = 13;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        hal::gpioInit(ledConfig);
    }

    // Echo state: shared between RX and TX callbacks
    struct EchoState
    {
        std::uint8_t buf[256];
        std::size_t length = 0;
        std::uint32_t rxCount = 0;
    };

    EchoState s_echo;

    // I2C slave RX callback: master wrote data to us
    void i2cSlaveRxCb(void *arg, const std::uint8_t *data, std::size_t length)
    {
        auto *echo = static_cast<EchoState *>(arg);
        std::size_t copyLen = (length <= sizeof(echo->buf)) ? length : sizeof(echo->buf);
        std::memcpy(echo->buf, data, copyLen);
        echo->length = copyLen;
        ++echo->rxCount;
        hal::gpioToggle(hal::Port::C, 13);
    }

    // I2C slave TX callback: master wants to read data from us
    void i2cSlaveTxCb(void *arg, std::uint8_t *data, std::size_t *length,
                      std::size_t maxLength)
    {
        auto *echo = static_cast<EchoState *>(arg);
        std::size_t copyLen = echo->length;
        if (copyLen > maxLength)
        {
            copyLen = maxLength;
        }
        std::memcpy(data, echo->buf, copyLen);
        *length = copyLen;
    }

    void initI2c1Slave()
    {
        // Enable clocks
        hal::rccEnableGpioClock(hal::Port::B);
        hal::rccEnableI2cClock(hal::I2cId::I2c1);

        // PB6 = I2C1_SCL (AF4) -- open-drain, pull-up
        hal::GpioConfig sclConfig{};
        sclConfig.port = hal::Port::B;
        sclConfig.pin = 6;
        sclConfig.mode = hal::PinMode::AlternateFunction;
        sclConfig.outputType = hal::OutputType::OpenDrain;
        sclConfig.pull = hal::PullMode::Up;
        sclConfig.speed = hal::OutputSpeed::VeryHigh;
        sclConfig.alternateFunction = 4;
        hal::gpioInit(sclConfig);

        // PB7 = I2C1_SDA (AF4) -- open-drain, pull-up
        hal::GpioConfig sdaConfig{};
        sdaConfig.port = hal::Port::B;
        sdaConfig.pin = 7;
        sdaConfig.mode = hal::PinMode::AlternateFunction;
        sdaConfig.outputType = hal::OutputType::OpenDrain;
        sdaConfig.pull = hal::PullMode::Up;
        sdaConfig.speed = hal::OutputSpeed::VeryHigh;
        sdaConfig.alternateFunction = 4;
        hal::gpioInit(sdaConfig);

        // Init I2C1 as slave at address 0x44
        hal::i2cSlaveInit(hal::I2cId::I2c1, 0x44, i2cSlaveRxCb, i2cSlaveTxCb,
                          &s_echo);

        // Enable slave interrupts
        hal::i2cSlaveEnable(hal::I2cId::I2c1);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== I2C1 Slave Echo Server (Phase 15) ===\r\n");
    print("Board 2: I2C1 slave echo at addr 0x44, LED on PC13\r\n");
    print("Pins: PB6(SCL) PB7(SDA)\r\n\r\n");

    initI2c1Slave();

    print("I2C1 Slave Echo Server ready\r\n");

    // LED OFF at start (active low: set = OFF)
    hal::gpioSet(hal::Port::C, 13);

    while (true)
    {
        __asm volatile("wfi");
    }
}
