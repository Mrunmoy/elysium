// STM32F2/F4 Timer driver
//
// Supports basic timers (TIM6, TIM7) and general-purpose timers (TIM2-TIM5).
// TIM2/TIM5 have 32-bit counters; TIM3/TIM4/TIM6/TIM7 are 16-bit.
// All timers in scope are on APB1 (timer clock = 84 MHz on F407, 60 MHz on F207).
//
// Register reference: RM0090 Section 18

#include "hal/Timer.h"

#include <cstdint>

namespace
{
    // Timer base addresses (RM0090 Table 1)
    constexpr std::uint32_t kTimerBase[] = {
        0x40000000,  // TIM2
        0x40000400,  // TIM3
        0x40000800,  // TIM4
        0x40000C00,  // TIM5
        0x40001000,  // TIM6
        0x40001400,  // TIM7
    };

    // Register offsets
    constexpr std::uint32_t kCr1 = 0x00;
    constexpr std::uint32_t kDier = 0x0C;
    constexpr std::uint32_t kSr = 0x10;
    constexpr std::uint32_t kEgr = 0x14;
    constexpr std::uint32_t kCcmr1 = 0x18;
    constexpr std::uint32_t kCcmr2 = 0x1C;
    constexpr std::uint32_t kCcer = 0x20;
    constexpr std::uint32_t kCnt = 0x24;
    constexpr std::uint32_t kPsc = 0x28;
    constexpr std::uint32_t kArr = 0x2C;
    constexpr std::uint32_t kCcr1 = 0x34;

    // CR1 bits
    constexpr std::uint32_t kCr1Cen = (1U << 0);
    constexpr std::uint32_t kCr1Urs = (1U << 2);
    constexpr std::uint32_t kCr1Opm = (1U << 3);
    constexpr std::uint32_t kCr1Arpe = (1U << 7);

    // DIER bits
    constexpr std::uint32_t kDierUie = (1U << 0);

    // EGR bits
    constexpr std::uint32_t kEgrUg = (1U << 0);

    // NVIC
    constexpr std::uint32_t kNvicIser = 0xE000E100;
    constexpr std::uint32_t kNvicIcer = 0xE000E180;

    // NVIC IRQ numbers for TIM2-TIM7
    constexpr std::uint8_t kTimerIrq[] = {
        28,  // TIM2
        29,  // TIM3
        30,  // TIM4
        50,  // TIM5
        54,  // TIM6 (shared with DAC)
        55,  // TIM7
    };

    // RCC
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kRccApb1enr = 0x40;

    // APB1ENR bits for TIM2-TIM7 are bits 0-5
    constexpr std::uint8_t kTimerRccBit[] = {0, 1, 2, 3, 4, 5};

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    std::uint32_t disableIrq()
    {
        std::uint32_t primask;
        __asm volatile("mrs %0, primask" : "=r"(primask));
        __asm volatile("cpsid i" ::: "memory");
        return primask;
    }

    void restoreIrq(std::uint32_t primask)
    {
        __asm volatile("msr primask, %0" ::"r"(primask) : "memory");
    }

    void nvicEnableIrq(std::uint8_t irqn)
    {
        reg(kNvicIser + (irqn / 32) * 4) = 1U << (irqn % 32);
    }

    void nvicDisableIrq(std::uint8_t irqn)
    {
        reg(kNvicIcer + (irqn / 32) * 4) = 1U << (irqn % 32);
    }

    bool isValidTimerId(hal::TimerId id)
    {
        return static_cast<std::uint8_t>(id) < hal::kTimerCount;
    }

    std::uint32_t timerBase(hal::TimerId id)
    {
        return kTimerBase[static_cast<std::uint8_t>(id)];
    }

    struct TimerState
    {
        hal::TimerCallbackFn callback = nullptr;
        void *arg = nullptr;
    };

    TimerState s_timerState[hal::kTimerCount];

    // CCRx register offset: CCR1 + channel * 4
    std::uint32_t ccrOffset(hal::TimerChannel ch)
    {
        return kCcr1 + static_cast<std::uint32_t>(ch) * 4;
    }

