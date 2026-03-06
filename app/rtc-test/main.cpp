// RTC hardware test -- Phase RTC
//
// Runs on STM32F407 (Board 1, J-Link).
// Tests RTC init (LSI), set/get time, set/get date, time advancement, alarm callback.
// Console: USART1 on PA9 (115200).

#include "kernel/BoardConfig.h"
#include "hal/Gpio.h"
#include "hal/Rcc.h"
#include "hal/Rtc.h"
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

    void printTwoDigits(std::uint8_t val)
    {
        hal::uartPutChar(g_consoleUart, '0' + static_cast<char>(val / 10));
        hal::uartPutChar(g_consoleUart, '0' + static_cast<char>(val % 10));
    }

    void printTime(const hal::RtcTime &t)
    {
        printTwoDigits(t.hours);
        hal::uartPutChar(g_consoleUart, ':');
        printTwoDigits(t.minutes);
        hal::uartPutChar(g_consoleUart, ':');
        printTwoDigits(t.seconds);
    }

    void printDate(const hal::RtcDate &d)
    {
        print("20");
        printTwoDigits(d.year);
        hal::uartPutChar(g_consoleUart, '-');
        printTwoDigits(d.month);
        hal::uartPutChar(g_consoleUart, '-');
        printTwoDigits(d.day);
        print(" (wd=");
        printDecimal(d.weekday);
        hal::uartPutChar(g_consoleUart, ')');
    }

    void printMachineCase(const char *caseName, bool pass)
    {
        print("MSOS_CASE:rtc:");
        print(caseName);
        print(pass ? ":PASS\r\n" : ":FAIL\r\n");
    }

    // Simple busy-wait delay using TIM7
    void delayMs(std::uint32_t ms)
    {
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            hal::timerDelayUs(1000);
        }
    }

    void initDelayTimer()
    {
        hal::rccEnableTimerClock(hal::TimerId::Tim7);

        // TIM7 at 1 MHz: PSC = (APB1_timer_clock / 1MHz) - 1
        // APB1 timer clock = 84 MHz on F407
        hal::TimerConfig tc;
        tc.id = hal::TimerId::Tim7;
        tc.prescaler = 83;
        tc.period = 0xFFFF;
        tc.autoReload = true;
        tc.onePulse = false;
        hal::timerInit(tc);
    }

    volatile bool g_alarmFired = false;

    void alarmCallback(void *arg)
    {
        volatile bool *flag = static_cast<volatile bool *>(arg);
        *flag = true;
    }

    // ---- Test 1: Init and ready ----
    bool testInitAndReady()
    {
        hal::rtcInit(hal::RtcClockSource::Lsi);

        // After init, RSF should eventually be set
        // Give it a moment to synchronize
        delayMs(100);

        bool ready = hal::rtcIsReady();
        print("  rtcIsReady() = ");
        print(ready ? "true" : "false");
        print("\r\n");
        return ready;
    }

    // ---- Test 2: Set/get time ----
    bool testSetGetTime()
    {
        hal::RtcTime tSet;
        tSet.hours = 14;
        tSet.minutes = 30;
        tSet.seconds = 0;
        hal::rtcSetTime(tSet);

        delayMs(50);

        hal::RtcTime tGet;
        hal::rtcGetTime(tGet);

        print("  Set: ");
        printTime(tSet);
        print("  Got: ");
        printTime(tGet);
        print("\r\n");

        return (tGet.hours == 14 && tGet.minutes == 30 && tGet.seconds <= 1);
    }

    // ---- Test 3: Set/get date ----
    bool testSetGetDate()
    {
        hal::RtcDate dSet;
        dSet.year = 26;
        dSet.month = 3;
        dSet.day = 6;
        dSet.weekday = 5;  // Friday
        hal::rtcSetDate(dSet);

        delayMs(50);

        hal::RtcDate dGet;
        hal::rtcGetDate(dGet);

        print("  Set: ");
        printDate(dSet);
        print("  Got: ");
        printDate(dGet);
        print("\r\n");

        return (dGet.year == 26 && dGet.month == 3 && dGet.day == 6 && dGet.weekday == 5);
    }

    // ---- Test 4: Time advances ----
    bool testTimeAdvances()
    {
        hal::RtcTime tSet;
        tSet.hours = 12;
        tSet.minutes = 0;
        tSet.seconds = 0;
        hal::rtcSetTime(tSet);

        print("  Waiting ~3 seconds...\r\n");
        delayMs(3200);  // Wait 3.2s (extra margin for LSI drift)

        hal::RtcTime tGet;
        hal::rtcGetTime(tGet);

        print("  After wait: ");
        printTime(tGet);
        print("\r\n");

        // Seconds should have advanced by 2-4 (LSI is ~32 kHz, not exact)
        return (tGet.hours == 12 && tGet.minutes == 0 && tGet.seconds >= 2 && tGet.seconds <= 5);
    }

    // ---- Test 5: Alarm fires ----
    bool testAlarmFires()
    {
        // Set time to 10:00:00
        hal::RtcTime tSet;
        tSet.hours = 10;
        tSet.minutes = 0;
        tSet.seconds = 0;
        hal::rtcSetTime(tSet);

        delayMs(50);

        // Set alarm to match seconds=3 (should fire in ~3s)
        g_alarmFired = false;

        hal::RtcAlarmConfig cfg;
        cfg.hours = 0;
        cfg.minutes = 0;
        cfg.seconds = 3;
        cfg.maskHours = true;
        cfg.maskMinutes = true;
        cfg.maskSeconds = false;
        cfg.maskDate = true;

        hal::rtcSetAlarm(cfg, alarmCallback, const_cast<bool *>(&g_alarmFired));

        print("  Alarm set for :03, waiting...\r\n");

        // Wait up to 5 seconds
        for (std::uint32_t i = 0; i < 50; ++i)
        {
            if (g_alarmFired)
            {
                break;
            }
            delayMs(100);
        }

        hal::RtcTime tNow;
        hal::rtcGetTime(tNow);
        print("  Time when checked: ");
        printTime(tNow);
        print("\r\n");
        print("  Alarm fired: ");
        print(g_alarmFired ? "yes" : "no");
        print("\r\n");

        hal::rtcCancelAlarm();

        return g_alarmFired;
    }

}  // namespace

