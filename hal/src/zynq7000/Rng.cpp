// Zynq-7000 RNG stub.
//
// The Zynq PS does not have a hardware random number generator.
// This stub satisfies the linker for cross-compilation.

#include "hal/Rng.h"

#include "msos/ErrorCode.h"

namespace hal
{
    std::int32_t rngInit()
    {
        return msos::error::kNoSys;
    }

    std::int32_t rngRead(std::uint32_t & /* value */)
    {
        return msos::error::kNoSys;
    }

    void rngDeinit()
    {
        // No-op on Zynq
    }
}  // namespace hal
