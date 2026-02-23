// IPC demo: echo server + client threads communicating via kernel message passing.
//
// Demonstrates ms-os Phase 6 unprivileged threads + SVC syscall interface.
// Server thread (privileged) handles Ping/Add/GetCount requests and prints results.
// Client thread (unprivileged) uses kernel::user:: SVC wrappers for all kernel calls.

#include "kernel/Kernel.h"
#include "kernel/Ipc.h"
#include "kernel/Syscall.h"
#include "kernel/Shell.h"
#include "kernel/Arch.h"
#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>
#include <cstring>

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

// FNV-1a hash of "Echo" (must match generated code)
static constexpr std::uint32_t kEchoServiceId = 0x3b7d6ba4u;

namespace
{
    // Thread stacks
    alignas(1024) std::uint32_t g_serverStack[256];   // 1024 bytes
    alignas(1024) std::uint32_t g_clientStack[256];   // 1024 bytes
    alignas(1024) std::uint32_t g_ledStack[256];      // 1024 bytes
    alignas(1024) std::uint32_t g_shellStack[256];    // 1024 bytes

    // Server thread ID (set in main, read by client)
    volatile kernel::ThreadId g_serverTid = kernel::kInvalidThreadId;

    // Integer-to-string (no sprintf in freestanding)
    void uintToStr(std::uint32_t val, char *buf, std::size_t bufSize)
    {
        if (bufSize == 0)
        {
            return;
        }
        if (val == 0)
        {
            if (bufSize >= 2)
            {
                buf[0] = '0';
                buf[1] = '\0';
            }
            return;
        }
        char tmp[11];
        std::size_t len = 0;
        while (val > 0 && len < sizeof(tmp))
        {
            tmp[len++] = '0' + static_cast<char>(val % 10);
            val /= 10;
        }
        std::size_t i = 0;
        while (len > 0 && i < bufSize - 1)
        {
            buf[i++] = tmp[--len];
        }
        buf[i] = '\0';
    }

    void uartPrint(const char *s)
    {
        kernel::arch::enterCritical();
        hal::uartWriteString(board::consoleUartId(), s);
        kernel::arch::exitCritical();
    }

    void uartPrintLine(const char *s)
    {
        kernel::arch::enterCritical();
        hal::uartWriteString(board::consoleUartId(), s);
        hal::uartWriteString(board::consoleUartId(), "\r\n");
        kernel::arch::exitCritical();
    }

    // ---- Echo server (inline, no generated code dependency for simplicity) ----

    // Method IDs matching the IDL
    static constexpr std::uint16_t kMethodPing     = 1;
    static constexpr std::uint16_t kMethodAdd      = 2;
    static constexpr std::uint16_t kMethodGetCount = 3;

    void echoServerThread(void *)
    {
        std::uint32_t requestCount = 0;
        char buf[16];

        uartPrintLine("echo: server started (privileged)");

        while (true)
        {
            kernel::Message request;
            std::int32_t rc = kernel::messageReceive(&request);
            if (rc != kernel::kIpcOk)
            {
                continue;
            }

            ++requestCount;

            kernel::Message reply;
            std::memset(&reply, 0, sizeof(reply));
            reply.type = static_cast<std::uint8_t>(kernel::MessageType::Reply);
            reply.serviceId = kEchoServiceId;
            reply.methodId = request.methodId;

            switch (request.methodId)
            {
            case kMethodPing:
            {
                std::uint32_t value = 0;
                std::memcpy(&value, request.payload, sizeof(value));
                std::memcpy(reply.payload, &value, sizeof(value));
                reply.payloadSize = sizeof(value);
                reply.status = kernel::kIpcOk;

                uartPrint("srv: ping(");
                uintToStr(value, buf, sizeof(buf));
                uartPrint(buf);
                uartPrintLine(")");
                break;
            }
            case kMethodAdd:
            {
                std::uint32_t a = 0;
                std::uint32_t b = 0;
                std::memcpy(&a, request.payload, sizeof(a));
                std::memcpy(&b, request.payload + sizeof(a), sizeof(b));
                std::uint32_t sum = a + b;
                std::memcpy(reply.payload, &sum, sizeof(sum));
                reply.payloadSize = sizeof(sum);
                reply.status = kernel::kIpcOk;

                uartPrint("srv: add(");
                uintToStr(a, buf, sizeof(buf));
                uartPrint(buf);
                uartPrint(",");
                uintToStr(b, buf, sizeof(buf));
                uartPrint(buf);
                uartPrint(")=");
                uintToStr(sum, buf, sizeof(buf));
                uartPrintLine(buf);
                break;
            }
            case kMethodGetCount:
            {
                std::memcpy(reply.payload, &requestCount, sizeof(requestCount));
                reply.payloadSize = sizeof(requestCount);
                reply.status = kernel::kIpcOk;

                uartPrint("srv: count=");
                uintToStr(requestCount, buf, sizeof(buf));
                uartPrintLine(buf);
                break;
            }
            default:
                reply.status = kernel::kIpcErrMethod;
                break;
            }

            kernel::messageReply(request.sender, reply);
        }
    }

    // ---- Echo client (unprivileged -- uses SVC wrappers) ----
    // Receives server TID via arg (cannot access globals from unprivileged mode).

