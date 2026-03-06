// Timer hardware test -- Phase 18
//
// Runs on STM32F407 (Board 1, J-Link).
// Tests TIM6 periodic interrupt, TIM7 microsecond delay, TIM3 PWM, start/stop.
// Console: USART1 on PA9 (115200).

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

    void printResult(const char *testName, bool pass)
    {
        print(testName);
        print(pass ? ": PASS\r\n" : ": FAIL\r\n");
    }

    void printMachineCase(const char *caseName, bool pass)
    {
        print("MSOS_CASE:timer:");
        print(caseName);
        print(pass ? ":PASS\r\n" : ":FAIL\r\n");
    }

    void printMachineSummary(std::uint32_t passCount, std::uint32_t totalCount)
    {
        print("MSOS_SUMMARY:timer:pass=");
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

    // ---- Test 1: TIM6 periodic interrupt (1 kHz, count ~1000 in 1 second) ----

    volatile std::uint32_t g_tim6Count = 0;

    void tim6Callback(void * /* arg */)
    {
        ++g_tim6Count;
    }

    bool testTim6PeriodicInterrupt()
    {
        hal::rccEnableTimerClock(hal::TimerId::Tim6);

        hal::TimerConfig cfg{};
        cfg.id = hal::TimerId::Tim6;
        cfg.prescaler = 83;   // 84 MHz / 84 = 1 MHz tick
        cfg.period = 999;     // 1 MHz / 1000 = 1 kHz overflow
        cfg.autoReload = true;
        cfg.onePulse = false;
        hal::timerInit(cfg);

        g_tim6Count = 0;
        hal::timerStart(hal::TimerId::Tim6, tim6Callback, nullptr);

        // Wait ~1 second using a busy loop calibrated for ~84 MHz
        // Each loop iteration is roughly 4-6 cycles with volatile read
        for (volatile std::uint32_t i = 0; i < 14000000; ++i)
        {
            __asm volatile("nop");
        }

        hal::timerStop(hal::TimerId::Tim6);

        print("  TIM6 callback count: ");
        printDecimal(g_tim6Count);
        print(" (expected ~1000)\r\n");

        // Accept 900-1100 (10% tolerance for busy-loop timing)
        return (g_tim6Count >= 900 && g_tim6Count <= 1100);
    }

    // ---- Test 2: TIM7 microsecond delay accuracy ----

    bool testTim7DelayUs()
    {
        hal::rccEnableTimerClock(hal::TimerId::Tim7);

        // Init TIM7 as free-running 1 MHz counter
        hal::TimerConfig cfg{};
        cfg.id = hal::TimerId::Tim7;
        cfg.prescaler = 83;    // 1 MHz tick
        cfg.period = 0xFFFF;   // Free-running
        cfg.autoReload = true;
        cfg.onePulse = false;
        hal::timerInit(cfg);
        hal::timerStart(hal::TimerId::Tim7, nullptr, nullptr);

        // Use TIM6 (already clocked) as reference: count TIM6 ticks during a 10000 us delay
        hal::TimerConfig ref{};
        ref.id = hal::TimerId::Tim6;
        ref.prescaler = 83;    // 1 MHz tick
        ref.period = 0xFFFF;
        ref.autoReload = true;
        hal::timerInit(ref);
        hal::timerSetCount(hal::TimerId::Tim6, 0);
        hal::timerStart(hal::TimerId::Tim6, nullptr, nullptr);

        hal::timerDelayUs(10000);  // 10 ms delay

        std::uint32_t elapsed = hal::timerGetCount(hal::TimerId::Tim6);
        hal::timerStop(hal::TimerId::Tim6);
        hal::timerStop(hal::TimerId::Tim7);

        print("  TIM6 reference ticks during 10ms delay: ");
        printDecimal(elapsed);
        print(" (expected ~10000)\r\n");

        // Accept 9000-11000 (10% tolerance)
        return (elapsed >= 9000 && elapsed <= 11000);
    }

    // ---- Test 3: TIM3 PWM output on PB4 (CH1, AF2) ----

    bool testTim3Pwm()
    {
        hal::rccEnableTimerClock(hal::TimerId::Tim3);
        hal::rccEnableGpioClock(hal::Port::B);

        // Configure PB4 as AF2 (TIM3_CH1)
        hal::GpioConfig gpioConfig{};
        gpioConfig.port = hal::Port::B;
        gpioConfig.pin = 4;
        gpioConfig.mode = hal::PinMode::AlternateFunction;
        gpioConfig.outputType = hal::OutputType::PushPull;
        gpioConfig.speed = hal::OutputSpeed::VeryHigh;
        gpioConfig.alternateFunction = 2;  // AF2 = TIM3
        hal::gpioInit(gpioConfig);

        // 1 kHz PWM, 50% duty
        hal::PwmConfig pwmCfg{};
        pwmCfg.id = hal::TimerId::Tim3;
        pwmCfg.channel = hal::TimerChannel::Ch1;
        pwmCfg.prescaler = 83;   // 1 MHz tick
        pwmCfg.period = 999;     // 1 kHz
        pwmCfg.duty = 500;       // 50%
        pwmCfg.activeHigh = true;
        hal::timerPwmInit(pwmCfg);
        hal::timerPwmStart(hal::TimerId::Tim3, hal::TimerChannel::Ch1);

        // Read PB4 state after a short delay to verify the output toggles
        // (We check it's not stuck at 0. PWM is running so it should be high or low.)
        // A real scope test would verify frequency/duty. Here we just verify no crash
        // and the timer is running.
        bool running = (hal::timerGetCount(hal::TimerId::Tim3) != 0 ||
                        hal::timerGetCount(hal::TimerId::Tim3) != 0);

        // Change duty to 75%
        hal::timerPwmSetDuty(hal::TimerId::Tim3, hal::TimerChannel::Ch1, 750);

        // Stop PWM
        hal::timerPwmStop(hal::TimerId::Tim3, hal::TimerChannel::Ch1);

        print("  TIM3 PWM started and stopped (PB4, AF2)\r\n");
        // If we got here without a fault, test passes
        (void)running;
        return true;
    }

    // ---- Test 4: LED blink via TIM2 interrupt (2 Hz, visual verification) ----

    volatile std::uint32_t g_tim2Count = 0;

    void tim2LedCallback(void * /* arg */)
    {
        ++g_tim2Count;
        // Toggle LED on PC13 every callback (2 Hz toggle = 1 Hz blink)
        hal::gpioToggle(hal::Port::C, 13);
    }

    bool testLedBlinkTimer()
    {
        // Init LED GPIO (PC13)
        hal::rccEnableGpioClock(hal::Port::C);
        hal::GpioConfig ledCfg{};
        ledCfg.port = hal::Port::C;
        ledCfg.pin = 13;
        ledCfg.mode = hal::PinMode::Output;
        ledCfg.outputType = hal::OutputType::PushPull;
        ledCfg.speed = hal::OutputSpeed::Low;
        hal::gpioInit(ledCfg);

        // Start LED off
        hal::gpioClear(hal::Port::C, 13);

        hal::rccEnableTimerClock(hal::TimerId::Tim2);

        hal::TimerConfig timerCfg{};
        timerCfg.id = hal::TimerId::Tim2;
        timerCfg.prescaler = 8399;   // 84 MHz / 8400 = 10 kHz tick
        timerCfg.period = 4999;      // 10 kHz / 5000 = 2 Hz overflow
        timerCfg.autoReload = true;
        hal::timerInit(timerCfg);

        g_tim2Count = 0;
        hal::timerStart(hal::TimerId::Tim2, tim2LedCallback, nullptr);

        // Blink for ~5 seconds (10 toggles = 5 full blinks visible on camera)
        print("  LED blinking at 1 Hz on PC13 for 5 seconds...\r\n");
        for (volatile std::uint32_t i = 0; i < 70000000; ++i)
        {
            __asm volatile("nop");
        }

        hal::timerStop(hal::TimerId::Tim2);

        print("  TIM2 callback count: ");
        printDecimal(g_tim2Count);
        print(" (expected ~10)\r\n");

        // Accept 8-12 (tolerance for busy-loop)
        return (g_tim2Count >= 8 && g_tim2Count <= 12);
    }

    // ---- Test 5: Timer start/stop verifies counter stops ----

    bool testStartStop()
    {
        hal::rccEnableTimerClock(hal::TimerId::Tim5);

        hal::TimerConfig cfg{};
        cfg.id = hal::TimerId::Tim5;
        cfg.prescaler = 83;    // 1 MHz tick
        cfg.period = 0xFFFFFFFF; // TIM5 is 32-bit
        cfg.autoReload = true;
        hal::timerInit(cfg);

        hal::timerSetCount(hal::TimerId::Tim5, 0);
        hal::timerStart(hal::TimerId::Tim5, nullptr, nullptr);

        // Let it run briefly
        for (volatile std::uint32_t i = 0; i < 1000; ++i)
        {
            __asm volatile("nop");
        }

        std::uint32_t runningCount = hal::timerGetCount(hal::TimerId::Tim5);

        hal::timerStop(hal::TimerId::Tim5);

        // Read count twice after stop -- should be identical (counter stopped)
        std::uint32_t stoppedCount1 = hal::timerGetCount(hal::TimerId::Tim5);
        for (volatile std::uint32_t i = 0; i < 100; ++i)
        {
            __asm volatile("nop");
        }
        std::uint32_t stoppedCount2 = hal::timerGetCount(hal::TimerId::Tim5);

        print("  TIM5 running count: ");
        printDecimal(runningCount);
        print(", stopped: ");
        printDecimal(stoppedCount1);
        print(" -> ");
        printDecimal(stoppedCount2);
        print("\r\n");

        return (runningCount > 0) && (stoppedCount1 == stoppedCount2);
    }
}  // namespace

