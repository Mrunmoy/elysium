// Fixed-size block pool allocator for kernel-internal use.
//
// O(1) allocate and free using an embedded free-list (each free block stores
// a pointer to the next free block in its first bytes).
//
// Thread safety: none built-in. Callers must use arch::enterCritical() /
// arch::exitCritical() around allocate/free when accessed from multiple
// threads or ISRs.

#pragma once

#include <cstdint>

namespace kernel
{
    struct BlockPoolStats
    {
        std::uint32_t blockSize;
        std::uint32_t totalBlocks;
        std::uint32_t freeBlocks;
        std::uint32_t minFreeBlocks;   // watermark (lowest free count ever seen)
    };

    class BlockPool
    {
    public:
        // Initialize the pool with an external buffer.
        // blockSize is rounded up to at least sizeof(void*) and aligned to sizeof(void*).
        void init(void *buffer, std::uint32_t blockSize, std::uint32_t blockCount);

        // Allocate one block. Returns nullptr if pool is exhausted.
        void *allocate();

        // Free a previously allocated block. Returns false if ptr is invalid
        // (null, out of bounds, or misaligned).
        bool free(void *ptr);

        // Get current pool statistics.
        BlockPoolStats stats() const;

        // Reset pool to initial state (all blocks free). For testing only.
        void reset();

    private:
        void *m_freeHead;
        std::uint8_t *m_bufferStart;
        std::uint8_t *m_bufferEnd;
        std::uint32_t m_blockSize;     // aligned to sizeof(void*)
        std::uint32_t m_totalBlocks;
        std::uint32_t m_freeBlocks;
        std::uint32_t m_minFreeBlocks;
    };

}  // namespace kernel
