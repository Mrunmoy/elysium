// Timer logic analyzer verification
//
// Outputs known PWM frequencies on PB4 (TIM3_CH1, AF2) for Saleae capture.
// Connect Saleae channel 0 to PB4, GND to board GND.
//
// Sequence:
//   1. 1 kHz, 50% duty  -- 3 seconds
//   2. 10 kHz, 25% duty -- 3 seconds
//   3. 100 Hz, 75% duty -- 3 seconds
//   4. GPIO toggle at max speed via TIM4 ISR -- 2 seconds (measures ISR latency)
//   5. Continuous 1 kHz 50% PWM (leave running for extended capture)

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Timer.h"
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

    void initPwmPin()
    {
        hal::rccEnableGpioClock(hal::Port::B);
        hal::rccEnableTimerClock(hal::TimerId::Tim3);

        hal::GpioConfig gpioConfig{};
        gpioConfig.port = hal::Port::B;
        gpioConfig.pin = 4;
        gpioConfig.mode = hal::PinMode::AlternateFunction;
        gpioConfig.outputType = hal::OutputType::PushPull;
        gpioConfig.speed = hal::OutputSpeed::VeryHigh;
        gpioConfig.alternateFunction = 2;  // AF2 = TIM3
        hal::gpioInit(gpioConfig);
    }

    void busyWaitSeconds(std::uint32_t seconds)
    {
        // ~14M iterations per second at 168 MHz
        for (std::uint32_t s = 0; s < seconds; ++s)
        {
            for (volatile std::uint32_t i = 0; i < 14000000; ++i)
            {
                __asm volatile("nop");
            }
        }
    }

    void runPwm(std::uint16_t prescaler, std::uint32_t period, std::uint32_t duty,
                const char *label, std::uint32_t durationSec)
    {
        print("  ");
        print(label);
        print(" -- ");
        printDecimal(durationSec);
        print("s\r\n");

        hal::PwmConfig pwmCfg{};
        pwmCfg.id = hal::TimerId::Tim3;
        pwmCfg.channel = hal::TimerChannel::Ch1;
        pwmCfg.prescaler = prescaler;
        pwmCfg.period = period;
        pwmCfg.duty = duty;
        pwmCfg.activeHigh = true;
        hal::timerPwmInit(pwmCfg);
        hal::timerPwmStart(hal::TimerId::Tim3, hal::TimerChannel::Ch1);

        busyWaitSeconds(durationSec);

        hal::timerPwmStop(hal::TimerId::Tim3, hal::TimerChannel::Ch1);
        hal::timerStop(hal::TimerId::Tim3);
    }

    // ISR toggle test: TIM4 fires at known rate, ISR toggles PB4 via GPIO
    volatile std::uint32_t g_toggleCount = 0;

    void toggleCallback(void * /* arg */)
    {
        hal::gpioToggle(hal::Port::B, 4);
        ++g_toggleCount;
    }

    void runIsrToggle(std::uint32_t frequencyHz, std::uint32_t durationSec)
    {
        print("  ISR toggle at ");
        printDecimal(frequencyHz);
        print(" Hz -- ");
        printDecimal(durationSec);
        print("s\r\n");

        // Reconfigure PB4 as plain GPIO output for toggle test
        hal::GpioConfig gpioConfig{};
        gpioConfig.port = hal::Port::B;
        gpioConfig.pin = 4;
        gpioConfig.mode = hal::PinMode::Output;
        gpioConfig.outputType = hal::OutputType::PushPull;
        gpioConfig.speed = hal::OutputSpeed::VeryHigh;
        hal::gpioInit(gpioConfig);

        hal::rccEnableTimerClock(hal::TimerId::Tim4);

        // Calculate prescaler and period for desired frequency
        // Timer clock = 84 MHz. Use PSC=83 -> 1 MHz tick, ARR = 1M/freq - 1
        std::uint32_t arr = (1000000 / frequencyHz) - 1;

        hal::TimerConfig cfg{};
        cfg.id = hal::TimerId::Tim4;
        cfg.prescaler = 83;
        cfg.period = arr;
        cfg.autoReload = true;
        hal::timerInit(cfg);

        g_toggleCount = 0;
        hal::timerStart(hal::TimerId::Tim4, toggleCallback, nullptr);

        busyWaitSeconds(durationSec);

        hal::timerStop(hal::TimerId::Tim4);

        print("  Toggle count: ");
        printDecimal(g_toggleCount);
        print("\r\n");

        // Restore PB4 as AF2 for PWM
        gpioConfig.mode = hal::PinMode::AlternateFunction;
        gpioConfig.alternateFunction = 2;
        hal::gpioInit(gpioConfig);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();

    print("\r\n=== Timer Logic Analyzer Test ===\r\n");
    print("PB4 (TIM3_CH1, AF2) -- connect Saleae CH0\r\n\r\n");

    initPwmPin();

    // Timer clock = 84 MHz (APB1 timers, F407)
    // PSC=83 -> 1 MHz tick
    // 1 kHz: ARR=999, 50% duty: CCR=500
    runPwm(83, 999, 500, "1 kHz, 50% duty (expect: f=1000 Hz, duty=50.0%)", 3);

    // 10 kHz: ARR=99, 25% duty: CCR=25
    runPwm(83, 99, 25, "10 kHz, 25% duty (expect: f=10000 Hz, duty=25.0%)", 3);

    // 100 Hz: ARR=9999, 75% duty: CCR=7500
    runPwm(83, 9999, 7500, "100 Hz, 75% duty (expect: f=100 Hz, duty=75.0%)", 3);

    // ISR toggle: 20 kHz toggle = 10 kHz square wave on the pin
    runIsrToggle(20000, 2);

    // Leave running: 1 kHz 50% for extended capture
    print("\r\nContinuous: 1 kHz, 50% duty on PB4\r\n");
    hal::PwmConfig finalCfg{};
    finalCfg.id = hal::TimerId::Tim3;
    finalCfg.channel = hal::TimerChannel::Ch1;
    finalCfg.prescaler = 83;
    finalCfg.period = 999;
    finalCfg.duty = 500;
    finalCfg.activeHigh = true;
    hal::timerPwmInit(finalCfg);
    hal::timerPwmStart(hal::TimerId::Tim3, hal::TimerChannel::Ch1);

    print("Done. PWM running indefinitely.\r\n");

    while (true)
    {
        __asm volatile("wfi");
    }
}
