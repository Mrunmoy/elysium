// DMA hardware smoke test -- Phase 17
//
// Runs on STM32F407 as a standalone DMA2 memory-to-memory validation.
// Prints both human-readable and machine-parseable result lines.

#include "kernel/BoardConfig.h"
#include "hal/Dma.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>
#include <cstring>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    hal::UartId g_consoleUart;

    constexpr hal::DmaController kController = hal::DmaController::Dma2;
    constexpr hal::DmaStream kStream = hal::DmaStream::Stream0;
    constexpr hal::DmaChannel kChannel = hal::DmaChannel::Channel0;

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
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
        print("MSOS_CASE:dma:");
        print(caseName);
        print(pass ? ":PASS\r\n" : ":FAIL\r\n");
    }

    void printMachineSummary(std::uint32_t passCount, std::uint32_t totalCount)
    {
        print("MSOS_SUMMARY:dma:pass=");
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

    void initDma()
    {
        hal::rccEnableDmaClock(kController);

        hal::DmaConfig cfg{};
        cfg.controller = kController;
        cfg.stream = kStream;
        cfg.channel = kChannel;
        cfg.direction = hal::DmaDirection::MemoryToMemory;
        cfg.peripheralSize = hal::DmaDataSize::Byte;
        cfg.memorySize = hal::DmaDataSize::Byte;
        cfg.peripheralIncrement = true;
        cfg.memoryIncrement = true;
        cfg.priority = hal::DmaPriority::High;
        cfg.circular = false;
        hal::dmaInit(cfg);
    }

    bool waitForDmaIdle(std::uint32_t timeoutLoops)
    {
        for (std::uint32_t i = 0; i < timeoutLoops; ++i)
        {
            if (!hal::dmaIsBusy(kController, kStream))
            {
                return true;
            }
            __asm volatile("nop");
        }
        return false;
    }

    bool testConfigAndIdle()
    {
        return (!hal::dmaIsBusy(kController, kStream) && hal::dmaRemaining(kController, kStream) == 0);
    }

    bool testMemoryToMemoryCopy()
    {
        std::uint8_t source[64];
        std::uint8_t dest[64];

        for (std::uint32_t i = 0; i < 64; ++i)
        {
            source[i] = static_cast<std::uint8_t>((i * 3U) ^ 0xA5U);
            dest[i] = 0;
        }

        hal::dmaStart(
            kController,
            kStream,
            reinterpret_cast<std::uint32_t>(source),
            reinterpret_cast<std::uint32_t>(dest),
            64,
            nullptr,
            nullptr);

        if (!waitForDmaIdle(4000000))
        {
            print("  [timeout waiting for DMA idle]\r\n");
            hal::dmaStop(kController, kStream);
            return false;
        }

        return std::memcmp(source, dest, 64) == 0;
    }

    bool testStopAfterStart()
    {
        std::uint8_t source[256];
        std::uint8_t dest[256];

        for (std::uint32_t i = 0; i < 256; ++i)
        {
            source[i] = static_cast<std::uint8_t>(i + 1U);
            dest[i] = 0;
        }

        hal::dmaStart(
            kController,
            kStream,
            reinterpret_cast<std::uint32_t>(source),
            reinterpret_cast<std::uint32_t>(dest),
            256,
            nullptr,
            nullptr);

        hal::dmaStop(kController, kStream);
        return !hal::dmaIsBusy(kController, kStream);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();

    print("\r\n=== DMA Hardware Smoke Test (Phase 17) ===\r\n");
    print("DMA2 stream0 memory-to-memory validation\r\n\r\n");

    initDma();

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 3;

    bool r1 = testConfigAndIdle();
    printResult("Test 1: Config and idle state", r1);
    printMachineCase("config-idle", r1);
    if (r1)
    {
        ++pass;
    }

    bool r2 = testMemoryToMemoryCopy();
    printResult("Test 2: Memory-to-memory copy (64 bytes)", r2);
    printMachineCase("memcpy-64", r2);
    if (r2)
    {
        ++pass;
    }

    bool r3 = testStopAfterStart();
    printResult("Test 3: Stop after start", r3);
    printMachineCase("stop-after-start", r3);
    if (r3)
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
