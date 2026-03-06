#include "hal/Adc.h"

#include "hal/Rcc.h"

#include <cstdint>

namespace
{
    constexpr std::uint32_t kAdc1Base = 0x40012000;
    constexpr std::uint32_t kAdc2Base = 0x40012100;
    constexpr std::uint32_t kAdc3Base = 0x40012200;
    constexpr std::uint32_t kAdcCount = 3;
    constexpr std::uint8_t kMaxChannel = 18;

    // ADC common registers (shared across ADC1/2/3).
    constexpr std::uint32_t kAdcCommonBase = 0x40012300;
    constexpr std::uint32_t kCcr = 0x04;

    // Register offsets.
    constexpr std::uint32_t kSr = 0x00;
    constexpr std::uint32_t kCr1 = 0x04;
    constexpr std::uint32_t kCr2 = 0x08;
    constexpr std::uint32_t kSmpr1 = 0x0C;
    constexpr std::uint32_t kSmpr2 = 0x10;
    constexpr std::uint32_t kSqr1 = 0x2C;
    constexpr std::uint32_t kSqr3 = 0x34;
    constexpr std::uint32_t kDr = 0x4C;

    // Bit positions.
    constexpr std::uint32_t kSrEoc = 1;
    constexpr std::uint32_t kCr1Res = 24;      // [25:24]
    constexpr std::uint32_t kCr2Adon = 0;
    constexpr std::uint32_t kCr2Align = 11;
    constexpr std::uint32_t kCr2Swstart = 30;
    constexpr std::uint32_t kSqr1L = 20;       // [23:20]
    constexpr std::uint32_t kCcrAdcpre = 16;   // [17:16]

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    bool isValidAdcId(hal::AdcId id)
    {
        return static_cast<std::uint8_t>(id) < kAdcCount;
    }

    std::uint32_t adcBase(hal::AdcId id)
    {
        switch (id)
        {
            case hal::AdcId::Adc1:
                return kAdc1Base;
            case hal::AdcId::Adc2:
                return kAdc2Base;
            case hal::AdcId::Adc3:
                return kAdc3Base;
            default:
                return kAdc1Base;
        }
    }

    void setSampleTime(std::uint32_t base, std::uint8_t channel, hal::AdcSampleTime sampleTime)
    {
        std::uint32_t sample = static_cast<std::uint32_t>(sampleTime) & 0x7U;
        if (channel <= 9U)
        {
            std::uint32_t shift = static_cast<std::uint32_t>(channel) * 3U;
            std::uint32_t smpr2 = reg(base + kSmpr2);
            smpr2 &= ~(0x7U << shift);
            smpr2 |= (sample << shift);
            reg(base + kSmpr2) = smpr2;
            return;
        }

        std::uint32_t shift = static_cast<std::uint32_t>(channel - 10U) * 3U;
        std::uint32_t smpr1 = reg(base + kSmpr1);
        smpr1 &= ~(0x7U << shift);
        smpr1 |= (sample << shift);
        reg(base + kSmpr1) = smpr1;
    }

    hal::AdcSampleTime s_sampleTime[3] = {
        hal::AdcSampleTime::Cycles84,
        hal::AdcSampleTime::Cycles84,
        hal::AdcSampleTime::Cycles84,
    };
}  // namespace

namespace hal
{
    void adcInit(const AdcConfig &config)
    {
        if (!isValidAdcId(config.id))
        {
            return;
        }

        rccEnableAdcClock(config.id);
        std::uint32_t base = adcBase(config.id);

        // Disable ADC before configuration.
        reg(base + kCr2) &= ~(1U << kCr2Adon);

        // Resolution.
        std::uint32_t cr1 = reg(base + kCr1);
        cr1 &= ~(0x3U << kCr1Res);
        cr1 |= (static_cast<std::uint32_t>(config.resolution) << kCr1Res);
        reg(base + kCr1) = cr1;

        // Alignment.
        std::uint32_t cr2 = reg(base + kCr2);
        if (config.align == AdcAlign::Left)
        {
            cr2 |= (1U << kCr2Align);
        }
        else
        {
            cr2 &= ~(1U << kCr2Align);
        }

        // Software trigger path for regular conversions.
        cr2 |= (1U << kCr2Adon);
        reg(base + kCr2) = cr2;

        // Common prescaler: PCLK2/4 for conservative timing.
        std::uint32_t ccr = reg(kAdcCommonBase + kCcr);
        ccr &= ~(0x3U << kCcrAdcpre);
        ccr |= (0x1U << kCcrAdcpre);
        reg(kAdcCommonBase + kCcr) = ccr;

        s_sampleTime[static_cast<std::uint8_t>(config.id)] = config.sampleTime;
    }

    std::int32_t adcRead(AdcId id, std::uint8_t channel, std::uint16_t *outValue,
                         std::uint32_t timeoutLoops)
    {
        if (!isValidAdcId(id) || outValue == nullptr || channel > kMaxChannel || timeoutLoops == 0)
        {
            return msos::error::kInvalid;
        }

        std::uint32_t base = adcBase(id);
        setSampleTime(base, channel, s_sampleTime[static_cast<std::uint8_t>(id)]);

        // One conversion in regular sequence.
        std::uint32_t sqr1 = reg(base + kSqr1);
        sqr1 &= ~(0xFU << kSqr1L);
        reg(base + kSqr1) = sqr1;
        reg(base + kSqr3) = static_cast<std::uint32_t>(channel);

        // Clear EOC and start conversion.
        reg(base + kSr) &= ~(1U << kSrEoc);
        reg(base + kCr2) |= (1U << kCr2Swstart);

        for (std::uint32_t i = 0; i < timeoutLoops; ++i)
        {
            if (reg(base + kSr) & (1U << kSrEoc))
            {
                *outValue = static_cast<std::uint16_t>(reg(base + kDr) & 0xFFFFU);
                return msos::error::kOk;
            }
        }

        return msos::error::kTimedOut;
    }
}  // namespace hal
