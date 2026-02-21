// Multi-threaded demo: LED blink + UART print + memory test running concurrently.
//
// Demonstrates ms-os kernel context switching on STM32F207ZGT6/STM32F407ZGT6.
// LED thread toggles PC13 every ~500ms.
// UART thread prints "tick N" every ~1s.
// Memory thread exercises BlockPool and Heap alloc/free every ~5s.

#include "kernel/Kernel.h"
#include "kernel/BlockPool.h"
#include "kernel/Heap.h"
#include "kernel/CortexM.h"
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
                hal::uartWriteString(hal::UartId::Usart1, "mem ok pool=");
                uintToStr(ps.freeBlocks, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, "/");
                uintToStr(ps.totalBlocks, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, " heap=");
                uintToStr(hs.usedSize, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, "/");
                uintToStr(hs.totalSize, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, "\r\n");
            }
            else
            {
                hal::uartWriteString(hal::UartId::Usart1, "mem FAIL\r\n");
            }
        }
    }

    void ledThread(void *)
    {
        std::uint32_t lastToggle = 0;
        while (true)
        {
            std::uint32_t now = kernel::tickCount();
            if (now - lastToggle >= 500)
            {
                hal::gpioToggle(hal::Port::C, 13);
                lastToggle = now;
            }
            kernel::yield();
        }
    }

    void uartThread(void *)
    {
        std::uint32_t counter = 0;
        std::uint32_t lastPrint = 0;
        while (true)
        {
            std::uint32_t now = kernel::tickCount();
            if (now - lastPrint >= 1000)
            {
                char buf[16];
                hal::uartWriteString(hal::UartId::Usart1, "tick ");
                uintToStr(counter, buf, sizeof(buf));
                hal::uartWriteString(hal::UartId::Usart1, buf);
                hal::uartWriteString(hal::UartId::Usart1, "\r\n");
                ++counter;
                lastPrint = now;
            }
            kernel::yield();
        }
    }
}  // namespace

int main()
{
    // Enable peripheral clocks
    hal::rccEnableGpioClock(hal::Port::C);
    hal::rccEnableGpioClock(hal::Port::A);
    hal::rccEnableUartClock(hal::UartId::Usart1);

    // Configure LED pin: PC13, push-pull output
    hal::GpioConfig ledConfig{};
    ledConfig.port = hal::Port::C;
    ledConfig.pin = 13;
    ledConfig.mode = hal::PinMode::Output;
    ledConfig.speed = hal::OutputSpeed::Low;
    ledConfig.outputType = hal::OutputType::PushPull;
    hal::gpioInit(ledConfig);

    // Configure USART1 TX pin: PA9, alternate function 7
    hal::GpioConfig txConfig{};
    txConfig.port = hal::Port::A;
    txConfig.pin = 9;
    txConfig.mode = hal::PinMode::AlternateFunction;
    txConfig.speed = hal::OutputSpeed::VeryHigh;
    txConfig.alternateFunction = 7;
    hal::gpioInit(txConfig);

    // Initialize UART: 115200 8N1
    hal::UartConfig uartConfig{};
    uartConfig.id = hal::UartId::Usart1;
    uartConfig.baudRate = 115200;
    hal::uartInit(uartConfig);

    hal::uartWriteString(hal::UartId::Usart1, "ms-os kernel starting\r\n");

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
