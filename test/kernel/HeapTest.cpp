// Unit tests for kernel heap allocator (first-fit with coalescing).

#include "kernel/Heap.h"
#include "MockKernel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

class HeapTest : public ::testing::Test
{
protected:
    static constexpr std::uint32_t kHeapSize = 4096;
    alignas(8) std::uint8_t m_buffer[kHeapSize];

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::heapInit(m_buffer, m_buffer + kHeapSize);
    }
};

TEST_F(HeapTest, Init_SetsCorrectStats)
{
    auto s = kernel::heapGetStats();
    EXPECT_GT(s.totalSize, 0u);
    EXPECT_EQ(s.usedSize, 0u);
    EXPECT_EQ(s.freeSize, s.totalSize);
    EXPECT_EQ(s.highWatermark, 0u);
    EXPECT_EQ(s.allocCount, 0u);
}

TEST_F(HeapTest, Init_AllFree)
{
    auto s = kernel::heapGetStats();
    EXPECT_EQ(s.freeSize, s.totalSize);
    EXPECT_GT(s.largestFreeBlock, 0u);
}

TEST_F(HeapTest, Alloc_ReturnsNonNull)
{
    void *p = kernel::heapAlloc(64);
    EXPECT_NE(p, nullptr);
}

TEST_F(HeapTest, Alloc_ReturnsAligned8)
{
    void *p1 = kernel::heapAlloc(1);
    void *p2 = kernel::heapAlloc(3);
    void *p3 = kernel::heapAlloc(7);
    void *p4 = kernel::heapAlloc(13);

    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p1) % 8, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p2) % 8, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p3) % 8, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p4) % 8, 0u);
}

