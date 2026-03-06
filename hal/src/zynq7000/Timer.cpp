// Zynq-7000 Timer stub
//
// The Zynq PS uses the SCU private timer and triple timer counters (TTC),
// which have a different register interface. All functions are no-ops.

#include "hal/Timer.h"

namespace hal
{
    void timerInit(const TimerConfig & /* config */) {}

    void timerStart(TimerId /* id */, TimerCallbackFn /* callback */, void * /* arg */) {}
    void timerStop(TimerId /* id */) {}

    std::uint32_t timerGetCount(TimerId /* id */) { return 0; }
    void timerSetCount(TimerId /* id */, std::uint32_t /* count */) {}

    void timerSetPeriod(TimerId /* id */, std::uint32_t /* period */) {}
    void timerSetPrescaler(TimerId /* id */, std::uint16_t /* prescaler */) {}

    void timerPwmInit(const PwmConfig & /* config */) {}
    void timerPwmStart(TimerId /* id */, TimerChannel /* channel */) {}
    void timerPwmStop(TimerId /* id */, TimerChannel /* channel */) {}
    void timerPwmSetDuty(TimerId /* id */, TimerChannel /* channel */, std::uint32_t /* duty */) {}

    void timerDelayUs(std::uint32_t /* us */) {}

    void rccEnableTimerClock(TimerId /* id */) {}
    void rccDisableTimerClock(TimerId /* id */) {}
}  // namespace hal
