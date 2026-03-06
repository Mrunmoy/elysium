// Mock ADC implementation for host-side testing.
// Replaces hal/src/stm32f4/Adc.cpp at link time.

#include "hal/Adc.h"

#include "MockRegisters.h"

namespace hal
{
    namespace
    {
        constexpr std::uint8_t kAdcCount = 3;

        bool isValidAdcId(AdcId id)
        {
            return static_cast<std::uint8_t>(id) < kAdcCount;
        }
    }  // namespace

    void adcInit(const AdcConfig &config)
    {
        if (!isValidAdcId(config.id))
        {
            return;
        }

        test::g_adcInitCalls.push_back({
            static_cast<std::uint8_t>(config.id),
            static_cast<std::uint8_t>(config.resolution),
            static_cast<std::uint8_t>(config.align),
            static_cast<std::uint8_t>(config.sampleTime),
        });
    }

    std::int32_t adcRead(AdcId id, std::uint8_t channel, std::uint16_t *outValue,
                         std::uint32_t timeoutLoops)
    {
        if (!isValidAdcId(id) || outValue == nullptr || channel > 18U || timeoutLoops == 0)
        {
            return msos::error::kInvalid;
        }

        test::g_adcReadCalls.push_back({
            static_cast<std::uint8_t>(id),
            channel,
            timeoutLoops,
        });

        *outValue = test::g_adcReadValue;
        return test::g_adcReadStatus;
    }
}  // namespace hal
