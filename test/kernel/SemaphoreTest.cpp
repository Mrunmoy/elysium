#include <gtest/gtest.h>

#include "kernel/Semaphore.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"
#include "kernel/Arch.h"

#include "MockKernel.h"

namespace
{
    void dummyThread(void *arg)
    {
        (void)arg;
    }

    alignas(8) std::uint32_t g_stack1[128];
    alignas(8) std::uint32_t g_stack2[128];
    alignas(8) std::uint32_t g_stack3[128];
}  // namespace

class SemaphoreTest : public ::testing::Test
{
protected:
    kernel::Scheduler &m_scheduler = kernel::internal::scheduler();

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::semaphoreReset();
        m_scheduler.init();
        kernel::g_currentTcb = nullptr;
        kernel::g_nextTcb = nullptr;
    }

    kernel::ThreadId createThread(const char *name, std::uint32_t *stack,
                                  std::uint32_t stackSize,
                                  std::uint8_t priority = kernel::kDefaultPriority)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.priority = priority;

        return kernel::threadCreate(config);
    }

    void makeRunning(kernel::ThreadId id)
    {
        m_scheduler.addThread(id);
        m_scheduler.switchContext();
    }

    void forceCurrent(kernel::ThreadId id)
    {
        m_scheduler.setCurrentThread(id);
        kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
        if (tcb != nullptr)
        {
            tcb->state = kernel::ThreadState::Running;
        }
    }
};

// ---- Creation ----

TEST_F(SemaphoreTest, Create_ReturnsValidId)
{
    kernel::SemaphoreId id = kernel::semaphoreCreate(1, 1, "test-sem");
    EXPECT_NE(id, kernel::kInvalidSemaphoreId);
}

TEST_F(SemaphoreTest, Create_InitialCount)
{
    kernel::SemaphoreId id = kernel::semaphoreCreate(3, 5);
    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(id);
    ASSERT_NE(scb, nullptr);

    EXPECT_EQ(scb->count, 3u);
    EXPECT_EQ(scb->maxCount, 5u);
}

TEST_F(SemaphoreTest, Create_InvalidIfInitialExceedsMax)
{
    kernel::SemaphoreId id = kernel::semaphoreCreate(5, 3);
    EXPECT_EQ(id, kernel::kInvalidSemaphoreId);
}

TEST_F(SemaphoreTest, Create_MaxSemaphoresReturnsInvalid)
{
    for (std::uint8_t i = 0; i < kernel::kMaxSemaphores; ++i)
    {
        EXPECT_NE(kernel::semaphoreCreate(0, 1), kernel::kInvalidSemaphoreId);
    }
    EXPECT_EQ(kernel::semaphoreCreate(0, 1), kernel::kInvalidSemaphoreId);
}

// ---- Wait / Signal (no blocking) ----

TEST_F(SemaphoreTest, Wait_DecrementsCount)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::SemaphoreId sid = kernel::semaphoreCreate(3, 5);
    EXPECT_TRUE(kernel::semaphoreWait(sid));

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 2u);
}

TEST_F(SemaphoreTest, Signal_IncrementsCount)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(1, 5);
    EXPECT_TRUE(kernel::semaphoreSignal(sid));

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 2u);
}

TEST_F(SemaphoreTest, Signal_FailsAtMaxCount)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(5, 5);
    EXPECT_FALSE(kernel::semaphoreSignal(sid));

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 5u);
}

TEST_F(SemaphoreTest, BinarySemaphore_ToggleBehavior)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::SemaphoreId sid = kernel::semaphoreCreate(1, 1);

    // Wait: 1 -> 0
    EXPECT_TRUE(kernel::semaphoreWait(sid));
    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);

    // Signal: 0 -> 1
    EXPECT_TRUE(kernel::semaphoreSignal(sid));
    EXPECT_EQ(scb->count, 1u);

    // Signal again: already at max
    EXPECT_FALSE(kernel::semaphoreSignal(sid));
}

// ---- TryWait ----

