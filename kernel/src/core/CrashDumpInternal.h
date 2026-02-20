// Internal crash dump output helpers shared between CrashDumpCommon.cpp
// and arch-specific CrashDumpArch.cpp files.
//
// These are NOT part of the public kernel API.

#pragma once

#include <cstdint>

namespace kernel
{
    void faultPrint(const char *str);
    void faultPrintHex(std::uint32_t value);

}  // namespace kernel
