// Host definitions for startup clock globals and linker heap symbols used by Kernel.cpp.
//
// On the real target, _heap_start and _heap_end are linker-script symbols
// with guaranteed ordering (&_heap_end > &_heap_start).
//
// Two independent globals have no ordering guarantee on the host -- the
// linker may reorder them, causing heapInit() to compute a negative size
// that wraps to a huge uint32_t and corrupts memory.
//
// Fix: place both in the same linker section so they remain adjacent in
// declaration order, guaranteeing &_heap_end > &_heap_start.

#include <cstdint>

extern "C"
{
    std::uint32_t SystemCoreClock = 120000000;
    std::uint32_t g_apb1Clock = 30000000;
    std::uint32_t g_apb2Clock = 60000000;
}

// Kernel.cpp declares: extern "C" std::uint8_t _heap_start, _heap_end;
// and calls heapInit(&_heap_start, &_heap_end).  The 1-byte span is below
// the minimum heap block size, so heapInit() safely skips initialization.
extern "C"
{
    std::uint8_t _heap_start __attribute__((section(".heap_mock")));
    std::uint8_t _heap_end   __attribute__((section(".heap_mock")));
}
