// Unit tests for BlockPool fixed-size block allocator.

#include "kernel/BlockPool.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>

class BlockPoolTest : public ::testing::Test
{
protected:
    static constexpr std::uint32_t kBlockSize = 32;
    static constexpr std::uint32_t kBlockCount = 8;

    alignas(8) std::uint8_t m_buffer[kBlockSize * kBlockCount];
    kernel::BlockPool m_pool;

    void SetUp() override
    {
        m_pool.init(m_buffer, kBlockSize, kBlockCount);
    }
};

TEST_F(BlockPoolTest, Init_SetsCorrectStats)
{
    auto s = m_pool.stats();
    EXPECT_EQ(s.blockSize, kBlockSize);
    EXPECT_EQ(s.totalBlocks, kBlockCount);
    EXPECT_EQ(s.freeBlocks, kBlockCount);
    EXPECT_EQ(s.minFreeBlocks, kBlockCount);
}

TEST_F(BlockPoolTest, Init_BlockSizeRoundedUp)
{
    // Block size smaller than sizeof(void*) should be rounded up
    kernel::BlockPool pool;
    alignas(8) std::uint8_t buf[sizeof(void *) * 4];
    pool.init(buf, 1, 4);

    auto s = pool.stats();
    EXPECT_GE(s.blockSize, sizeof(void *));
    EXPECT_EQ(s.blockSize % sizeof(void *), 0u);
}

TEST_F(BlockPoolTest, Allocate_ReturnsNonNull)
{
    void *p = m_pool.allocate();
    EXPECT_NE(p, nullptr);
}

TEST_F(BlockPoolTest, Allocate_ReturnsDistinctBlocks)
{
    std::set<void *> blocks;
    for (std::uint32_t i = 0; i < kBlockCount; ++i)
    {
        void *p = m_pool.allocate();
        ASSERT_NE(p, nullptr);
        EXPECT_TRUE(blocks.insert(p).second) << "Duplicate block at allocation " << i;
    }
}

TEST_F(BlockPoolTest, Allocate_AllBlocksThenNull)
{
    for (std::uint32_t i = 0; i < kBlockCount; ++i)
    {
        EXPECT_NE(m_pool.allocate(), nullptr);
    }
    EXPECT_EQ(m_pool.allocate(), nullptr);
}

TEST_F(BlockPoolTest, Allocate_TracksMinFreeBlocks)
{
    m_pool.allocate();
    m_pool.allocate();
    auto s = m_pool.stats();
    EXPECT_EQ(s.freeBlocks, kBlockCount - 2);
    EXPECT_EQ(s.minFreeBlocks, kBlockCount - 2);

    // Free one back -- minFreeBlocks should not increase
    void *p = m_pool.allocate();
    m_pool.free(p);
    s = m_pool.stats();
    EXPECT_EQ(s.freeBlocks, kBlockCount - 2);
    EXPECT_EQ(s.minFreeBlocks, kBlockCount - 3);
}

TEST_F(BlockPoolTest, Free_ReturnsBlockToPool)
{
    void *p = m_pool.allocate();
    EXPECT_EQ(m_pool.stats().freeBlocks, kBlockCount - 1);
    EXPECT_TRUE(m_pool.free(p));
    EXPECT_EQ(m_pool.stats().freeBlocks, kBlockCount);
}

TEST_F(BlockPoolTest, Free_InvalidPointerReturnsFalse)
{
    EXPECT_FALSE(m_pool.free(nullptr));
}

TEST_F(BlockPoolTest, Free_OutOfBoundsReturnsFalse)
{
    std::uint8_t outsideBuf[32];
    EXPECT_FALSE(m_pool.free(outsideBuf));
}

TEST_F(BlockPoolTest, Free_MisalignedReturnsFalse)
{
    // Point to the middle of the first block (not on a block boundary)
    void *misaligned = m_buffer + 1;
    EXPECT_FALSE(m_pool.free(misaligned));
}

TEST_F(BlockPoolTest, AllocFreeAlloc_ReusesBlock)
{
    void *p1 = m_pool.allocate();
    m_pool.free(p1);
    void *p2 = m_pool.allocate();
    // Free pushes to head, allocate pops from head -- should get same block back
    EXPECT_EQ(p1, p2);
}

TEST_F(BlockPoolTest, FullCycle_AllocAllFreeAllAllocAll)
{
    void *blocks[kBlockCount];

    // Allocate all
    for (std::uint32_t i = 0; i < kBlockCount; ++i)
    {
        blocks[i] = m_pool.allocate();
        ASSERT_NE(blocks[i], nullptr);
    }
    EXPECT_EQ(m_pool.allocate(), nullptr);

    // Free all
    for (std::uint32_t i = 0; i < kBlockCount; ++i)
    {
        EXPECT_TRUE(m_pool.free(blocks[i]));
    }
    EXPECT_EQ(m_pool.stats().freeBlocks, kBlockCount);

    // Allocate all again
    for (std::uint32_t i = 0; i < kBlockCount; ++i)
    {
        EXPECT_NE(m_pool.allocate(), nullptr);
    }
    EXPECT_EQ(m_pool.allocate(), nullptr);
}

TEST_F(BlockPoolTest, Reset_RestoresInitialState)
{
    m_pool.allocate();
    m_pool.allocate();
    m_pool.allocate();
    m_pool.reset();

    auto s = m_pool.stats();
    EXPECT_EQ(s.freeBlocks, kBlockCount);
    EXPECT_EQ(s.minFreeBlocks, kBlockCount);
}

TEST_F(BlockPoolTest, Stats_ReflectsCurrentState)
{
    void *p1 = m_pool.allocate();
    void *p2 = m_pool.allocate();

    auto s = m_pool.stats();
    EXPECT_EQ(s.blockSize, kBlockSize);
    EXPECT_EQ(s.totalBlocks, kBlockCount);
    EXPECT_EQ(s.freeBlocks, kBlockCount - 2);

    m_pool.free(p1);
    s = m_pool.stats();
    EXPECT_EQ(s.freeBlocks, kBlockCount - 1);

    m_pool.free(p2);
    s = m_pool.stats();
    EXPECT_EQ(s.freeBlocks, kBlockCount);
}
