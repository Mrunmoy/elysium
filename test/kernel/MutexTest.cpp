#include <gtest/gtest.h>

#include "kernel/Mutex.h"
#include "kernel/Scheduler.h"
#include "kernel/Thread.h"
#include "kernel/CortexM.h"

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

class MutexTest : public ::testing::Test
{
protected:
    kernel::Scheduler &m_scheduler = kernel::internal::scheduler();

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::mutexReset();
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

    // Make thread the current Running thread via scheduler
    void makeRunning(kernel::ThreadId id)
    {
        m_scheduler.addThread(id);
        m_scheduler.switchContext();
    }

    // Force a specific thread as current (bypasses priority selection).
    // Used in contention tests where we need to simulate multiple threads
    // taking turns regardless of priority.
    void forceCurrent(kernel::ThreadId id)
    {
        m_scheduler.setCurrentThread(id);
        kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
        if (tcb != nullptr)
        {
            tcb->m_state = kernel::ThreadState::Running;
        }
    }
};

// ---- Creation ----

TEST_F(MutexTest, Create_ReturnsValidId)
{
    kernel::MutexId id = kernel::mutexCreate("test-mtx");
    EXPECT_NE(id, kernel::kInvalidMutexId);
}

TEST_F(MutexTest, Create_InitiallyUnlocked)
{
    kernel::MutexId id = kernel::mutexCreate();
    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(id);
    ASSERT_NE(mcb, nullptr);

    EXPECT_EQ(mcb->m_owner, kernel::kInvalidThreadId);
    EXPECT_EQ(mcb->m_lockCount, 0u);
}

TEST_F(MutexTest, Create_MaxMutexesReturnsInvalid)
{
    for (std::uint8_t i = 0; i < kernel::kMaxMutexes; ++i)
    {
        EXPECT_NE(kernel::mutexCreate(), kernel::kInvalidMutexId);
    }
    EXPECT_EQ(kernel::mutexCreate(), kernel::kInvalidMutexId);
}

// ---- Lock / Unlock ----

TEST_F(MutexTest, Lock_AcquiresFreeMutex)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::MutexId mid = kernel::mutexCreate("mtx");
    EXPECT_TRUE(kernel::mutexLock(mid));

    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_owner, tid);
    EXPECT_EQ(mcb->m_lockCount, 1u);
}

TEST_F(MutexTest, Unlock_ReleasesOwnedMutex)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);
    EXPECT_TRUE(kernel::mutexUnlock(mid));

    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_owner, kernel::kInvalidThreadId);
    EXPECT_EQ(mcb->m_lockCount, 0u);
}

TEST_F(MutexTest, Unlock_FailsIfNotOwner)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    // t1 locks the mutex
    makeRunning(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // Switch to t2
    m_scheduler.addThread(t2);
    m_scheduler.switchContext();

    // t2 tries to unlock -- should fail
    EXPECT_FALSE(kernel::mutexUnlock(mid));

    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_owner, t1);
}

// ---- Recursive locking ----

TEST_F(MutexTest, RecursiveLock_IncrementsCount)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);
    kernel::mutexLock(mid);
    kernel::mutexLock(mid);

    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_lockCount, 3u);
    EXPECT_EQ(mcb->m_owner, tid);
}

TEST_F(MutexTest, RecursiveUnlock_DecrementsCount)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);
    kernel::mutexLock(mid);

    kernel::mutexUnlock(mid);
    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_lockCount, 1u);
    EXPECT_EQ(mcb->m_owner, tid);  // Still owned

    kernel::mutexUnlock(mid);
    EXPECT_EQ(mcb->m_lockCount, 0u);
    EXPECT_EQ(mcb->m_owner, kernel::kInvalidThreadId);  // Released
}

// ---- TryLock ----

TEST_F(MutexTest, TryLock_SucceedsWhenFree)
{
    kernel::ThreadId tid = createThread("t1", g_stack1, sizeof(g_stack1));
    makeRunning(tid);

    kernel::MutexId mid = kernel::mutexCreate("mtx");
    EXPECT_TRUE(kernel::mutexTryLock(mid));

    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_owner, tid);
}

