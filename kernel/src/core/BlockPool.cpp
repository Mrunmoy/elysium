// Fixed-size block pool allocator: embedded free-list, O(1) alloc/free.
//
// Each free block stores a void* to the next free block in its first bytes.
// allocate() pops head, free() pushes to head.

#include "kernel/BlockPool.h"

#include <cstdint>
#include <cstring>

namespace kernel
{
    void BlockPool::init(void *buffer, std::uint32_t blockSize, std::uint32_t blockCount)
    {
        // Minimum block size must hold a pointer for the free-list link
        if (blockSize < sizeof(void *))
        {
            blockSize = sizeof(void *);
        }

        // Round up to pointer alignment
        std::uint32_t align = static_cast<std::uint32_t>(sizeof(void *));
        blockSize = (blockSize + align - 1) & ~(align - 1);

        m_blockSize = blockSize;
        m_totalBlocks = blockCount;
        m_freeBlocks = blockCount;
        m_minFreeBlocks = blockCount;
        m_bufferStart = static_cast<std::uint8_t *>(buffer);
        m_bufferEnd = m_bufferStart + (blockSize * blockCount);

        // Build the free list: each block points to the next
        m_freeHead = nullptr;
        if (blockCount > 0)
        {
            m_freeHead = m_bufferStart;
            for (std::uint32_t i = 0; i < blockCount - 1; ++i)
            {
                std::uint8_t *current = m_bufferStart + (i * blockSize);
                std::uint8_t *next = current + blockSize;
                void *nextPtr = next;
                std::memcpy(current, &nextPtr, sizeof(void *));
            }
            // Last block points to nullptr
            std::uint8_t *last = m_bufferStart + ((blockCount - 1) * blockSize);
            void *null = nullptr;
            std::memcpy(last, &null, sizeof(void *));
        }
    }

    void *BlockPool::allocate()
    {
        if (m_freeHead == nullptr)
        {
            return nullptr;
        }

        // Pop head of free list
        void *block = m_freeHead;

        // Read the next pointer from the block
        void *next = nullptr;
        std::memcpy(&next, block, sizeof(void *));
        m_freeHead = next;

        --m_freeBlocks;
        if (m_freeBlocks < m_minFreeBlocks)
        {
            m_minFreeBlocks = m_freeBlocks;
        }

        return block;
    }

    bool BlockPool::free(void *ptr)
    {
        if (ptr == nullptr)
        {
            return false;
        }

        // Check bounds
        auto *bytePtr = static_cast<std::uint8_t *>(ptr);
        if (bytePtr < m_bufferStart || bytePtr >= m_bufferEnd)
        {
            return false;
        }

        // Check alignment to block boundary
        auto offset = static_cast<std::uint32_t>(bytePtr - m_bufferStart);
        if (offset % m_blockSize != 0)
        {
            return false;
        }

        // Push to head of free list
        std::memcpy(ptr, &m_freeHead, sizeof(void *));
        m_freeHead = ptr;
        ++m_freeBlocks;

        return true;
    }

    BlockPoolStats BlockPool::stats() const
    {
        BlockPoolStats s;
        s.blockSize = m_blockSize;
        s.totalBlocks = m_totalBlocks;
        s.freeBlocks = m_freeBlocks;
        s.minFreeBlocks = m_minFreeBlocks;
        return s;
    }

    void BlockPool::reset()
    {
        init(m_bufferStart, m_blockSize, m_totalBlocks);
    }

}  // namespace kernel
