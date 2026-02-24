#pragma once

// ms-os version information.
//
// The version is set in the root CMakeLists.txt (project VERSION x.y.z)
// and injected via compile definitions. The constants below provide
// compile-time access to the version components and a formatted string.

#include <cstdint>

namespace kernel
{
namespace version
{
    static constexpr std::uint8_t kMajor = MSOS_VERSION_MAJOR;
    static constexpr std::uint8_t kMinor = MSOS_VERSION_MINOR;
    static constexpr std::uint8_t kPatch = MSOS_VERSION_PATCH;

// Stringify helper macros
#define MSOS_STRINGIFY_(x) #x
#define MSOS_STRINGIFY(x) MSOS_STRINGIFY_(x)

    static constexpr const char *kString =
        "ms-os v"
        MSOS_STRINGIFY(MSOS_VERSION_MAJOR) "."
        MSOS_STRINGIFY(MSOS_VERSION_MINOR) "."
        MSOS_STRINGIFY(MSOS_VERSION_PATCH);

#undef MSOS_STRINGIFY
#undef MSOS_STRINGIFY_

}  // namespace version
}  // namespace kernel
