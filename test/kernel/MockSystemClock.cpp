// Host definitions for startup clock globals and linker heap symbols used by Kernel.cpp.

#include <cstdint>

extern "C"
{
    std::uint32_t SystemCoreClock = 120000000;
    std::uint32_t g_apb1Clock = 30000000;
    std::uint32_t g_apb2Clock = 60000000;

    // Minimal heap boundary symbols required for linking Kernel.cpp::init().
    // Integration tests here do not depend on heap capacity from these symbols.
    std::uint8_t _heap_start = 0;
    std::uint8_t _heap_end = 0;
}
