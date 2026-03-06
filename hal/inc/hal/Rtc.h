#pragma once

#include <cstdint>

namespace hal
{
    enum class RtcClockSource : std::uint8_t
    {
        Lsi = 0,  // Internal ~32 kHz RC oscillator
        Lse,      // External 32.768 kHz crystal
    };

    struct RtcTime
    {
        std::uint8_t hours = 0;    // 0-23
        std::uint8_t minutes = 0;  // 0-59
        std::uint8_t seconds = 0;  // 0-59
    };

    struct RtcDate
    {
        std::uint8_t year = 0;     // 0-99 (offset from 2000)
        std::uint8_t month = 1;    // 1-12
        std::uint8_t day = 1;      // 1-31
        std::uint8_t weekday = 1;  // 1-7 (1=Monday)
    };

    struct RtcAlarmConfig
    {
        std::uint8_t hours = 0;
        std::uint8_t minutes = 0;
        std::uint8_t seconds = 0;
        bool maskHours = true;     // true = ignore hours
        bool maskMinutes = true;   // true = ignore minutes
        bool maskSeconds = false;  // false = match seconds
        bool maskDate = true;      // true = ignore date
    };

    using RtcAlarmCallbackFn = void (*)(void *arg);

    void rtcInit(RtcClockSource clockSource);
    void rtcSetTime(const RtcTime &time);
    void rtcGetTime(RtcTime &time);
    void rtcSetDate(const RtcDate &date);
    void rtcGetDate(RtcDate &date);
    void rtcSetAlarm(const RtcAlarmConfig &config, RtcAlarmCallbackFn callback, void *arg);
    void rtcCancelAlarm();
    bool rtcIsReady();

}  // namespace hal
