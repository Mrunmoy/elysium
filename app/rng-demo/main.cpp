// RNG hardware test -- reads 10 random numbers and verifies basic properties.
//
// Expected: all reads succeed, values are non-zero, not all identical.

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Rng.h"
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

    void printHex32(std::uint32_t value)
    {
        const char hex[] = "0123456789ABCDEF";
        char buf[11];
        buf[0] = '0';
        buf[1] = 'x';
        for (int i = 7; i >= 0; --i)
        {
            buf[2 + (7 - i)] = hex[(value >> (i * 4)) & 0xF];
        }
        buf[10] = '\0';
        print(buf);
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

    void printMachineCase(const char *caseName, bool pass)
    {
        print("MSOS_CASE:rng:");
        print(caseName);
        print(pass ? ":PASS\r\n" : ":FAIL\r\n");
    }

    void printMachineSummary(std::uint32_t passCount, std::uint32_t totalCount)
    {
        print("MSOS_SUMMARY:rng:pass=");
        printDecimal(passCount);
        print(":total=");
        printDecimal(totalCount);
        print(":result=");
        print(passCount == totalCount ? "PASS\r\n" : "FAIL\r\n");
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

    constexpr std::uint32_t kNumReads = 10;

    bool testInitAndRead(std::uint32_t *values)
    {
        hal::rccEnableRngClock();

        std::int32_t status = hal::rngInit();
        if (status != 0)
        {
            print("  rngInit() failed with code ");
            printDecimal(static_cast<std::uint32_t>(-status));
            print("\r\n");
            return false;
        }

        for (std::uint32_t i = 0; i < kNumReads; ++i)
        {
            status = hal::rngRead(values[i]);
            if (status != 0)
            {
                print("  rngRead() failed at index ");
                printDecimal(i);
                print(" with code ");
                printDecimal(static_cast<std::uint32_t>(-status));
                print("\r\n");
                return false;
            }
        }
        return true;
    }

    bool testNonZero(const std::uint32_t *values)
    {
        for (std::uint32_t i = 0; i < kNumReads; ++i)
        {
            if (values[i] == 0)
            {
                print("  value[");
                printDecimal(i);
                print("] is zero\r\n");
                return false;
            }
        }
        return true;
    }

    bool testNotAllIdentical(const std::uint32_t *values)
    {
        for (std::uint32_t i = 1; i < kNumReads; ++i)
        {
            if (values[i] != values[0])
            {
                return true;
            }
        }
        print("  all values identical: ");
        printHex32(values[0]);
        print("\r\n");
        return false;
    }

    bool testDeinit()
    {
        hal::rngDeinit();
        hal::rccDisableRngClock();
        return true;
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();

    print("\r\n=== RNG Hardware Test ===\r\n");
    print("Reading ");
    printDecimal(kNumReads);
    print(" random numbers from TRNG\r\n\r\n");

    std::uint32_t values[kNumReads] = {};
    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 4;

    // Test 1: Init and read
    bool r1 = testInitAndRead(values);
    printResult("Test 1: Init and read 10 values", r1);
    printMachineCase("init-read", r1);
    if (r1)
    {
        ++pass;
    }

    // Print all values
    if (r1)
    {
        print("  Values:\r\n");
        for (std::uint32_t i = 0; i < kNumReads; ++i)
        {
            print("    [");
            printDecimal(i);
            print("] ");
            printHex32(values[i]);
            print("\r\n");
        }
        print("\r\n");
    }

    // Test 2: All non-zero
    bool r2 = r1 && testNonZero(values);
    printResult("Test 2: All values non-zero", r2);
    printMachineCase("non-zero", r2);
    if (r2)
    {
        ++pass;
    }

    // Test 3: Not all identical
    bool r3 = r1 && testNotAllIdentical(values);
    printResult("Test 3: Not all identical", r3);
    printMachineCase("not-identical", r3);
    if (r3)
    {
        ++pass;
    }

    // Test 4: Deinit
    bool r4 = testDeinit();
    printResult("Test 4: Deinit", r4);
    printMachineCase("deinit", r4);
    if (r4)
    {
        ++pass;
    }

    print("\r\n--- Summary: ");
    printDecimal(pass);
    print("/");
    printDecimal(kTotal);
    print(" passed");
    print(pass == kTotal ? " (ALL PASS)" : " (SOME FAILED)");
    print(" ---\r\n");
    printMachineSummary(pass, kTotal);

    while (true)
    {
        __asm volatile("wfi");
    }
}
