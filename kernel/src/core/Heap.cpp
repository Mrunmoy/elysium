// Kernel heap allocator: first-fit free-list with immediate coalescing.
//
// Block header layout:
//   uint32_t size   -- total block size including header; bit 0 = allocated flag
//   BlockHeader *next -- next in free list (valid only when free)
//
// All sizes aligned to 8 bytes (AAPCS). Free list kept sorted by address
// for O(1) adjacency detection during coalescing. Sentinel end-block (size=0,
// allocated) prevents coalescing past heap end.

#include "kernel/Heap.h"
#include "kernel/Arch.h"

#include <cstdint>
#include <cstring>

namespace kernel
{
    namespace
    {
        static constexpr std::uint32_t kAlignment = 8;
        static constexpr std::uint32_t kAllocatedBit = 1u;

        struct BlockHeader
        {
            std::uint32_t size;     // includes header; bit 0 = allocated flag
            BlockHeader *next;      // next in free list (valid only when free)
        };

        static constexpr std::uint32_t kMinBlockSize =
            ((sizeof(BlockHeader) + kAlignment - 1) / kAlignment) * kAlignment;

        // Sentinel header marks start of free list
        BlockHeader s_freeListHead;

        // Heap region
        std::uint8_t *s_heapStart = nullptr;
        std::uint8_t *s_heapEnd = nullptr;

        // Statistics
        std::uint32_t s_totalSize = 0;
        std::uint32_t s_usedSize = 0;
        std::uint32_t s_highWatermark = 0;
        std::uint32_t s_allocCount = 0;

        std::uint32_t blockSize(const BlockHeader *hdr)
        {
            return hdr->size & ~kAllocatedBit;
        }

        bool isAllocated(const BlockHeader *hdr)
        {
            return (hdr->size & kAllocatedBit) != 0;
        }

        void markAllocated(BlockHeader *hdr)
        {
            hdr->size |= kAllocatedBit;
        }

        void markFree(BlockHeader *hdr)
        {
            hdr->size &= ~kAllocatedBit;
        }

        std::uint32_t alignUp(std::uint32_t val)
        {
            return (val + kAlignment - 1) & ~(kAlignment - 1);
        }

        // Insert a free block into the address-sorted free list
        void insertFreeBlock(BlockHeader *block)
        {
            BlockHeader *prev = &s_freeListHead;
            BlockHeader *curr = s_freeListHead.next;

            // Walk until we find the insertion point (sorted by address)
            while (curr != nullptr && curr < block)
            {
                prev = curr;
                curr = curr->next;
            }

            // Try to coalesce with the next block
            auto *blockEnd = reinterpret_cast<BlockHeader *>(
                reinterpret_cast<std::uint8_t *>(block) + blockSize(block));
            if (curr != nullptr && blockEnd == curr)
            {
                // Merge block with next
                block->size += blockSize(curr);
                block->next = curr->next;
            }
            else
            {
                block->next = curr;
            }

            // Try to coalesce with the previous block
            if (prev != &s_freeListHead)
            {
                auto *prevEnd = reinterpret_cast<BlockHeader *>(
                    reinterpret_cast<std::uint8_t *>(prev) + blockSize(prev));
                if (prevEnd == block)
                {
                    // Merge previous with block
                    prev->size += blockSize(block);
                    prev->next = block->next;
                    return;
                }
            }

            prev->next = block;
        }

        // Remove a free block from the free list
        void removeFreeBlock(BlockHeader *block, BlockHeader *prev)
        {
            prev->next = block->next;
        }

    }  // namespace

