// UART2 Board-to-Board Test Runner -- Phase 13
//
// Runs on Board 1 (J-Link). Sends test patterns on USART2 to Board 2
// (echo server), waits for echoed data using interrupt-driven RX,
// verifies correctness. Results printed on USART1 console.
// LED on PC13 toggles on each received echo byte for visual feedback.
//
// Board: STM32F407ZGT6
//   USART2: PA2 (TX, AF7), PA3 (RX, AF7) -- cross-wired to Board 2
//   LED: PC13 (active low)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    hal::UartId g_consoleUart;

    // RX interrupt sets this flag; waitForEcho uses WFI to sleep until data arrives.
    volatile bool g_rxReady = false;

    void rxNotify(void * /* arg */)
    {
        g_rxReady = true;
    }

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

    void initUsart2()
    {
        // Enable GPIOA clock (PA2, PA3)
        hal::rccEnableGpioClock(hal::Port::A);

        // PA2 = USART2 TX (AF7)
        hal::GpioConfig txConfig{};
        txConfig.port = hal::Port::A;
        txConfig.pin = 2;
        txConfig.mode = hal::PinMode::AlternateFunction;
        txConfig.speed = hal::OutputSpeed::VeryHigh;
        txConfig.alternateFunction = 7;
        hal::gpioInit(txConfig);

        // PA3 = USART2 RX (AF7)
        hal::GpioConfig rxConfig{};
        rxConfig.port = hal::Port::A;
        rxConfig.pin = 3;
        rxConfig.mode = hal::PinMode::AlternateFunction;
        rxConfig.speed = hal::OutputSpeed::VeryHigh;
        rxConfig.alternateFunction = 7;
        hal::gpioInit(rxConfig);

        // Enable USART2 clock and init at 115200
        hal::rccEnableUartClock(hal::UartId::Usart2);

        hal::UartConfig uartConfig{};
        uartConfig.id = hal::UartId::Usart2;
        uartConfig.baudRate = 115200;
        hal::uartInit(uartConfig);

        // Enable RX interrupt with notify callback
        hal::uartRxInterruptEnable(hal::UartId::Usart2, rxNotify, nullptr);
    }

    // Busy-wait delay (~1ms at 168 MHz, approximate)
    void delayMs(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            for (volatile std::uint32_t j = 0; j < 33600; ++j)
            {
                // ~168000 cycles / 5 cycles per loop iteration = ~33600
            }
        }
    }

    // Turn LED ON (active-low: clear = ON)
    void ledOn()
    {
        hal::gpioClear(hal::Port::C, 13);
    }

    // Turn LED OFF (active-low: set = OFF)
    void ledOff()
    {
        hal::gpioSet(hal::Port::C, 13);
    }

    // Wait for a single echo byte with timeout.
    // Uses WFI to sleep between polls, woken by USART2 RX interrupt.
    // Timeout is a fallback loop count in case no interrupt fires.
    bool waitForEcho(char *out, std::uint32_t timeoutLoops)
    {
        for (std::uint32_t i = 0; i < timeoutLoops; ++i)
        {
            if (hal::uartTryGetChar(hal::UartId::Usart2, out))
            {
                // Blink LED on RX. The waitForEcho call loop provides
                // the natural OFF time between blinks.
                ledOn();
                return true;
            }

            // Sleep until next interrupt (USART2 RX or any other).
            // Avoids burning CPU while waiting for echo.
            if (!g_rxReady)
            {
                __asm volatile("wfi");
            }
            g_rxReady = false;
        }
        return false;
    }

    // Drain any stale bytes from the RX buffer
    void drainRx()
    {
        char c;
        while (hal::uartTryGetChar(hal::UartId::Usart2, &c))
        {
        }
        g_rxReady = false;
        ledOff();
    }

    // --- Test 1: Single byte ---
    bool testSingleByte()
    {
        drainRx();
        hal::uartPutChar(hal::UartId::Usart2, static_cast<char>(0xA5));

        char rx;
        if (!waitForEcho(&rx, 500000))
        {
            print("  [timeout]\r\n");
            return false;
        }

        if (static_cast<std::uint8_t>(rx) != 0xA5)
        {
            print("  [expected 0xA5, got 0x");
            printHex(static_cast<std::uint8_t>(rx));
            print("]\r\n");
            return false;
        }
        return true;
    }

    // --- Test 2: Multi-byte ---
    bool testMultiByte()
    {
        drainRx();
        const std::uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};

        for (std::uint8_t i = 0; i < 4; ++i)
        {
            hal::uartPutChar(hal::UartId::Usart2, static_cast<char>(tx[i]));
        }

        for (std::uint8_t i = 0; i < 4; ++i)
        {
            char rx;
            if (!waitForEcho(&rx, 500000))
            {
                print("  [timeout at byte ");
                printDecimal(i);
                print("]\r\n");
                return false;
            }
            if (static_cast<std::uint8_t>(rx) != tx[i])
            {
                print("  [mismatch at byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i]);
                print(", got 0x");
                printHex(static_cast<std::uint8_t>(rx));
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    // --- Test 3: Sequential (0x00-0xFF) ---
    bool testSequential()
    {
        drainRx();

        for (std::uint16_t i = 0; i < 256; ++i)
        {
            std::uint8_t val = static_cast<std::uint8_t>(i);
            hal::uartPutChar(hal::UartId::Usart2, static_cast<char>(val));

            char rx;
            if (!waitForEcho(&rx, 500000))
            {
                print("  [timeout at 0x");
                printHex(val);
                print("]\r\n");
                return false;
            }
            if (static_cast<std::uint8_t>(rx) != val)
            {
                print("  [mismatch at 0x");
                printHex(val);
                print(": got 0x");
                printHex(static_cast<std::uint8_t>(rx));
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    // --- Test 4: Burst (16 bytes) ---
    bool testBurst()
    {
        drainRx();
        constexpr std::uint8_t kBurstLen = 16;
        std::uint8_t tx[kBurstLen];

        for (std::uint8_t i = 0; i < kBurstLen; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i * 17 + 0x10);  // Varied pattern
        }

        // Send all bytes rapidly
        for (std::uint8_t i = 0; i < kBurstLen; ++i)
        {
            hal::uartPutChar(hal::UartId::Usart2, static_cast<char>(tx[i]));
        }

        // Collect all echoes
        for (std::uint8_t i = 0; i < kBurstLen; ++i)
        {
            char rx;
            if (!waitForEcho(&rx, 500000))
            {
                print("  [timeout at byte ");
                printDecimal(i);
                print("]\r\n");
                return false;
            }
            if (static_cast<std::uint8_t>(rx) != tx[i])
            {
                print("  [mismatch at byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i]);
                print(", got 0x");
                printHex(static_cast<std::uint8_t>(rx));
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    // --- Test 5: Stress (64 bytes) ---
    bool testStress()
    {
        drainRx();
        constexpr std::uint8_t kStressLen = 64;
        std::uint8_t tx[kStressLen];

        for (std::uint8_t i = 0; i < kStressLen; ++i)
        {
            tx[i] = static_cast<std::uint8_t>(i ^ 0x55);  // XOR pattern
        }

        // Send all bytes rapidly
        for (std::uint8_t i = 0; i < kStressLen; ++i)
        {
            hal::uartPutChar(hal::UartId::Usart2, static_cast<char>(tx[i]));
        }

        // Collect all echoes
        for (std::uint8_t i = 0; i < kStressLen; ++i)
        {
            char rx;
            if (!waitForEcho(&rx, 500000))
            {
                print("  [timeout at byte ");
                printDecimal(i);
                print("]\r\n");
                return false;
            }
            if (static_cast<std::uint8_t>(rx) != tx[i])
            {
                print("  [mismatch at byte ");
                printDecimal(i);
                print(": expected 0x");
                printHex(tx[i]);
                print(", got 0x");
                printHex(static_cast<std::uint8_t>(rx));
                print("]\r\n");
                return false;
            }
        }
        return true;
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== UART2 Board-to-Board Test (Phase 13) ===\r\n");
    print("Board 1: Test runner on USART2, LED on PC13\r\n\r\n");

    initUsart2();

    // Wait for echo server on Board 2 to be ready
    print("Waiting for echo server...\r\n");
    delayMs(500);

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 5;

    bool r1 = testSingleByte();
    printResult("Single byte (0xA5)", r1);
    if (r1) ++pass;

    bool r2 = testMultiByte();
    printResult("Multi-byte (4 bytes)", r2);
    if (r2) ++pass;

    bool r3 = testSequential();
    printResult("Sequential (0x00-0xFF)", r3);
    if (r3) ++pass;

    bool r4 = testBurst();
    printResult("Burst (16 bytes)", r4);
    if (r4) ++pass;

    bool r5 = testStress();
    printResult("Stress (64 bytes)", r5);
    if (r5) ++pass;

    ledOff();

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