TEST_F(SemaphoreTest, TryWait_SucceedsWhenAvailable)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(1, 1);
    EXPECT_TRUE(kernel::semaphoreTryWait(sid));

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);
}

TEST_F(SemaphoreTest, TryWait_FailsWhenZero)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(0, 1);
    EXPECT_FALSE(kernel::semaphoreTryWait(sid));
}

// ---- Blocking behavior ----

TEST_F(SemaphoreTest, Wait_BlocksWhenCountZero)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::SemaphoreId sid = kernel::semaphoreCreate(0, 1);

    // Wait should block (count is 0)
    kernel::semaphoreWait(sid);

    // Thread should be Blocked
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(tid);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Blocked);

    // Should be in wait queue
    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->waitCount, 1u);
    EXPECT_EQ(scb->waitHead, tid);
}

TEST_F(SemaphoreTest, Signal_WakesBlockedThread)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    // t1 blocks on empty semaphore
    makeRunning(t1);
    kernel::SemaphoreId sid = kernel::semaphoreCreate(0, 1);
    kernel::semaphoreWait(sid);

    // t1 is now blocked, t2 becomes current
    m_scheduler.addThread(t2);
    m_scheduler.switchContext();
    ASSERT_EQ(m_scheduler.currentThreadId(), t2);

    // t2 signals the semaphore -- should wake t1
    kernel::semaphoreSignal(sid);

    // t1 should be Ready (unblocked)
    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->state, kernel::ThreadState::Ready);

    // Count should still be 0 (consumed by waking t1)
    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);
    EXPECT_EQ(scb->waitCount, 0u);
}

TEST_F(SemaphoreTest, Signal_WakesHighestPriorityWaiter)
{
    kernel::ThreadId t1 = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId t2 = createThread("high", g_stack2, sizeof(g_stack2), 5);
    kernel::ThreadId t3 = createThread("signaler", g_stack3, sizeof(g_stack3), 10);

    kernel::SemaphoreId sid = kernel::semaphoreCreate(0, 1);

    // t1 (low priority) blocks on semaphore
    forceCurrent(t1);
    kernel::semaphoreWait(sid);

    // t2 (high priority) blocks on semaphore
    forceCurrent(t2);
    kernel::semaphoreWait(sid);

    // t3 signals -- should wake t2 (highest priority waiter)
    forceCurrent(t3);
    kernel::semaphoreSignal(sid);

    // t2 was woken and has higher priority than t3, so preemptive switchContext
    // made t2 Running (not just Ready)
    kernel::ThreadControlBlock *tcb2 = kernel::threadGetTcb(t2);
    EXPECT_NE(tcb2->state, kernel::ThreadState::Blocked);

    // t1 still blocked
    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->state, kernel::ThreadState::Blocked);

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->waitCount, 1u);
}

// ---- Counting semaphore ----

TEST_F(SemaphoreTest, CountingSemaphore_MultipleResources)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::SemaphoreId sid = kernel::semaphoreCreate(3, 3);

    // Acquire all three
    EXPECT_TRUE(kernel::semaphoreWait(sid));
    EXPECT_TRUE(kernel::semaphoreWait(sid));
    EXPECT_TRUE(kernel::semaphoreWait(sid));

    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 0u);

    // Release them back
    EXPECT_TRUE(kernel::semaphoreSignal(sid));
    EXPECT_TRUE(kernel::semaphoreSignal(sid));
    EXPECT_TRUE(kernel::semaphoreSignal(sid));
    EXPECT_EQ(scb->count, 3u);
}

// ---- ISR context rejection ----

TEST_F(SemaphoreTest, Wait_RejectsFromIsrContext)
{
    kernel::SemaphoreId sid = kernel::semaphoreCreate(3, 5);

    // Simulate ISR context
    test::g_isrContext = true;
    EXPECT_FALSE(kernel::semaphoreWait(sid));

    // Count should be unchanged
    kernel::SemaphoreControlBlock *scb = kernel::semaphoreGetBlock(sid);
    EXPECT_EQ(scb->count, 3u);
}