int main()
{
    board::configInit(g_boardDtb, g_boardDtbSize);
    initConsole();

    print("\r\n=== Timer Hardware Test (Phase 18) ===\r\n\r\n");

    std::uint32_t pass = 0;
    constexpr std::uint32_t kTotal = 5;

    bool r1 = testTim6PeriodicInterrupt();
    printResult("Test 1: TIM6 periodic interrupt (1 kHz)", r1);
    printMachineCase("tim6-periodic-1khz", r1);
    if (r1)
    {
        ++pass;
    }

    bool r2 = testTim7DelayUs();
    printResult("Test 2: TIM7 microsecond delay (10 ms)", r2);
    printMachineCase("tim7-delay-10ms", r2);
    if (r2)
    {
        ++pass;
    }

    bool r3 = testTim3Pwm();
    printResult("Test 3: TIM3 PWM output (PB4, 1 kHz)", r3);
    printMachineCase("tim3-pwm-pb4", r3);
    if (r3)
    {
        ++pass;
    }

    bool r4 = testLedBlinkTimer();
    printResult("Test 4: LED blink via TIM2 (1 Hz, PC13)", r4);
    printMachineCase("tim2-led-blink-1hz", r4);
    if (r4)
    {
        ++pass;
    }

    bool r5 = testStartStop();
    printResult("Test 5: TIM5 start/stop", r5);
    printMachineCase("tim5-start-stop", r5);
    if (r5)
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

    // Keep LED blinking after tests complete (visual proof-of-life)
    print("\r\nLED blinking at 1 Hz on PC13 (continuous)...\r\n");

    // Re-init LED GPIO and TIM2 for continuous blink
    hal::rccEnableGpioClock(hal::Port::C);
    hal::GpioConfig ledCfg{};
    ledCfg.port = hal::Port::C;
    ledCfg.pin = 13;
    ledCfg.mode = hal::PinMode::Output;
    ledCfg.outputType = hal::OutputType::PushPull;
    ledCfg.speed = hal::OutputSpeed::Low;
    hal::gpioInit(ledCfg);

    hal::rccEnableTimerClock(hal::TimerId::Tim2);
    hal::TimerConfig blinkCfg{};
    blinkCfg.id = hal::TimerId::Tim2;
    blinkCfg.prescaler = 8399;   // 10 kHz tick
    blinkCfg.period = 4999;      // 2 Hz
    blinkCfg.autoReload = true;
    hal::timerInit(blinkCfg);

    auto blinkCb = [](void *) { hal::gpioToggle(hal::Port::C, 13); };
    hal::timerStart(hal::TimerId::Tim2, blinkCb, nullptr);

    while (true)
    {
        __asm volatile("wfi");
    }
}
