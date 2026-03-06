// DMA hardware coverage test -- Phase 18
//
// Runs on STM32F407 as a standalone DMA2 memory-to-memory validation matrix.
// Prints both human-readable and machine-parseable result lines.

#include "kernel/BoardConfig.h"
#include "hal/Dma.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstddef>
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
    constexpr std::uint32_t kTimeoutLoops = 6000000;

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

    void printHex(std::uint8_t value)
    {
        const char kHex[] = "0123456789ABCDEF";
        hal::uartPutChar(g_consoleUart, kHex[value >> 4]);
        hal::uartPutChar(g_consoleUart, kHex[value & 0x0F]);
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
    }

    void configureDma(hal::DmaDataSize size, bool sourceIncrement, bool destIncrement)
    {
        hal::DmaConfig cfg{};
        cfg.controller = kController;
        cfg.stream = kStream;
        cfg.channel = kChannel;
        cfg.direction = hal::DmaDirection::MemoryToMemory;
        cfg.peripheralSize = size;
        cfg.memorySize = size;
        cfg.peripheralIncrement = sourceIncrement;
        cfg.memoryIncrement = destIncrement;
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

    bool runTransfer(std::uint32_t sourceAddr, std::uint32_t destAddr, std::uint16_t count)
    {
        hal::dmaStart(kController, kStream, sourceAddr, destAddr, count, nullptr, nullptr);
        if (!waitForDmaIdle(kTimeoutLoops))
        {
            print("  [timeout waiting for DMA idle]\r\n");
            hal::dmaStop(kController, kStream);
            return false;
        }
        return true;
    }

    bool testConfigAndIdle()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);
        return (!hal::dmaIsBusy(kController, kStream) &&
                hal::dmaRemaining(kController, kStream) == 0);
    }

    void fillPattern(std::uint8_t *buffer, std::size_t length, std::uint8_t seed)
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            buffer[i] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(i) * 13U) ^ seed);
        }
    }

    bool verifyEqual(const std::uint8_t *source, const std::uint8_t *dest, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            if (source[i] != dest[i])
            {
                print("  [mismatch at ");
                printDecimal(static_cast<std::uint32_t>(i));
                print(": expected 0x");
                printHex(source[i]);
                print(" got 0x");
                printHex(dest[i]);
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    bool testMemcpyByte1()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);

        std::uint8_t source[1] = {0x5A};
        std::uint8_t dest[1] = {0};
        if (!runTransfer(reinterpret_cast<std::uint32_t>(source),
                         reinterpret_cast<std::uint32_t>(dest), 1))
        {
            return false;
        }
        return verifyEqual(source, dest, 1);
    }

    bool testMemcpyByte64()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);

        std::uint8_t source[64];
        std::uint8_t dest[64];
        fillPattern(source, 64, 0xA5);
        std::memset(dest, 0, sizeof(dest));
        if (!runTransfer(reinterpret_cast<std::uint32_t>(source),
                         reinterpret_cast<std::uint32_t>(dest), 64))
        {
            return false;
        }
        return verifyEqual(source, dest, 64);
    }

    bool testMemcpyHalfword32()
    {
        configureDma(hal::DmaDataSize::HalfWord, true, true);

        alignas(4) std::uint16_t source[32];
        alignas(4) std::uint16_t dest[32];
        for (std::size_t i = 0; i < 32; ++i)
        {
            source[i] = static_cast<std::uint16_t>(0x1100U + i * 7U);
            dest[i] = 0;
        }
        if (!runTransfer(reinterpret_cast<std::uint32_t>(source),
                         reinterpret_cast<std::uint32_t>(dest), 32))
        {
            return false;
        }
        return std::memcmp(source, dest, sizeof(source)) == 0;
    }

    bool testMemcpyWord16()
    {
        configureDma(hal::DmaDataSize::Word, true, true);

        alignas(4) std::uint32_t source[16];
        alignas(4) std::uint32_t dest[16];
        for (std::size_t i = 0; i < 16; ++i)
        {
            source[i] = 0xA5A50000U | static_cast<std::uint32_t>(i * 257U);
            dest[i] = 0;
        }
        if (!runTransfer(reinterpret_cast<std::uint32_t>(source),
                         reinterpret_cast<std::uint32_t>(dest), 16))
        {
            return false;
        }
        return std::memcmp(source, dest, sizeof(source)) == 0;
    }

    bool testFixedSourceFill()
    {
        configureDma(hal::DmaDataSize::Byte, false, true);

        const std::uint8_t source = 0x3C;
        std::uint8_t dest[48];
        std::memset(dest, 0, sizeof(dest));
        if (!runTransfer(reinterpret_cast<std::uint32_t>(&source),
                         reinterpret_cast<std::uint32_t>(dest), 48))
        {
            return false;
        }
        for (std::size_t i = 0; i < 48; ++i)
        {
            if (dest[i] != source)
            {
                print("  [fill mismatch at ");
                printDecimal(static_cast<std::uint32_t>(i));
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    bool testMemcpyByteUnaligned()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);

        alignas(4) std::uint8_t source[40];
        alignas(4) std::uint8_t dest[48];
        fillPattern(source, sizeof(source), 0xD2);
        std::memset(dest, 0, sizeof(dest));

        const std::uint32_t sourceAddr = reinterpret_cast<std::uint32_t>(source + 1);
        const std::uint32_t destAddr = reinterpret_cast<std::uint32_t>(dest + 3);
        constexpr std::uint16_t kCount = 31;
        if (!runTransfer(sourceAddr, destAddr, kCount))
        {
            return false;
        }
        return verifyEqual(source + 1, dest + 3, kCount);
    }

    bool testRepeatability()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);

        std::uint8_t source[32];
        std::uint8_t dest[32];
        for (std::uint32_t iter = 0; iter < 20; ++iter)
        {
            fillPattern(source, sizeof(source), static_cast<std::uint8_t>(0x40U + iter));
            std::memset(dest, 0, sizeof(dest));
            if (!runTransfer(reinterpret_cast<std::uint32_t>(source),
                             reinterpret_cast<std::uint32_t>(dest), 32))
            {
                return false;
            }
            if (!verifyEqual(source, dest, 32))
            {
                print("  [repeatability iter ");
                printDecimal(iter);
                print("]\r\n");
                return false;
            }
        }
        return true;
    }

    bool testStopAfterStart()
    {
        configureDma(hal::DmaDataSize::Byte, true, true);

        std::uint8_t source[1024];
        std::uint8_t dest[1024];
        fillPattern(source, sizeof(source), 0x9E);
        std::memset(dest, 0, sizeof(dest));

        hal::dmaStart(
            kController,
            kStream,
            reinterpret_cast<std::uint32_t>(source),
            reinterpret_cast<std::uint32_t>(dest),
            1024,
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

    print("\r\n=== DMA Hardware Coverage Test (Phase 18) ===\r\n");
    print("DMA2 stream0 memory-to-memory validation matrix\r\n\r\n");

    initDma();

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 9;

    bool r1 = testConfigAndIdle();
    printResult("Test 1: Config and idle state", r1);
    printMachineCase("config-idle", r1);
    if (r1)
    {
        ++pass;
    }

    bool r2 = testMemcpyByte1();
    printResult("Test 2: Byte copy (1 byte)", r2);
    printMachineCase("memcpy-byte-1", r2);
    if (r2)
    {
        ++pass;
    }

    bool r3 = testMemcpyByte64();
    printResult("Test 3: Byte copy (64 bytes)", r3);
    printMachineCase("memcpy-byte-64", r3);
    if (r3)
    {
        ++pass;
    }

    bool r4 = testMemcpyHalfword32();
    printResult("Test 4: Halfword copy (32 transfers)", r4);
    printMachineCase("memcpy-halfword-32", r4);
    if (r4)
    {
        ++pass;
    }

    bool r5 = testMemcpyWord16();
    printResult("Test 5: Word copy (16 transfers)", r5);
    printMachineCase("memcpy-word-16", r5);
    if (r5)
    {
        ++pass;
    }

    bool r6 = testFixedSourceFill();
    printResult("Test 6: Fixed source fill (48 bytes)", r6);
    printMachineCase("fixed-source-fill-48", r6);
    if (r6)
    {
        ++pass;
    }

    bool r7 = testMemcpyByteUnaligned();
    printResult("Test 7: Byte copy unaligned (31 bytes)", r7);
    printMachineCase("memcpy-byte-unaligned-31", r7);
    if (r7)
    {
        ++pass;
    }

    bool r8 = testRepeatability();
    printResult("Test 8: Repeatability (20x32 byte copies)", r8);
    printMachineCase("repeatability-20x32", r8);
    if (r8)
    {
        ++pass;
    }

    bool r9 = testStopAfterStart();
    printResult("Test 9: Stop after start", r9);
    printMachineCase("stop-after-start", r9);
    if (r9)
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