    void echoClientThread(void *arg)
    {
        kernel::ThreadId server = static_cast<kernel::ThreadId>(
            reinterpret_cast<std::uintptr_t>(arg));

        // Wait for server to be ready (SVC)
        kernel::user::sleep(100);

        std::uint32_t iteration = 0;

        while (true)
        {
            // -- Ping --
            {
                kernel::Message request;
                std::memset(&request, 0, sizeof(request));
                request.type = static_cast<std::uint8_t>(kernel::MessageType::Request);
                request.serviceId = kEchoServiceId;
                request.methodId = kMethodPing;
                std::uint32_t value = iteration * 10;
                std::memcpy(request.payload, &value, sizeof(value));
                request.payloadSize = sizeof(value);

                kernel::Message reply;
                kernel::user::messageSend(server, request, &reply);
            }

            // -- Add --
            {
                kernel::Message request;
                std::memset(&request, 0, sizeof(request));
                request.type = static_cast<std::uint8_t>(kernel::MessageType::Request);
                request.serviceId = kEchoServiceId;
                request.methodId = kMethodAdd;
                std::uint32_t a = iteration + 1;
                std::uint32_t b = iteration + 2;
                std::memcpy(request.payload, &a, sizeof(a));
                std::memcpy(request.payload + sizeof(a), &b, sizeof(b));
                request.payloadSize = sizeof(a) + sizeof(b);

                kernel::Message reply;
                kernel::user::messageSend(server, request, &reply);
            }

            // -- GetCount --
            {
                kernel::Message request;
                std::memset(&request, 0, sizeof(request));
                request.type = static_cast<std::uint8_t>(kernel::MessageType::Request);
                request.serviceId = kEchoServiceId;
                request.methodId = kMethodGetCount;
                request.payloadSize = 0;

                kernel::Message reply;
                kernel::user::messageSend(server, request, &reply);
            }

            ++iteration;
            kernel::user::sleep(2000);
        }
    }

    // ---- LED heartbeat ----

    void ledThread(void *)
    {
        const board::BoardConfig &cfg = board::config();
        while (true)
        {
            if (cfg.hasLed)
            {
                hal::gpioToggle(hal::Port(cfg.led.port - 'A'), cfg.led.pin);
            }
            kernel::sleep(500);
        }
    }

    // ---- Shell ----

    void shellWrite(const char *str)
    {
        hal::uartWriteString(board::consoleUartId(), str);
    }

    void shellThread(void *)
    {
        kernel::ShellConfig shellConfig{};
        shellConfig.writeFn = shellWrite;
        kernel::shellInit(shellConfig);

        uartPrintLine("shell: ready");
        kernel::shellPrompt();

        while (true)
        {
            char c;
            if (hal::uartTryGetChar(board::consoleUartId(), &c))
            {
                kernel::shellProcessChar(c);
            }
            else
            {
                kernel::sleep(10);
            }
        }
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);

    const board::BoardConfig &cfg = board::config();

    // Enable peripheral clocks and configure pins from board config
    if (cfg.hasLed)
    {
        hal::rccEnableGpioClock(hal::Port(cfg.led.port - 'A'));

        hal::GpioConfig ledConfig{};
        ledConfig.port = hal::Port(cfg.led.port - 'A');
        ledConfig.pin = cfg.led.pin;
        ledConfig.mode = hal::PinMode::Output;
        ledConfig.speed = hal::OutputSpeed::Low;
        ledConfig.outputType = hal::OutputType::PushPull;
        hal::gpioInit(ledConfig);
    }

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

    if (cfg.hasConsoleRx)
    {
        hal::rccEnableGpioClock(hal::Port(cfg.consoleRx.port - 'A'));

        hal::GpioConfig rxConfig{};
        rxConfig.port = hal::Port(cfg.consoleRx.port - 'A');
        rxConfig.pin = cfg.consoleRx.pin;
        rxConfig.mode = hal::PinMode::AlternateFunction;
        rxConfig.alternateFunction = cfg.consoleRx.af;
        hal::gpioInit(rxConfig);
    }

    hal::UartId uartId = board::consoleUartId();
    hal::rccEnableUartClock(uartId);

    hal::UartConfig uartConfig{};
    uartConfig.id = uartId;
    uartConfig.baudRate = cfg.consoleBaud;
    hal::uartInit(uartConfig);

    hal::uartWriteString(uartId, "ms-os ipc-demo starting\r\n");

    // Initialize kernel
    kernel::init();

    // Create server thread (higher priority so it services requests quickly)
    g_serverTid = kernel::createThread(echoServerThread, nullptr, "echo-srv",
                                       g_serverStack, sizeof(g_serverStack), 8);

    // Create client thread (unprivileged -- uses SVC for kernel calls).
    // Pass server TID as arg since unprivileged threads cannot access globals.
    kernel::createThread(echoClientThread,
                         reinterpret_cast<void *>(static_cast<std::uintptr_t>(g_serverTid)),
                         "echo-cli",
                         g_clientStack, sizeof(g_clientStack), 10, 0, false);

    // Create LED heartbeat thread
    kernel::createThread(ledThread, nullptr, "led",
                         g_ledStack, sizeof(g_ledStack), 15);

    // Create shell thread (low priority, just above idle)
    kernel::createThread(shellThread, nullptr, "shell",
                         g_shellStack, sizeof(g_shellStack), 20);

    // Start scheduler -- does not return
    kernel::startScheduler();

    while (true)
    {
    }
}
