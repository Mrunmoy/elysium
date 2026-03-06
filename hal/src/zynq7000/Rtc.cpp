// Zynq-7000 RTC stub (no RTC peripheral on PS).

#include "hal/Rtc.h"

namespace hal
{
    void rtcInit(RtcClockSource) {}
    void rtcSetTime(const RtcTime &) {}
    void rtcGetTime(RtcTime &time)
    {
        time.hours = 0;
        time.minutes = 0;
        time.seconds = 0;
    }
    void rtcSetDate(const RtcDate &) {}
    void rtcGetDate(RtcDate &date)
    {
        date.year = 0;
        date.month = 1;
        date.day = 1;
        date.weekday = 1;
    }
    void rtcSetAlarm(const RtcAlarmConfig &, RtcAlarmCallbackFn, void *) {}
    void rtcCancelAlarm() {}
    bool rtcIsReady() { return false; }
}  // namespace hal