    void heapInit(void *start, void *end)
    {
        auto *startByte = static_cast<std::uint8_t *>(start);
        auto *endByte = static_cast<std::uint8_t *>(end);

        // Align start up (use uintptr_t mask to avoid 32-bit truncation on x86_64)
        auto startAddr = reinterpret_cast<std::uintptr_t>(startByte);
        startAddr = (startAddr + kAlignment - 1) & ~std::uintptr_t{kAlignment - 1};
        startByte = reinterpret_cast<std::uint8_t *>(startAddr);

        // Align end down to leave room for end sentinel
        auto endAddr = reinterpret_cast<std::uintptr_t>(endByte);
        endAddr = endAddr & ~std::uintptr_t{kAlignment - 1};
        endByte = reinterpret_cast<std::uint8_t *>(endAddr);

        s_heapStart = startByte;
        s_heapEnd = endByte;

        std::uint32_t heapSize = static_cast<std::uint32_t>(endByte - startByte);

        // Place end sentinel at the very end (one BlockHeader's worth)
        std::uint32_t sentinelSize = alignUp(static_cast<std::uint32_t>(sizeof(BlockHeader)));
        if (heapSize <= sentinelSize + kMinBlockSize)
        {
            // Heap too small
            s_totalSize = 0;
            s_usedSize = 0;
            s_highWatermark = 0;
            s_allocCount = 0;
            s_freeListHead.next = nullptr;
            return;
        }

        auto *sentinel = reinterpret_cast<BlockHeader *>(endByte - sentinelSize);
        sentinel->size = 0 | kAllocatedBit;  // size=0, allocated (prevents coalescing)
        sentinel->next = nullptr;

        // Create the initial free block covering the entire usable heap
        auto *firstBlock = reinterpret_cast<BlockHeader *>(startByte);
        std::uint32_t usableSize = heapSize - sentinelSize;
        firstBlock->size = usableSize;
        firstBlock->next = nullptr;

        s_freeListHead.size = 0;
        s_freeListHead.next = firstBlock;

        s_totalSize = usableSize;
        s_usedSize = 0;
        s_highWatermark = 0;
        s_allocCount = 0;
    }

    void *heapAlloc(std::uint32_t size)
    {
        if (size == 0)
        {
            return nullptr;
        }

        // Compute total block size: header + payload, aligned
        std::uint32_t totalSize = alignUp(
            static_cast<std::uint32_t>(sizeof(BlockHeader)) + size);
        if (totalSize < kMinBlockSize)
        {
            totalSize = kMinBlockSize;
        }

        arch::enterCritical();

        // First-fit: walk the free list
        BlockHeader *prev = &s_freeListHead;
        BlockHeader *curr = s_freeListHead.next;

        while (curr != nullptr)
        {
            std::uint32_t currSize = blockSize(curr);
            if (currSize >= totalSize)
            {
                // Found a fit -- split if remainder is large enough
                std::uint32_t remainder = currSize - totalSize;
                if (remainder >= kMinBlockSize)
                {
                    // Split: current block becomes totalSize, remainder becomes new free block
                    auto *newBlock = reinterpret_cast<BlockHeader *>(
                        reinterpret_cast<std::uint8_t *>(curr) + totalSize);
                    newBlock->size = remainder;
                    newBlock->next = curr->next;

                    curr->size = totalSize;
                    prev->next = newBlock;
                }
                else
                {
                    // Use entire block (no split)
                    totalSize = currSize;
                    removeFreeBlock(curr, prev);
                }

                markAllocated(curr);
                s_usedSize += totalSize;
                ++s_allocCount;
                if (s_usedSize > s_highWatermark)
                {
                    s_highWatermark = s_usedSize;
                }

                arch::exitCritical();

                // Return pointer past the header
                return reinterpret_cast<std::uint8_t *>(curr) + sizeof(BlockHeader);
            }

            prev = curr;
            curr = curr->next;
        }

        arch::exitCritical();
        return nullptr;
    }

    void heapFree(void *ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        // Validate pointer is within heap bounds
        auto *bytePtr = static_cast<std::uint8_t *>(ptr);
        if (bytePtr < s_heapStart + sizeof(BlockHeader) || bytePtr >= s_heapEnd)
        {
            return;
        }

        auto *hdr = reinterpret_cast<BlockHeader *>(
            bytePtr - sizeof(BlockHeader));

        // Validate block looks allocated
        if (!isAllocated(hdr))
        {
            return;
        }

        arch::enterCritical();

        std::uint32_t freedSize = blockSize(hdr);
        markFree(hdr);
        s_usedSize -= freedSize;
        --s_allocCount;

        // Insert back into the address-sorted free list (coalesces automatically)
        insertFreeBlock(hdr);

        arch::exitCritical();
    }

    HeapStats heapGetStats()
    {
        arch::enterCritical();

        HeapStats stats;
        stats.totalSize = s_totalSize;
        stats.usedSize = s_usedSize;
        stats.freeSize = s_totalSize - s_usedSize;
        stats.highWatermark = s_highWatermark;
        stats.allocCount = s_allocCount;

        // Find largest free block
        stats.largestFreeBlock = 0;
        BlockHeader *curr = s_freeListHead.next;
        while (curr != nullptr)
        {
            std::uint32_t sz = blockSize(curr);
            if (sz > stats.largestFreeBlock)
            {
                stats.largestFreeBlock = sz;
            }
            curr = curr->next;
        }

        arch::exitCritical();

        return stats;
    }

    void heapReset()
    {
        heapInit(s_heapStart, s_heapEnd);
    }

}  // namespace kernel
