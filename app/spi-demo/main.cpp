// SPI Loopback Demo -- Phase 10 verification for SPI driver
//
// Connects SPI1 MOSI (PA7) to MISO (PA6) externally for loopback.
// Runs polled and async transfer tests, prints results over console UART.
//
// Board: STM32F407ZGT6 (also works on F207)
//   SPI1: PA5 (SCK, AF5), PA6 (MISO, AF5), PA7 (MOSI, AF5)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "kernel/Kernel.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Spi.h"
#include "hal/Uart.h"

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

    void initSpi1()
    {
        // Enable clocks
        hal::rccEnableGpioClock(hal::Port::A);
        hal::rccEnableSpiClock(hal::SpiId::Spi1);

        // PA5 = SPI1_SCK (AF5)
        hal::GpioConfig sckConfig{};
        sckConfig.port = hal::Port::A;
        sckConfig.pin = 5;
        sckConfig.mode = hal::PinMode::AlternateFunction;
        sckConfig.speed = hal::OutputSpeed::VeryHigh;
        sckConfig.alternateFunction = 5;
        hal::gpioInit(sckConfig);

        // PA6 = SPI1_MISO (AF5)
        hal::GpioConfig misoConfig{};
        misoConfig.port = hal::Port::A;
        misoConfig.pin = 6;
        misoConfig.mode = hal::PinMode::AlternateFunction;
        misoConfig.speed = hal::OutputSpeed::VeryHigh;
        misoConfig.alternateFunction = 5;
        hal::gpioInit(misoConfig);

        // PA7 = SPI1_MOSI (AF5)
        hal::GpioConfig mosiConfig{};
        mosiConfig.port = hal::Port::A;
        mosiConfig.pin = 7;
        mosiConfig.mode = hal::PinMode::AlternateFunction;
        mosiConfig.speed = hal::OutputSpeed::VeryHigh;
        mosiConfig.alternateFunction = 5;
        hal::gpioInit(mosiConfig);

        // Configure SPI1: master, mode 0, software NSS, prescaler /16
        hal::SpiConfig spiConfig{};
        spiConfig.id = hal::SpiId::Spi1;
        spiConfig.mode = hal::SpiMode::Mode0;
        spiConfig.prescaler = hal::SpiBaudPrescaler::Div16;
        spiConfig.dataSize = hal::SpiDataSize::Bits8;
        spiConfig.bitOrder = hal::SpiBitOrder::MsbFirst;
        spiConfig.master = true;
        spiConfig.softwareNss = true;
        hal::spiInit(spiConfig);
    }

    // Test 1: Polled single-byte loopback
    bool testPolledSingleByte()
    {
        std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, 0xA5);
        return rx == 0xA5;
    }

    // Test 2: Polled multi-byte loopback
    bool testPolledMultiByte()
    {
        std::uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};
        std::uint8_t rx[4] = {};

        hal::spiTransfer(hal::SpiId::Spi1, tx, rx, 4);

        return std::memcmp(tx, rx, 4) == 0;
    }

    // Test 3: Polled byte pattern sweep (0x00..0xFF)
    bool testPatternSweep()
    {
        for (std::uint16_t i = 0; i < 256; ++i)
        {
            std::uint8_t val = static_cast<std::uint8_t>(i);
            std::uint8_t rx = hal::spiTransferByte(hal::SpiId::Spi1, val);
            if (rx != val)
            {
                return false;
            }
        }
        return true;
    }

    // Test 4: Async loopback with semaphore completion wait
    volatile bool g_asyncDone = false;

    void asyncCallback(void * /* arg */)
    {
        g_asyncDone = true;
    }

    bool testAsyncTransfer()
    {
        std::uint8_t tx[] = {0xCA, 0xFE, 0xBA, 0xBE};
        std::uint8_t rx[4] = {};
        g_asyncDone = false;

        hal::spiTransferAsync(hal::SpiId::Spi1, tx, rx, 4, asyncCallback, nullptr);

        // Busy-wait for completion (in real app, use semaphore)
        for (std::uint32_t i = 0; i < 1000000 && !g_asyncDone; ++i)
        {
            __asm volatile("nop");
        }

        if (!g_asyncDone)
        {
            return false;
        }

        return std::memcmp(tx, rx, 4) == 0;
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();

    print("\r\n=== SPI Loopback Demo (Phase 10) ===\r\n");
    print("Connect PA7 (MOSI) to PA6 (MISO) for loopback\r\n\r\n");

    initSpi1();

    std::uint32_t pass = 0;
    std::uint32_t total = 4;

    bool r1 = testPolledSingleByte();
    printResult("Polled single byte", r1);
    if (r1) ++pass;

    bool r2 = testPolledMultiByte();
    printResult("Polled multi-byte", r2);
    if (r2) ++pass;

    bool r3 = testPatternSweep();
    printResult("Pattern sweep (0x00-0xFF)", r3);
    if (r3) ++pass;

    bool r4 = testAsyncTransfer();
    printResult("Async transfer", r4);
    if (r4) ++pass;

    print("\r\n--- Summary: ");
    char passChar = '0' + static_cast<char>(pass);
    char totalChar = '0' + static_cast<char>(total);
    hal::uartPutChar(g_consoleUart, passChar);
    print("/");
    hal::uartPutChar(g_consoleUart, totalChar);
    print(" passed");
    print(pass == total ? " (ALL PASS)" : " (SOME FAILED)");
    print(" ---\r\n");

    while (true)
    {
        __asm volatile("wfi");
    }
}
