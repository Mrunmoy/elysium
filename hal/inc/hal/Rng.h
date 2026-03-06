#pragma once

#include <cstdint>

namespace hal
{
    std::int32_t rngInit();
    std::int32_t rngRead(std::uint32_t &value);
    void rngDeinit();

}  // namespace hal