    // CCMR register offset and bit shift for a channel
    // Ch1/Ch2 -> CCMR1, Ch3/Ch4 -> CCMR2
    // Ch1/Ch3 -> bits 3-6, Ch2/Ch4 -> bits 11-14
    void ccmrConfig(std::uint32_t base, hal::TimerChannel ch)
    {
        std::uint8_t chIdx = static_cast<std::uint8_t>(ch);
        std::uint32_t ccmrOff = (chIdx < 2) ? kCcmr1 : kCcmr2;
        std::uint32_t shift = ((chIdx & 1) == 0) ? 0 : 8;

        // OCxM = 110 (PWM mode 1) at bits [6:4] or [14:12]
        // OCxPE = 1 (preload enable) at bit 3 or 11
        std::uint32_t val = reg(base + ccmrOff);
        val &= ~(0xFFU << shift);
        val |= (0x68U << shift);  // OC1M=110 (bits 6:4) + OC1PE=1 (bit 3) = 0b01101000
        reg(base + ccmrOff) = val;
    }

    void handleTimerIrq(std::uint8_t idx)
    {
        std::uint32_t base = kTimerBase[idx];
        std::uint32_t sr = reg(base + kSr);

        if (sr & 1U)
        {
            // Clear UIF by writing 0 to it (rc_w0)
            reg(base + kSr) = ~1U;

            auto &st = s_timerState[idx];
            if (st.callback)
            {
                st.callback(st.arg);
            }
        }
    }
}  // namespace

namespace hal
{
    void timerInit(const TimerConfig &config)
    {
        if (!isValidTimerId(config.id))
        {
            return;
        }

        std::uint32_t base = timerBase(config.id);

        // Disable counter
        reg(base + kCr1) &= ~kCr1Cen;

        // Set prescaler and auto-reload
        reg(base + kPsc) = config.prescaler;
        reg(base + kArr) = config.period;

        // Configure CR1: URS (overflow-only updates)
        std::uint32_t cr1 = kCr1Urs;
        if (config.autoReload)
        {
            cr1 |= kCr1Arpe;
        }
        if (config.onePulse)
        {
            cr1 |= kCr1Opm;
        }
        reg(base + kCr1) = cr1;

        // Generate update event to load shadow registers
        reg(base + kEgr) = kEgrUg;

        // Clear update interrupt flag (UG sets UIF)
        reg(base + kSr) = ~1U;
    }

    void timerStart(TimerId id, TimerCallbackFn callback, void *arg)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = timerBase(id);

        std::uint32_t saved = disableIrq();

        // Store callback
        s_timerState[idx].callback = callback;
        s_timerState[idx].arg = arg;

        if (callback)
        {
            // Enable update interrupt
            reg(base + kDier) |= kDierUie;
            nvicEnableIrq(kTimerIrq[idx]);
        }

        // Start counter
        reg(base + kCr1) |= kCr1Cen;

