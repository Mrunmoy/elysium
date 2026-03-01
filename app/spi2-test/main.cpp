// SPI1 Board-to-Board Test Runner -- Phase 14
//
// Runs on Board 1 (J-Link). Sends test patterns on SPI1 (master) to Board 2
// (slave echo server), verifies echoed data. Results printed on USART1 console.
// LED on PC13 toggles on each test for visual feedback.
//
// SPI echo protocol (1-byte offset):
//   Master sends PRIME byte, then data bytes, then a DUMMY byte.
//   Slave echoes previous byte on each transfer.
//   Master verifies: rx[i] == tx[i-1] for i>0, and rxLast == tx[N-1].
//
// Board: STM32F407ZGT6
//   SPI1: PA5 (SCK, AF5), PA6 (MISO, AF5), PA7 (MOSI, AF5)
//   LED: PC13 (active low)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Spi.h"
#include "hal/Uart.h"

#include <cstdint>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
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

    void initSpi1Master()
    {
        // Enable clocks
        hal::rccEnableGpioClock(hal::Port::A);
        hal::rccEnableSpiClock(hal::SpiId::Spi1);

        // PA5 = SPI1_SCK (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig sckConfig{};
        sckConfig.port = hal::Port::A;
        sckConfig.pin = 5;
        sckConfig.mode = hal::PinMode::AlternateFunction;
        sckConfig.alternateFunction = 5;
        hal::gpioInit(sckConfig);

        // PA6 = SPI1_MISO (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig misoConfig{};
        misoConfig.port = hal::Port::A;
        misoConfig.pin = 6;
        misoConfig.mode = hal::PinMode::AlternateFunction;
        misoConfig.alternateFunction = 5;
        hal::gpioInit(misoConfig);

        // PA7 = SPI1_MOSI (AF5) -- AF push-pull, no pull, low speed
        hal::GpioConfig mosiConfig{};
        mosiConfig.port = hal::Port::A;
        mosiConfig.pin = 7;
        mosiConfig.mode = hal::PinMode::AlternateFunction;
        mosiConfig.alternateFunction = 5;
        hal::gpioInit(mosiConfig);

        // Configure SPI1 as master: mode 0, software NSS, Div256 for slow clock
        // APB2 = 84 MHz, Div256 -> 328 kHz SCK (gives slave time to process)
        hal::SpiConfig spiConfig{};
        spiConfig.id = hal::SpiId::Spi1;
        spiConfig.mode = hal::SpiMode::Mode0;
        spiConfig.prescaler = hal::SpiBaudPrescaler::Div256;
        spiConfig.dataSize = hal::SpiDataSize::Bits8;
        spiConfig.bitOrder = hal::SpiBitOrder::MsbFirst;
        spiConfig.master = true;
        spiConfig.softwareNss = true;
        hal::spiInit(spiConfig);
    }

    // Short delay between SPI transfers to let slave ISR process
    void spiDelay()
    {
        for (volatile std::uint32_t i = 0; i < 1000; ++i)
        {
        }
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

    // --- Test 1: Single byte echo ---
    // Send PRIME, data byte, DUMMY. Verify echo of data byte.
    bool testSingleByte()
    {
        const std::uint8_t kPrime = 0xA5;

        // Prime: send 0xA5, discard RX (initial 0x00 or junk)
        hal::spiTransferByte(hal::SpiId::Spi1, kPrime);
        spiDelay();

        // Send dummy to clock out the echo of PRIME
        std::uint8_t rxPrime = hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        if (rxPrime != kPrime)
        {
            print("  [expected 0x");
            printHex(kPrime);
            print(", got 0x");
            printHex(rxPrime);
            print("]\r\n");
            return false;
        }
        return true;
    }

    // --- Test 2: Multi-byte echo (4 bytes) ---
    bool testMultiByte()
    {
        const std::uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};
        constexpr std::uint8_t kLen = 4;

        // Prime: send first byte, discard RX
        hal::spiTransferByte(hal::SpiId::Spi1, tx[0]);
        spiDelay();

        // Send remaining bytes, each RX is echo of previous TX
        for (std::uint8_t i = 1; i < kLen; ++i)
        {
            std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, tx[i]);
            spiDelay();

            if (rx != tx[i - 1])
            {
                print("  [byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i - 1]);
                print(", got 0x");
                printHex(rx);
                print("]\r\n");
                return false;
            }
        }

        // Send dummy to get echo of last byte
        std::uint8_t rxLast = hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        if (rxLast != tx[kLen - 1])
        {
            print("  [last: expected 0x");
            printHex(tx[kLen - 1]);
            print(", got 0x");
            printHex(rxLast);
            print("]\r\n");
            return false;
        }
        return true;
    }

    // --- Test 3: Sequential (0x00-0xFF) ---
    bool testSequential()
    {
        // Prime: send 0x00, discard RX
        hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        // Send 0x01-0xFF, each RX should be the previous byte
        for (std::uint16_t i = 1; i < 256; ++i)
        {
            std::uint8_t val = static_cast<std::uint8_t>(i);
            std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, val);
            spiDelay();

            std::uint8_t expected = static_cast<std::uint8_t>(i - 1);
            if (rx != expected)
            {
                print("  [0x");
                printHex(val);
                print(": expected 0x");
                printHex(expected);
                print(", got 0x");
                printHex(rx);
                print("]\r\n");
                return false;
            }
        }

        // Get echo of 0xFF
        std::uint8_t rxLast = hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        if (rxLast != 0xFF)
        {
            print("  [last: expected 0xFF, got 0x");
            printHex(rxLast);
            print("]\r\n");
            return false;
        }
        return true;
    }

    // --- Test 4: Burst (16 bytes) ---
    bool testBurst()
    {
        constexpr std::uint8_t kBurstLen = 16;
        std::uint8_t tx[kBurstLen];

        for (std::uint8_t i = 0; i < kBurstLen; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i * 17 + 0x10);
        }

        // Prime: send first byte
        hal::spiTransferByte(hal::SpiId::Spi1, tx[0]);
        spiDelay();

        // Send remaining, verify echo of previous
        for (std::uint8_t i = 1; i < kBurstLen; ++i)
        {
            std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, tx[i]);
            spiDelay();

            if (rx != tx[i - 1])
            {
                print("  [byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i - 1]);
                print(", got 0x");
                printHex(rx);
                print("]\r\n");
                return false;
            }
        }

        // Get echo of last byte
        std::uint8_t rxLast = hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        if (rxLast != tx[kBurstLen - 1])
        {
            print("  [last: expected 0x");
            printHex(tx[kBurstLen - 1]);
            print(", got 0x");
            printHex(rxLast);
            print("]\r\n");
            return false;
        }
        return true;
    }

    // --- Test 5: Stress (64 bytes) ---
    bool testStress()
    {
        constexpr std::uint8_t kStressLen = 64;
        std::uint8_t tx[kStressLen];

        for (std::uint8_t i = 0; i < kStressLen; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i ^ 0x55);
        }

        // Prime: send first byte
        hal::spiTransferByte(hal::SpiId::Spi1, tx[0]);
        spiDelay();

        // Send remaining, verify echo of previous
        for (std::uint8_t i = 1; i < kStressLen; ++i)
        {
            std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, tx[i]);
            spiDelay();

            if (rx != tx[i - 1])
            {
                print("  [byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i - 1]);
                print(", got 0x");
                printHex(rx);
                print("]\r\n");
                return false;
            }
        }

        // Get echo of last byte
        std::uint8_t rxLast = hal::spiTransferByte(hal::SpiId::Spi1, 0x00);
        spiDelay();

        if (rxLast != tx[kStressLen - 1])
        {
            print("  [last: expected 0x");
            printHex(tx[kStressLen - 1]);
            print(", got 0x");
            printHex(rxLast);
            print("]\r\n");
            return false;
        }
        return true;
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== SPI1 Board-to-Board Test (Phase 14) ===\r\n");
    print("Board 1: SPI1 master test runner, LED on PC13\r\n");
    print("Pins: PA5(SCK) PA6(MISO) PA7(MOSI)\r\n\r\n");

    initSpi1Master();

    // Wait for slave echo server on Board 2 to be ready
    print("Waiting for slave...\r\n");
    delayMs(500);

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 5;

    bool r1 = testSingleByte();
    printResult("Single byte echo", r1);
    if (r1) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    bool r2 = testMultiByte();
    printResult("Multi-byte echo (4 bytes)", r2);
    if (r2) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    bool r3 = testSequential();
    printResult("Sequential echo (0x00-0xFF)", r3);
    if (r3) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    bool r4 = testBurst();
    printResult("Burst echo (16 bytes)", r4);
    if (r4) ++pass;
    hal::gpioToggle(hal::Port::C, 13);

    bool r5 = testStress();
    printResult("Stress echo (64 bytes)", r5);
    if (r5) ++pass;
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