TEST_F(MutexTest, TryLock_FailsWhenHeldByOther)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    makeRunning(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // Switch to t2
    m_scheduler.addThread(t2);
    m_scheduler.switchContext();

    EXPECT_FALSE(kernel::mutexTryLock(mid));
}

// ---- Contention and priority inheritance ----

TEST_F(MutexTest, Lock_BlocksAndAddsToWaitQueue)
{
    kernel::ThreadId t1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    // t1 locks the mutex
    makeRunning(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // Add t2 and switch to it
    m_scheduler.addThread(t2);
    m_scheduler.switchContext();
    ASSERT_EQ(m_scheduler.currentThreadId(), t2);

    // t2 tries to lock -- blocks
    kernel::mutexLock(mid);

    // Verify: t2 is in the wait queue
    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_waitCount, 1u);
    EXPECT_EQ(mcb->m_waitHead, t2);

    // t2's state should be Blocked
    kernel::ThreadControlBlock *tcb2 = kernel::threadGetTcb(t2);
    EXPECT_EQ(tcb2->m_state, kernel::ThreadState::Blocked);
}

TEST_F(MutexTest, PriorityInheritance_BoostsOwner)
{
    // t1 (low priority) holds mutex, t2 (high priority) blocks on it
    kernel::ThreadId t1 = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId t2 = createThread("high", g_stack2, sizeof(g_stack2), 5);

    // t1 locks
    makeRunning(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // Switch to t2
    m_scheduler.addThread(t2);
    m_scheduler.switchContext();
    ASSERT_EQ(m_scheduler.currentThreadId(), t2);

    // t2 blocks on the mutex -- should boost t1's priority
    kernel::mutexLock(mid);

    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->m_currentPriority, 5u);   // Boosted to t2's priority
    EXPECT_EQ(tcb1->m_basePriority, 20u);      // Base unchanged
}

TEST_F(MutexTest, PriorityInheritance_RestoredOnUnlock)
{
    kernel::ThreadId t1 = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId t2 = createThread("high", g_stack2, sizeof(g_stack2), 5);

    // t1 locks mutex
    forceCurrent(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // t2 blocks on mutex (boosts t1)
    forceCurrent(t2);
    kernel::mutexLock(mid);

    // t1 should be boosted
    kernel::ThreadControlBlock *tcb1 = kernel::threadGetTcb(t1);
    EXPECT_EQ(tcb1->m_currentPriority, 5u);

    // t1 unlocks
    forceCurrent(t1);
    kernel::mutexUnlock(mid);

    // t1's priority should be restored
    EXPECT_EQ(tcb1->m_currentPriority, 20u);
}

TEST_F(MutexTest, Unlock_WakesHighestPriorityWaiter)
{
    kernel::ThreadId t1 = createThread("owner", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId t2 = createThread("low-wait", g_stack2, sizeof(g_stack2), 20);
    kernel::ThreadId t3 = createThread("high-wait", g_stack3, sizeof(g_stack3), 5);

    // t1 locks
    forceCurrent(t1);
    kernel::MutexId mid = kernel::mutexCreate("mtx");
    kernel::mutexLock(mid);

    // t2 (priority 20) blocks on mutex
    forceCurrent(t2);
    kernel::mutexLock(mid);

    // t3 (priority 5) blocks on mutex -- also boosts t1
    forceCurrent(t3);
    kernel::mutexLock(mid);

    // t1 unlocks
    forceCurrent(t1);
    kernel::mutexUnlock(mid);

    // t3 (highest priority waiter) should now own the mutex
    kernel::MutexControlBlock *mcb = kernel::mutexGetBlock(mid);
    EXPECT_EQ(mcb->m_owner, t3);
    EXPECT_EQ(mcb->m_waitCount, 1u);  // t2 still waiting
}
