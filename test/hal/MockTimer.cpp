// Mock Timer implementation for host-side testing.
// Replaces hal/src/stm32f4/Timer.cpp at link time.

#include "hal/Timer.h"

#include "MockRegisters.h"

namespace hal
{
    namespace
    {
        bool isValidTimerId(TimerId id)
        {
            return static_cast<std::uint8_t>(id) < kTimerCount;
        }
    }  // namespace

    void timerInit(const TimerConfig &config)
    {
        if (!isValidTimerId(config.id))
        {
            return;
        }

        test::g_timerInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            config.prescaler,
            config.period,
            config.autoReload,
            config.onePulse,
        });
    }

    void timerStart(TimerId id, TimerCallbackFn callback, void *arg)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        test::g_timerStartCalls.push_back({
            static_cast<std::uint8_t>(id),
            callback != nullptr,
        });
        test::g_timerCallback = reinterpret_cast<void *>(callback);
        test::g_timerCallbackArg = arg;
    }

    void timerStop(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        ++test::g_timerStopCount;
        test::g_timerCallback = nullptr;
        test::g_timerCallbackArg = nullptr;
    }

    std::uint32_t timerGetCount(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return 0;
        }
        return test::g_timerCount;
    }

    void timerSetCount(TimerId id, std::uint32_t count)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        test::g_timerSetCountVal = count;
        test::g_timerCount = count;
    }

    void timerSetPeriod(TimerId id, std::uint32_t period)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        test::g_timerSetPeriodVal = period;
    }

    void timerSetPrescaler(TimerId id, std::uint16_t prescaler)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        test::g_timerSetPrescalerVal = prescaler;
    }

    void timerPwmInit(const PwmConfig &config)
    {
        if (!isValidTimerId(config.id))
        {
            return;
        }

        test::g_pwmInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            static_cast<std::uint8_t>(config.channel),
            config.prescaler,
            config.period,
            config.duty,
            config.activeHigh,
        });
    }

    void timerPwmStart(TimerId id, TimerChannel channel)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        test::g_pwmChannelActions.push_back({
            static_cast<std::uint8_t>(id),
            static_cast<std::uint8_t>(channel),
            true,
        });
    }

    void timerPwmStop(TimerId id, TimerChannel channel)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        test::g_pwmChannelActions.push_back({
            static_cast<std::uint8_t>(id),
            static_cast<std::uint8_t>(channel),
            false,
        });
    }

    void timerPwmSetDuty(TimerId id, TimerChannel channel, std::uint32_t duty)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        test::g_pwmSetDutyCalls.push_back({
            static_cast<std::uint8_t>(id),
            static_cast<std::uint8_t>(channel),
            duty,
        });
    }

    void timerDelayUs(std::uint32_t us)
    {
        test::g_timerDelayUsValue = us;
    }

    void rccEnableTimerClock(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        test::g_timerRccCalls.push_back({
            static_cast<std::uint8_t>(id),
            true,
        });
    }

    void rccDisableTimerClock(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        test::g_timerRccCalls.push_back({
            static_cast<std::uint8_t>(id),
            false,
        });
    }

}  // namespace hal
