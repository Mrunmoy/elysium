// UART2 Echo Server -- Phase 13 board-to-board integration test
//
// Runs on Board 2 (CMSIS-DAP). Echoes any byte received on USART2 back
// on USART2. Uses interrupt-driven RX with WFI for low-power idle.
// LED on PC13 toggles on each echoed byte for visual activity indicator.
//
// Board: STM32F407ZGT6
//   USART2: PA2 (TX, AF7), PA3 (RX, AF7) -- cross-wired to Board 1
//   LED: PC13 (active low)
//   Console: USART1 TX on PA9 (AF7)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Uart.h"

extern "C" const std::uint8_t g_boardDtb[];
extern "C" const std::uint32_t g_boardDtbSize;

namespace
{
    hal::UartId g_consoleUart;

    // RX interrupt sets this flag; main loop clears it after draining buffer.
    volatile bool g_rxReady = false;

    void rxNotify(void * /* arg */)
    {
        g_rxReady = true;
    }

    void print(const char *msg)
    {
        hal::uartWriteString(g_consoleUart, msg);
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
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();
    initLed();

    print("\r\n=== UART2 Echo Server (Phase 13) ===\r\n");
    print("Board 2: Echo on USART2, LED on PC13\r\n\r\n");

    initUsart2();

    print("UART2 Echo Server ready\r\n");

    // RX activity LED: ON while echoing, OFF when idle.
    // uartPutChar waits for TXE (~87us per byte at 115200) which
    // provides natural LED-ON time -- no artificial delay needed.
    hal::gpioSet(hal::Port::C, 13);  // LED OFF at start

    while (true)
    {
        char c;
        if (hal::uartTryGetChar(hal::UartId::Usart2, &c))
        {
            hal::gpioClear(hal::Port::C, 13);  // LED ON
            hal::uartPutChar(hal::UartId::Usart2, c);

            // Drain remaining bytes in ring buffer.
            while (hal::uartTryGetChar(hal::UartId::Usart2, &c))
            {
                hal::uartPutChar(hal::UartId::Usart2, c);
            }

            hal::gpioSet(hal::Port::C, 13);  // LED OFF
        }
    }
}
