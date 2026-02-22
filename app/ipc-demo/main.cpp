// IPC demo: echo server + client threads communicating via kernel message passing.
//
// Demonstrates ms-os Phase 5 IPC on STM32F207/F407/PYNQ-Z2.
// Server thread handles Ping/Add/GetCount requests from the client thread.
// Client sends requests every ~2s and prints results on UART.

#include "kernel/Kernel.h"
#include "kernel/Ipc.h"
#include "kernel/Arch.h"
#include "BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

#include <cstdint>
#include <cstring>

// FNV-1a hash of "Echo" (must match generated code)
static constexpr std::uint32_t kEchoServiceId = 0x3b7d6ba4u;

namespace
{
    // Thread stacks
    alignas(1024) std::uint32_t g_serverStack[256];   // 1024 bytes
    alignas(1024) std::uint32_t g_clientStack[256];   // 1024 bytes
    alignas(1024) std::uint32_t g_ledStack[256];      // 1024 bytes

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

    void int32ToStr(std::int32_t val, char *buf, std::size_t bufSize)
    {
        if (val < 0 && bufSize > 1)
        {
            buf[0] = '-';
            uintToStr(static_cast<std::uint32_t>(-val), buf + 1, bufSize - 1);
        }
        else
        {
            uintToStr(static_cast<std::uint32_t>(val), buf, bufSize);
        }
    }

    void uartPrint(const char *s)
    {
        kernel::arch::enterCritical();
        hal::uartWriteString(board::kConsoleUart, s);
        kernel::arch::exitCritical();
    }

    void uartPrintLine(const char *s)
    {
        kernel::arch::enterCritical();
        hal::uartWriteString(board::kConsoleUart, s);
        hal::uartWriteString(board::kConsoleUart, "\r\n");
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

        uartPrintLine("echo: server started");

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
                // Echo the value back
                std::uint32_t value = 0;
                std::memcpy(&value, request.payload, sizeof(value));
                std::memcpy(reply.payload, &value, sizeof(value));
                reply.payloadSize = sizeof(value);
                reply.status = kernel::kIpcOk;
                break;
            }
            case kMethodAdd:
            {
                // Add two uint32s
                std::uint32_t a = 0;
                std::uint32_t b = 0;
                std::memcpy(&a, request.payload, sizeof(a));
                std::memcpy(&b, request.payload + sizeof(a), sizeof(b));
                std::uint32_t sum = a + b;
                std::memcpy(reply.payload, &sum, sizeof(sum));
                reply.payloadSize = sizeof(sum);
                reply.status = kernel::kIpcOk;
                break;
            }
            case kMethodGetCount:
            {
                // Return request count
                std::memcpy(reply.payload, &requestCount, sizeof(requestCount));
                reply.payloadSize = sizeof(requestCount);
                reply.status = kernel::kIpcOk;
                break;
            }
            default:
                reply.status = kernel::kIpcErrMethod;
                break;
            }

            kernel::messageReply(request.sender, reply);
        }
    }

    // ---- Echo client ----

    void echoClientThread(void *)
    {
        uartPrintLine("echo: client started");

        // Wait for server to be ready
        kernel::sleep(100);

        kernel::ThreadId server = g_serverTid;
        std::uint32_t iteration = 0;
        char buf[16];

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
                std::int32_t rc = kernel::messageSend(server, request, &reply);

                uartPrint("ping(");
                uintToStr(value, buf, sizeof(buf));
                uartPrint(buf);
                uartPrint(")=");
                if (rc == kernel::kIpcOk)
                {
                    std::uint32_t result = 0;
                    std::memcpy(&result, reply.payload, sizeof(result));
                    uintToStr(result, buf, sizeof(buf));
                    uartPrint(buf);
                    uartPrint(" rc=");
                    int32ToStr(reply.status, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
                else
                {
                    uartPrint("ERR ");
                    int32ToStr(rc, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
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
                std::int32_t rc = kernel::messageSend(server, request, &reply);

                uartPrint("add(");
                uintToStr(a, buf, sizeof(buf));
                uartPrint(buf);
                uartPrint(",");
                uintToStr(b, buf, sizeof(buf));
                uartPrint(buf);
                uartPrint(")=");
                if (rc == kernel::kIpcOk)
                {
                    std::uint32_t sum = 0;
                    std::memcpy(&sum, reply.payload, sizeof(sum));
                    uintToStr(sum, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
                else
                {
                    uartPrint("ERR ");
                    int32ToStr(rc, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
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
                std::int32_t rc = kernel::messageSend(server, request, &reply);

                uartPrint("count=");
                if (rc == kernel::kIpcOk)
                {
                    std::uint32_t count = 0;
                    std::memcpy(&count, reply.payload, sizeof(count));
                    uintToStr(count, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
                else
                {
                    uartPrint("ERR ");
                    int32ToStr(rc, buf, sizeof(buf));
                    uartPrintLine(buf);
                }
            }

            ++iteration;
            kernel::sleep(2000);
        }
    }

    // ---- LED heartbeat ----

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

    hal::uartWriteString(board::kConsoleUart, "ms-os ipc-demo starting\r\n");

    // Initialize kernel
    kernel::init();

    // Create server thread (higher priority so it services requests quickly)
    g_serverTid = kernel::createThread(echoServerThread, nullptr, "echo-srv",
                                       g_serverStack, sizeof(g_serverStack), 8);

    // Create client thread
    kernel::createThread(echoClientThread, nullptr, "echo-cli",
                         g_clientStack, sizeof(g_clientStack), 10);

    // Create LED heartbeat thread
    kernel::createThread(ledThread, nullptr, "led",
                         g_ledStack, sizeof(g_ledStack), 15);

    // Start scheduler -- does not return
    kernel::startScheduler();

    while (true)
    {
    }
}