        restoreIrq(saved);
    }

    void timerStop(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint8_t idx = static_cast<std::uint8_t>(id);
        std::uint32_t base = timerBase(id);

        std::uint32_t saved = disableIrq();

        // Stop counter
        reg(base + kCr1) &= ~kCr1Cen;

        // Disable update interrupt
        reg(base + kDier) &= ~kDierUie;
        nvicDisableIrq(kTimerIrq[idx]);

        // Clear callback
        s_timerState[idx].callback = nullptr;
        s_timerState[idx].arg = nullptr;

        restoreIrq(saved);
    }

    std::uint32_t timerGetCount(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return 0;
        }
        return reg(timerBase(id) + kCnt);
    }

    void timerSetCount(TimerId id, std::uint32_t count)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        reg(timerBase(id) + kCnt) = count;
    }

    void timerSetPeriod(TimerId id, std::uint32_t period)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        reg(timerBase(id) + kArr) = period;
    }

    void timerSetPrescaler(TimerId id, std::uint16_t prescaler)
    {
        if (!isValidTimerId(id))
        {
            return;
        }
        reg(timerBase(id) + kPsc) = prescaler;
    }

    void timerPwmInit(const PwmConfig &config)
    {
        if (!isValidTimerId(config.id))
        {
            return;
        }

        std::uint32_t base = timerBase(config.id);
        std::uint8_t chIdx = static_cast<std::uint8_t>(config.channel);

        // Disable counter
        reg(base + kCr1) &= ~kCr1Cen;

        // Set prescaler and period
        reg(base + kPsc) = config.prescaler;
        reg(base + kArr) = config.period;

        // Configure output compare mode (PWM mode 1 + preload)
        ccmrConfig(base, config.channel);

        // Set duty cycle
        reg(base + ccrOffset(config.channel)) = config.duty;

        // Configure polarity and enable output
        std::uint32_t ccer = reg(base + kCcer);
        std::uint32_t ccBit = chIdx * 4;  // CC1E at bit 0, CC2E at bit 4, etc.
        ccer |= (1U << ccBit);            // CCxE = 1 (enable output)
        if (!config.activeHigh)
        {
            ccer |= (1U << (ccBit + 1));  // CCxP = 1 (active low)
        }
        else
        {
            ccer &= ~(1U << (ccBit + 1)); // CCxP = 0 (active high)
        }
        reg(base + kCcer) = ccer;

        // Enable auto-reload preload
        reg(base + kCr1) |= kCr1Arpe;

        // Generate update event to load shadow registers
        reg(base + kEgr) = kEgrUg;

        // Clear UIF
        reg(base + kSr) = ~1U;
    }

    void timerPwmStart(TimerId id, TimerChannel channel)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint32_t base = timerBase(id);
        std::uint8_t chIdx = static_cast<std::uint8_t>(channel);

        // Enable channel output
        reg(base + kCcer) |= (1U << (chIdx * 4));

        // Start counter
        reg(base + kCr1) |= kCr1Cen;
    }

    void timerPwmStop(TimerId id, TimerChannel channel)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint32_t base = timerBase(id);
        std::uint8_t chIdx = static_cast<std::uint8_t>(channel);

        // Disable channel output
        reg(base + kCcer) &= ~(1U << (chIdx * 4));
    }

    void timerPwmSetDuty(TimerId id, TimerChannel channel, std::uint32_t duty)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        reg(timerBase(id) + ccrOffset(channel)) = duty;
    }

    void timerDelayUs(std::uint32_t us)
    {
        if (us == 0)
        {
            return;
        }

        // Uses TIM7 as a free-running 1 MHz counter.
        // Caller must have already called rccEnableTimerClock(Tim7) and
        // timerInit with PSC = (timer_clock_MHz - 1), ARR = 0xFFFF.
        constexpr std::uint32_t kMaxChunk = 65000;
        std::uint32_t base = timerBase(TimerId::Tim7);

        while (us > 0)
        {
            std::uint32_t chunk = (us > kMaxChunk) ? kMaxChunk : us;
            reg(base + kCnt) = 0;

            // Start counter if not already running
            reg(base + kCr1) |= kCr1Cen;

            while (reg(base + kCnt) < chunk)
            {
                // Busy wait
            }

            us -= chunk;
        }
    }

    void rccEnableTimerClock(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint32_t saved = disableIrq();
        reg(kRccBase + kRccApb1enr) |= (1U << kTimerRccBit[static_cast<std::uint8_t>(id)]);
        restoreIrq(saved);
    }

    void rccDisableTimerClock(TimerId id)
    {
        if (!isValidTimerId(id))
        {
            return;
        }

        std::uint32_t saved = disableIrq();
        reg(kRccBase + kRccApb1enr) &= ~(1U << kTimerRccBit[static_cast<std::uint8_t>(id)]);
        restoreIrq(saved);
    }

}  // namespace hal

// ISR handlers
extern "C" void TIM2_IRQHandler() { handleTimerIrq(0); }
extern "C" void TIM3_IRQHandler() { handleTimerIrq(1); }
extern "C" void TIM4_IRQHandler() { handleTimerIrq(2); }
extern "C" void TIM5_IRQHandler() { handleTimerIrq(3); }
extern "C" void TIM6_DAC_IRQHandler() { handleTimerIrq(4); }
extern "C" void TIM7_IRQHandler() { handleTimerIrq(5); }
