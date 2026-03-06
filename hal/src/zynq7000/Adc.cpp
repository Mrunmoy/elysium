// Zynq-7000 ADC stub
//
// XADC integration is not part of the current PYNQ-Z2 port.

#include "hal/Adc.h"

namespace hal
{
    void adcInit(const AdcConfig & /* config */) {}

    std::int32_t adcRead(AdcId /* id */, std::uint8_t /* channel */, std::uint16_t * /* outValue */,
                         std::uint32_t /* timeoutLoops */)
    {
        return msos::error::kNoSys;
    }
}  // namespace hal
