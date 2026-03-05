// I2C1 Board-to-Board Test Runner -- Phase 15
//
// Runs on Board 1 (J-Link). Sends test patterns on I2C1 (master) to Board 2
// (slave echo server at address 0x44), verifies echoed data.
// Results printed on USART1 console. LED on PC13 toggles on each test.
//
// Echo protocol:
//   1. Master writes N bytes to slave (address 0x44)
//   2. Master reads N bytes back from slave
//   3. Verify: received data matches sent data
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

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    constexpr std::uint8_t kSlaveAddr = 0x44;
    constexpr std::uint8_t kBme680Addr = 0x77;

    hal::UartId g_consoleUart;

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
    }

    void printHex(std::uint8_t val)
    {
        const char hex[] = "0123456789ABCDEF";
        hal::uartPutChar(g_consoleUart, hex[val >> 4]);
        hal::uartPutChar(g_consoleUart, hex[val & 0x0F]);
    }

    void printDecimal(std::uint32_t value)
    {
        if (value == 0)
        {
            hal::uartPutChar(g_consoleUart, '0');
            return;
        }

        char buf[10];
        int pos = 0;
        while (value > 0 && pos < 10)
        {
            buf[pos++] = '0' + static_cast<char>(value % 10);
            value /= 10;
        }
        for (int i = pos - 1; i >= 0; --i)
        {
            hal::uartPutChar(g_consoleUart, buf[i]);
        }
    }

    void printResult(const char *testName, bool pass)
    {
        print(testName);
        print(pass ? ": PASS\r\n" : ": FAIL\r\n");
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

    void initI2c1Master()
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

        // Init I2C1 as master, standard mode
        hal::I2cConfig i2cConfig{};
        i2cConfig.id = hal::I2cId::I2c1;
        i2cConfig.speed = hal::I2cSpeed::Standard;
        hal::i2cInit(i2cConfig);
    }

    // Busy-wait delay (~1ms at 168 MHz)
    void delayMs(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            for (volatile std::uint32_t j = 0; j < 33600; ++j)
            {
            }
        }
    }

    // Write-then-read echo test helper.
    // Returns true if all bytes echo back correctly.
    bool echoTest(const std::uint8_t *txData, std::size_t length)
    {
        // Write to slave
        hal::I2cError err = hal::i2cWrite(hal::I2cId::I2c1, kSlaveAddr,
                                           txData, length);
        if (err != hal::I2cError::Ok)
        {
            print("  [write error: 0x");
            printHex(static_cast<std::uint8_t>(err));
            print("]\r\n");
            return false;
        }

        // Short delay for slave to process
        delayMs(5);

        // Read back
        std::uint8_t rxBuf[256];
        err = hal::i2cRead(hal::I2cId::I2c1, kSlaveAddr, rxBuf, length);
        if (err != hal::I2cError::Ok)
        {
            print("  [read error: 0x");
            printHex(static_cast<std::uint8_t>(err));
            print("]\r\n");
            return false;
        }

        // Verify
        for (std::size_t i = 0; i < length; ++i)
        {
            if (rxBuf[i] != txData[i])
            {
                print("  [byte ");
                printDecimal(static_cast<std::uint32_t>(i));
                print(": expected 0x");
                printHex(txData[i]);
                print(", got 0x");
                printHex(rxBuf[i]);
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    // --- Test 1: Single byte echo ---
    bool testSingleByte()
    {
        const std::uint8_t tx[] = {0xA5};
        return echoTest(tx, 1);
    }

    // --- Test 2: Multi-byte echo (4 bytes) ---
    bool testMultiByte()
    {
        const std::uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};
        return echoTest(tx, 4);
    }

    // --- Test 3: Sequential (0x00-0xFF) ---
    bool testSequential()
    {
        std::uint8_t tx[256];
        for (std::uint16_t i = 0; i < 256; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i);
        }
        return echoTest(tx, 256);
    }

    // --- Test 4: Burst (16 bytes) ---
    bool testBurst()
    {
        std::uint8_t tx[16];
        for (std::uint8_t i = 0; i < 16; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i * 17 + 0x10);
        }
        return echoTest(tx, 16);
    }

    // --- Test 5: Stress (64 bytes) ---
    bool testStress()
    {
        std::uint8_t tx[64];
        for (std::uint8_t i = 0; i < 64; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i ^ 0x55);
        }
        return echoTest(tx, 64);
    }

    // --- Test 6: BME680 chip ID ---
    bool testBme680ChipId()
    {
        // Write register address 0xD0 (chip ID register)
        const std::uint8_t regAddr = 0xD0;
        std::uint8_t chipId = 0;

        hal::I2cError err = hal::i2cWriteRead(hal::I2cId::I2c1, kBme680Addr,
                                               &regAddr, 1, &chipId, 1);
        if (err != hal::I2cError::Ok)
        {
            print("  [BME680 error: 0x");
            printHex(static_cast<std::uint8_t>(err));
            print("]\r\n");
            return false;
        }

        print("  [chip ID: 0x");
        printHex(chipId);
        print("]\r\n");

        return (chipId == 0x61);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== I2C1 Board-to-Board Test (Phase 15) ===\r\n");
    print("Board 1: I2C1 master test runner, LED on PC13\r\n");
    print("Pins: PB6(SCL) PB7(SDA)\r\n\r\n");

    initI2c1Master();

    // Wait for slave echo server on Board 2 to be ready
    print("Waiting for slave...\r\n");
    delayMs(500);

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 6;

    bool r1 = testSingleByte();
    printResult("Test 1: Single byte echo", r1);
    if (r1) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    delayMs(10);

    bool r2 = testMultiByte();
    printResult("Test 2: Multi-byte echo (4 bytes)", r2);
    if (r2) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    delayMs(10);

    bool r3 = testSequential();
    printResult("Test 3: Sequential echo (0x00-0xFF)", r3);
    if (r3) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    delayMs(10);

    bool r4 = testBurst();
    printResult("Test 4: Burst echo (16 bytes)", r4);
    if (r4) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    delayMs(10);

    bool r5 = testStress();
    printResult("Test 5: Stress echo (64 bytes)", r5);
    if (r5) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    delayMs(10);

    bool r6 = testBme680ChipId();
    printResult("Test 6: BME680 chip ID (0x77)", r6);
    if (r6) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    print("\r\n--- Summary: ");
    hal::uartPutChar(g_consoleUart, '0' + static_cast<char>(pass));
    print("/");
    hal::uartPutChar(g_consoleUart, '0' + static_cast<char>(kTotal));
    print(" passed");
    print(pass == kTotal ? " (ALL PASS)" : " (SOME FAILED)");
    print(" ---\r\n");

    while (true)
    {
        __asm volatile("wfi");
    }
}