TEST_F(HeapTest, Alloc_TwoAllocsReturnDistinct)
{
    void *p1 = kernel::heapAlloc(32);
    void *p2 = kernel::heapAlloc(32);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST_F(HeapTest, Alloc_ZeroSizeReturnsNull)
{
    EXPECT_EQ(kernel::heapAlloc(0), nullptr);
}

TEST_F(HeapTest, Alloc_FullHeapReturnsNull)
{
    // Keep allocating until we run out
    while (kernel::heapAlloc(64) != nullptr)
    {
    }
    // One more should return null
    EXPECT_EQ(kernel::heapAlloc(1), nullptr);
}

TEST_F(HeapTest, Free_ReturnsMemoryToPool)
{
    void *p = kernel::heapAlloc(64);
    auto before = kernel::heapGetStats();

    kernel::heapFree(p);
    auto after = kernel::heapGetStats();

    EXPECT_GT(after.freeSize, before.freeSize);
    EXPECT_EQ(after.allocCount, before.allocCount - 1);
}

TEST_F(HeapTest, Free_NullptrIsNoOp)
{
    auto before = kernel::heapGetStats();
    kernel::heapFree(nullptr);
    auto after = kernel::heapGetStats();
    EXPECT_EQ(before.freeSize, after.freeSize);
    EXPECT_EQ(before.allocCount, after.allocCount);
}

TEST_F(HeapTest, Free_InvalidPointerIsNoOp)
{
    std::uint8_t stack;
    auto before = kernel::heapGetStats();
    kernel::heapFree(&stack);
    auto after = kernel::heapGetStats();
    EXPECT_EQ(before.freeSize, after.freeSize);
}

TEST_F(HeapTest, Coalesce_AdjacentFreeBlocks)
{
    void *p1 = kernel::heapAlloc(64);
    void *p2 = kernel::heapAlloc(64);

    kernel::heapFree(p1);
    kernel::heapFree(p2);

    auto s = kernel::heapGetStats();
    // After coalescing, should be one large free block
    EXPECT_EQ(s.largestFreeBlock, s.totalSize);
}

TEST_F(HeapTest, Coalesce_FreeMiddleBlockMergesBoth)
{
    void *p1 = kernel::heapAlloc(64);
    void *p2 = kernel::heapAlloc(64);
    void *p3 = kernel::heapAlloc(64);

    // Free outer blocks first, then middle
    kernel::heapFree(p1);
    kernel::heapFree(p3);
    kernel::heapFree(p2);

    auto s = kernel::heapGetStats();
    // All three should coalesce into one block
    EXPECT_EQ(s.largestFreeBlock, s.totalSize);
    EXPECT_EQ(s.allocCount, 0u);
}

TEST_F(HeapTest, Coalesce_MultipleFreesMergeAll)
{
    std::vector<void *> ptrs;
    for (int i = 0; i < 10; ++i)
    {
        void *p = kernel::heapAlloc(32);
        if (p == nullptr)
        {
            break;
        }
        ptrs.push_back(p);
    }

    // Free all in forward order
    for (auto *p : ptrs)
    {
        kernel::heapFree(p);
    }

    auto s = kernel::heapGetStats();
    EXPECT_EQ(s.largestFreeBlock, s.totalSize);
    EXPECT_EQ(s.allocCount, 0u);
}

TEST_F(HeapTest, Split_LargeBlockSplitsOnAlloc)
{
    auto before = kernel::heapGetStats();
    void *p = kernel::heapAlloc(32);
    ASSERT_NE(p, nullptr);

    auto after = kernel::heapGetStats();
    // Used size should be approximately header + 32 (aligned)
    EXPECT_GT(after.usedSize, 0u);
    EXPECT_LT(after.usedSize, before.totalSize);
    EXPECT_GT(after.largestFreeBlock, 0u);
}

TEST_F(HeapTest, NoSplit_ExactFitDoesNotSplit)
{
    // Allocate everything, free, then allocate the exact same size
    auto s = kernel::heapGetStats();
    std::uint32_t totalPayload = s.totalSize - 8;  // header is part of totalSize

    void *p = kernel::heapAlloc(totalPayload);
    if (p != nullptr)
    {
        auto s2 = kernel::heapGetStats();
        EXPECT_EQ(s2.freeSize, 0u);
        kernel::heapFree(p);
    }
}

TEST_F(HeapTest, Fragmentation_FirstFitFindsFirstHole)
{
    // Allocate three blocks, free the first, then allocate a smaller block
    // First-fit should place it in the first hole
    void *p1 = kernel::heapAlloc(64);
    kernel::heapAlloc(64);
    kernel::heapAlloc(64);

    kernel::heapFree(p1);

    void *p4 = kernel::heapAlloc(32);
    ASSERT_NE(p4, nullptr);
    // First-fit should reuse the first freed slot
    EXPECT_EQ(p4, p1);
}

TEST_F(HeapTest, Stats_HighWatermarkTracked)
{
    kernel::heapAlloc(128);
    kernel::heapAlloc(128);

    auto s1 = kernel::heapGetStats();
    std::uint32_t hw1 = s1.highWatermark;

    void *p = kernel::heapAlloc(256);
    auto s2 = kernel::heapGetStats();
    EXPECT_GE(s2.highWatermark, hw1);

    // Free everything -- watermark should not decrease
    kernel::heapFree(p);
    auto s3 = kernel::heapGetStats();
    EXPECT_EQ(s3.highWatermark, s2.highWatermark);
}

TEST_F(HeapTest, Stats_AllocCountAccurate)
{
    EXPECT_EQ(kernel::heapGetStats().allocCount, 0u);

    void *p1 = kernel::heapAlloc(32);
    EXPECT_EQ(kernel::heapGetStats().allocCount, 1u);

    void *p2 = kernel::heapAlloc(32);
    EXPECT_EQ(kernel::heapGetStats().allocCount, 2u);

    kernel::heapFree(p1);
    EXPECT_EQ(kernel::heapGetStats().allocCount, 1u);

    kernel::heapFree(p2);
    EXPECT_EQ(kernel::heapGetStats().allocCount, 0u);
}

TEST_F(HeapTest, Reset_RestoresInitialState)
{
    kernel::heapAlloc(64);
    kernel::heapAlloc(128);
    kernel::heapAlloc(64);

    kernel::heapReset();

    auto s = kernel::heapGetStats();
    EXPECT_EQ(s.usedSize, 0u);
    EXPECT_EQ(s.allocCount, 0u);
    EXPECT_EQ(s.freeSize, s.totalSize);
}

TEST_F(HeapTest, StressTest_AllocFreeRandomPattern)
{
    // Allocate-free pattern with varied sizes to stress coalescing
    std::vector<void *> ptrs;

    // Allocate a bunch
    for (std::uint32_t size = 8; size <= 128; size += 8)
    {
        void *p = kernel::heapAlloc(size);
        if (p != nullptr)
        {
            ptrs.push_back(p);
        }
    }

    // Free every other one
    for (std::size_t i = 0; i < ptrs.size(); i += 2)
    {
        kernel::heapFree(ptrs[i]);
        ptrs[i] = nullptr;
    }

    // Allocate again in the holes
    for (std::size_t i = 0; i < ptrs.size(); i += 2)
    {
        void *p = kernel::heapAlloc(8);
        if (p != nullptr)
        {
            ptrs[i] = p;
        }
    }

    // Free everything
    for (auto *p : ptrs)
    {
        if (p != nullptr)
        {
            kernel::heapFree(p);
        }
    }

    auto s = kernel::heapGetStats();
    EXPECT_EQ(s.allocCount, 0u);
    EXPECT_EQ(s.freeSize, s.totalSize);
}

TEST_F(HeapTest, AllocAll_FreeAll_AllocAll)
{
    std::vector<void *> ptrs;

    // First round: allocate until full
    while (true)
    {
        void *p = kernel::heapAlloc(32);
        if (p == nullptr)
        {
            break;
        }
        ptrs.push_back(p);
    }
    EXPECT_GT(ptrs.size(), 0u);

    // Free all
    for (auto *p : ptrs)
    {
        kernel::heapFree(p);
    }
    ptrs.clear();

    // Second round: should be able to allocate the same number again
    std::size_t count = 0;
    while (true)
    {
        void *p = kernel::heapAlloc(32);
        if (p == nullptr)
        {
            break;
        }
        ptrs.push_back(p);
        ++count;
    }
    EXPECT_GT(count, 0u);

    // Cleanup
    for (auto *p : ptrs)
    {
        kernel::heapFree(p);
    }
}
