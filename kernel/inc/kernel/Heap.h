// Kernel heap allocator: first-fit free-list with immediate coalescing.
//
// Provides variable-size allocation for user application code (operator new/delete
// route here via HeapOperators.cpp). Based on OSTEP Ch 17 free-space management.
//
// Thread safety: enterCritical/exitCritical around alloc/free.

#pragma once

#include <cstdint>

namespace kernel
{
    struct HeapStats
    {
        std::uint32_t totalSize;
        std::uint32_t usedSize;
        std::uint32_t freeSize;
        std::uint32_t highWatermark;      // maximum usedSize ever seen
        std::uint32_t allocCount;         // number of active allocations
        std::uint32_t largestFreeBlock;   // largest contiguous free block
    };

    // Initialize heap with a memory region [start, end).
    // start must be 8-byte aligned.
    void heapInit(void *start, void *end);

    // Allocate size bytes. Returns nullptr on failure or if size == 0.
    // Returned pointer is 8-byte aligned.
    void *heapAlloc(std::uint32_t size);

    // Free a previously allocated block. Nullptr is a no-op.
    void heapFree(void *ptr);

    // Get current heap statistics.
    HeapStats heapGetStats();

    // Reset heap to initial state. For testing only.
    void heapReset();

}  // namespace kernel