int main()
{
    // Init board config from DTB
    board::configInit(g_boardDtb, g_boardDtbSize);

    // Init console UART via board config
    const board::BoardConfig &cfg = board::config();
    if (cfg.hasConsoleTx)
    {
        hal::rccEnableGpioClock(hal::Port(cfg.consoleTx.port - 'A'));

        hal::GpioConfig txPin;
        txPin.port = hal::Port(cfg.consoleTx.port - 'A');
        txPin.pin = cfg.consoleTx.pin;
        txPin.mode = hal::PinMode::AlternateFunction;
        txPin.speed = hal::OutputSpeed::VeryHigh;
        txPin.alternateFunction = cfg.consoleTx.af;
        hal::gpioInit(txPin);
    }

    g_consoleUart = board::consoleUartId();
    hal::rccEnableUartClock(g_consoleUart);

    hal::UartConfig uartCfg;
    uartCfg.id = g_consoleUart;
    uartCfg.baudRate = 115200;
    hal::uartInit(uartCfg);

    // Init TIM7 for delayUs
    initDelayTimer();

    print("\r\n=== RTC Hardware Test ===\r\n\r\n");

    std::uint32_t pass = 0;
    std::uint32_t total = 5;

    // Test 1
    print("[1/5] Init and ready\r\n");
    bool r1 = testInitAndReady();
    printMachineCase("init_ready", r1);
    if (r1) ++pass;

    // Test 2
    print("[2/5] Set/get time\r\n");
    bool r2 = testSetGetTime();
    printMachineCase("set_get_time", r2);
    if (r2) ++pass;

    // Test 3
    print("[3/5] Set/get date\r\n");
    bool r3 = testSetGetDate();
    printMachineCase("set_get_date", r3);
    if (r3) ++pass;

    // Test 4
    print("[4/5] Time advances\r\n");
    bool r4 = testTimeAdvances();
    printMachineCase("time_advances", r4);
    if (r4) ++pass;

    // Test 5
    print("[5/5] Alarm fires\r\n");
    bool r5 = testAlarmFires();
    printMachineCase("alarm_fires", r5);
    if (r5) ++pass;

    // Summary
    print("\r\nMSOS_SUMMARY:rtc:");
    printDecimal(pass);
    hal::uartPutChar(g_consoleUart, '/');
    printDecimal(total);
    print(pass == total ? ":PASS\r\n" : ":FAIL\r\n");

    print("\r\n=== Done ===\r\n");

    while (true)
    {
        __asm volatile("wfi");
    }
}
