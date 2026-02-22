// Multi-threaded demo: LED blink + UART print + memory test running concurrently.
//
// Demonstrates ms-os kernel context switching on STM32F207ZGT6/STM32F407ZGT6.
// LED thread toggles PC13 every ~500ms.
// UART thread prints "tick N" every ~1s.
// Memory thread exercises BlockPool and Heap alloc/free every ~5s.

#include "kernel/Kernel.h"
#include "kernel/BlockPool.h"
#include "kernel/Heap.h"
#include "kernel/Arch.h"
#include "BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>
#include <cstring>

namespace
{
    // Thread stacks (statically allocated, aligned to stack size for MPU)
    alignas(1024) std::uint32_t g_ledStack[256];    // 1024 bytes
    alignas(1024) std::uint32_t g_uartStack[256];   // 1024 bytes
    alignas(1024) std::uint32_t g_memStack[256];    // 1024 bytes

    // BlockPool backing buffer (8 blocks x 32 bytes = 256 bytes)
    alignas(8) std::uint8_t g_poolBuffer[256];

    // Simple integer-to-string for tick count (no sprintf in freestanding)
    void uintToStr(std::uint32_t val, char *buf, std::size_t bufSize)
    {
        if (bufSize == 0)
        {
            return;
        }

        // Handle zero
        if (val == 0)
        {
            if (bufSize >= 2)
            {
                buf[0] = '0';
                buf[1] = '\0';
            }
            return;
        }

        // Build digits in reverse
        char tmp[11];  // max 10 digits for uint32_t
        std::size_t len = 0;
        while (val > 0 && len < sizeof(tmp))
        {
            tmp[len++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }

        // Copy reversed
        std::size_t i = 0;
        while (len > 0 && i < bufSize - 1)
        {
            buf[i++] = tmp[--len];
        }
        buf[i] = '\0';
    }

    void memThread(void *)
    {
        kernel::BlockPool pool;
        pool.init(g_poolBuffer, 32, 8);

        constexpr std::uint8_t kPoolPattern = 0xAA;
        constexpr std::uint8_t kHeapPattern = 0xBB;
        constexpr std::uint32_t kHeapAllocSize = 64;

        while (true)
        {
            kernel::sleep(5000);

            bool ok = true;

            // BlockPool: allocate, write pattern, verify, free
            kernel::arch::enterCritical();
            void *blk = pool.allocate();
            kernel::arch::exitCritical();

            if (blk != nullptr)
            {
                std::memset(blk, kPoolPattern, 32);
                auto *bytes = static_cast<std::uint8_t *>(blk);
                for (std::uint32_t i = 0; i < 32; ++i)
                {
                    if (bytes[i] != kPoolPattern)
                    {
                        ok = false;
                        break;
                    }
                }
                kernel::arch::enterCritical();
                pool.free(blk);
                kernel::arch::exitCritical();
            }
            else
            {
                ok = false;
            }

            // Heap: allocate, write pattern, verify, free
            kernel::arch::enterCritical();
            void *hblk = kernel::heapAlloc(kHeapAllocSize);
            kernel::arch::exitCritical();

            if (hblk != nullptr)
            {
                std::memset(hblk, kHeapPattern, kHeapAllocSize);
                auto *hbytes = static_cast<std::uint8_t *>(hblk);
                for (std::uint32_t i = 0; i < kHeapAllocSize; ++i)
                {
                    if (hbytes[i] != kHeapPattern)
                    {
                        ok = false;
                        break;
                    }
                }
                kernel::arch::enterCritical();
                kernel::heapFree(hblk);
                kernel::arch::exitCritical();
            }
            else
            {
                ok = false;
            }

            // Print stats
            if (ok)
            {
                kernel::arch::enterCritical();
                kernel::BlockPoolStats ps = pool.stats();
                kernel::HeapStats hs = kernel::heapGetStats();
                kernel::arch::exitCritical();

                char buf[16];
                kernel::arch::enterCritical();
                hal::uartWriteString(board::kConsoleUart, "mem ok pool=");
                uintToStr(ps.freeBlocks, buf, sizeof(buf));
                hal::uartWriteString(board::kConsoleUart, buf);
                hal::uartWriteString(board::kConsoleUart, "/");
                uintToStr(ps.totalBlocks, buf, sizeof(buf));
                hal::uartWriteString(board::kConsoleUart, buf);
                hal::uartWriteString(board::kConsoleUart, " heap=");
                uintToStr(hs.usedSize, buf, sizeof(buf));
                hal::uartWriteString(board::kConsoleUart, buf);
                hal::uartWriteString(board::kConsoleUart, "/");
                uintToStr(hs.totalSize, buf, sizeof(buf));
                hal::uartWriteString(board::kConsoleUart, buf);
                hal::uartWriteString(board::kConsoleUart, "\r\n");
                kernel::arch::exitCritical();
            }
            else
            {
                kernel::arch::enterCritical();
                hal::uartWriteString(board::kConsoleUart, "mem FAIL\r\n");
                kernel::arch::exitCritical();
            }
        }
    }

    void ledThread(void *)
    {
        while (true)
        {
            if constexpr (board::kHasLed)
            {
                hal::gpioToggle(board::kLedPort, board::kLedPin);
            }
            kernel::sleep(500);
        }
    }

    void uartThread(void *)
    {
        std::uint32_t counter = 0;
        while (true)
        {
            char buf[16];
            uintToStr(counter, buf, sizeof(buf));

            kernel::arch::enterCritical();
            hal::uartWriteString(board::kConsoleUart, "tick ");
            hal::uartWriteString(board::kConsoleUart, buf);
            hal::uartWriteString(board::kConsoleUart, "\r\n");
            kernel::arch::exitCritical();

            ++counter;
            kernel::sleep(1000);
        }
    }
}  // namespace

int main()
{
    // Enable peripheral clocks and configure pins from board config
    if constexpr (board::kHasLed)
    {
        hal::rccEnableGpioClock(board::kLedPort);

        hal::GpioConfig ledConfig{};
        ledConfig.port = board::kLedPort;
        ledConfig.pin = board::kLedPin;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        ledConfig.outputType = hal::OutputType::PushPull;
        hal::gpioInit(ledConfig);
    }

    if constexpr (board::kHasConsoleTx)
    {
        hal::rccEnableGpioClock(board::kConsoleTxPort);

        hal::GpioConfig txConfig{};
        txConfig.port = board::kConsoleTxPort;
        txConfig.pin = board::kConsoleTxPin;
        txConfig.mode = hal::PinMode::AlternateFunction;
        txConfig.speed = hal::OutputSpeed::VeryHigh;
        txConfig.alternateFunction = board::kConsoleTxAf;
        hal::gpioInit(txConfig);
    }

    hal::rccEnableUartClock(board::kConsoleUart);

    hal::UartConfig uartConfig{};
    uartConfig.id = board::kConsoleUart;
    uartConfig.baudRate = board::kConsoleBaud;
    hal::uartInit(uartConfig);

    hal::uartWriteString(board::kConsoleUart, "ms-os kernel starting\r\n");

    // Initialize kernel
    kernel::init();

    // Create application threads with explicit priorities (lower number = higher priority)
    kernel::createThread(ledThread, nullptr, "led",
                         g_ledStack, sizeof(g_ledStack), 10);

    kernel::createThread(uartThread, nullptr, "uart",
                         g_uartStack, sizeof(g_uartStack), 10);

    kernel::createThread(memThread, nullptr, "mem",
                         g_memStack, sizeof(g_memStack), 10);

    // Start scheduler -- does not return
    kernel::startScheduler();

    // Should never reach here
    while (true)
    {
    }
}
