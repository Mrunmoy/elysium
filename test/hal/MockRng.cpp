// Mock RNG implementation for host-side testing.
// Replaces hal/src/stm32f4/Rng.cpp at link time.

#include "hal/Rng.h"

#include "MockRegisters.h"

namespace hal
{
    std::int32_t rngInit()
    {
        ++test::g_rngInitCount;
        return test::g_rngInitReturnCode;
    }

    std::int32_t rngRead(std::uint32_t &value)
    {
        ++test::g_rngReadCount;

        if (test::g_rngReadReturnCode != 0)
        {
            return test::g_rngReadReturnCode;
        }

        if (test::g_rngReadPos < test::g_rngValues.size())
        {
            value = test::g_rngValues[test::g_rngReadPos++];
        }
        else
        {
            value = 0;
        }

        return 0;  // kOk
    }

    void rngDeinit()
    {
        ++test::g_rngDeinitCount;
    }
}  // namespace hal
