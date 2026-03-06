// Mock RTC implementation for host-side testing.
// Replaces hal/src/stm32f4/Rtc.cpp at link time.

#include "hal/Rtc.h"

#include "MockRegisters.h"

namespace hal
{
    void rtcInit(RtcClockSource clockSource)
    {
        test::g_rtcInitCalls.push_back({
            static_cast<std::uint8_t>(clockSource),
        });
    }

    void rtcSetTime(const RtcTime &time)
    {
        test::g_rtcSetTimeCalls.push_back({
            time.hours,
            time.minutes,
            time.seconds,
        });
        test::g_rtcHours = time.hours;
        test::g_rtcMinutes = time.minutes;
        test::g_rtcSeconds = time.seconds;
    }

    void rtcGetTime(RtcTime &time)
    {
        time.hours = test::g_rtcHours;
        time.minutes = test::g_rtcMinutes;
        time.seconds = test::g_rtcSeconds;
    }

    void rtcSetDate(const RtcDate &date)
    {
        test::g_rtcSetDateCalls.push_back({
            date.year,
            date.month,
            date.day,
            date.weekday,
        });
        test::g_rtcYear = date.year;
        test::g_rtcMonth = date.month;
        test::g_rtcDay = date.day;
        test::g_rtcWeekday = date.weekday;
    }

    void rtcGetDate(RtcDate &date)
    {
        date.year = test::g_rtcYear;
        date.month = test::g_rtcMonth;
        date.day = test::g_rtcDay;
        date.weekday = test::g_rtcWeekday;
    }

    void rtcSetAlarm(const RtcAlarmConfig &config, RtcAlarmCallbackFn callback, void *arg)
    {
        test::g_rtcAlarmCalls.push_back({
            config.hours,
            config.minutes,
            config.seconds,
            config.maskHours,
            config.maskMinutes,
            config.maskSeconds,
            config.maskDate,
            reinterpret_cast<void *>(callback),
            arg,
        });
    }

    void rtcCancelAlarm()
    {
        ++test::g_rtcCancelAlarmCount;
    }

    bool rtcIsReady()
    {
        return test::g_rtcReady;
    }

}  // namespace hal
