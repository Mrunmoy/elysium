#pragma once

#include "msos/ErrorCode.h"

#include <cstdint>

namespace hal
{
    enum class TimerId : std::uint8_t
    {
        Tim2 = 0,
        Tim3,
        Tim4,
        Tim5,
        Tim6,
        Tim7
    };

    constexpr std::uint32_t kTimerCount = 6;

    enum class TimerChannel : std::uint8_t
    {
        Ch1 = 0,
        Ch2,
        Ch3,
        Ch4
    };

    using TimerCallbackFn = void (*)(void *arg);

    struct TimerConfig
    {
        TimerId id;
        std::uint16_t prescaler = 0;   // Divides timer clock by (prescaler + 1)
        std::uint32_t period = 0;       // Auto-reload value (ARR)
        bool autoReload = true;         // ARPE: buffer ARR writes
        bool onePulse = false;          // OPM: stop after one update event
    };

    struct PwmConfig
    {
        TimerId id;
        TimerChannel channel;
        std::uint16_t prescaler = 0;
        std::uint32_t period = 0;       // ARR: determines PWM frequency
        std::uint32_t duty = 0;         // CCRx: determines duty cycle
        bool activeHigh = true;         // Output polarity
    };

    // Basic timer: init, start with periodic interrupt, stop
    void timerInit(const TimerConfig &config);
    void timerStart(TimerId id, TimerCallbackFn callback, void *arg);
    void timerStop(TimerId id);

    // Counter access
    std::uint32_t timerGetCount(TimerId id);
    void timerSetCount(TimerId id, std::uint32_t count);

    // Period/prescaler update (takes effect at next update event if ARPE=1)
    void timerSetPeriod(TimerId id, std::uint32_t period);
    void timerSetPrescaler(TimerId id, std::uint16_t prescaler);

    // PWM output
    void timerPwmInit(const PwmConfig &config);
    void timerPwmStart(TimerId id, TimerChannel channel);
    void timerPwmStop(TimerId id, TimerChannel channel);
    void timerPwmSetDuty(TimerId id, TimerChannel channel, std::uint32_t duty);

    // Microsecond delay (blocking, uses TIM7)
    void timerDelayUs(std::uint32_t us);

    // RCC clock enable/disable
    void rccEnableTimerClock(TimerId id);
    void rccDisableTimerClock(TimerId id);

}  // namespace hal
