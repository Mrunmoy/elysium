#include <gtest/gtest.h>

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

class SchedulerTest : public ::testing::Test
{
protected:
    kernel::Scheduler m_scheduler;

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        m_scheduler.init();
    }

    kernel::ThreadId createThread(const char *name, std::uint32_t *stack,
                                  std::uint32_t stackSize, std::uint32_t timeSlice = 0)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.timeSlice = timeSlice;

        return kernel::threadCreate(config);
    }
};

TEST_F(SchedulerTest, Init_StartsWithNoThreads)
{
    EXPECT_EQ(m_scheduler.readyCount(), 0u);
    EXPECT_EQ(m_scheduler.currentThreadId(), kernel::kInvalidThreadId);
}

TEST_F(SchedulerTest, AddThread_AppearsInReadyQueue)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1));
    ASSERT_NE(id, kernel::kInvalidThreadId);

    EXPECT_TRUE(m_scheduler.addThread(id));
    EXPECT_EQ(m_scheduler.readyCount(), 1u);
}

TEST_F(SchedulerTest, PickNext_RoundRobinTwoThreads)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);

    // First pick should return first thread added
    EXPECT_EQ(m_scheduler.pickNext(), id1);
}

TEST_F(SchedulerTest, PickNext_RoundRobinThreeThreads)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));
    kernel::ThreadId id3 = createThread("t3", g_stack3, sizeof(g_stack3));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);
    m_scheduler.addThread(id3);

    // Should return first in queue
    EXPECT_EQ(m_scheduler.pickNext(), id1);
}

TEST_F(SchedulerTest, Tick_DecrementsTimeSlice)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1));
    m_scheduler.addThread(id);
    m_scheduler.setCurrentThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    tcb->m_state = kernel::ThreadState::Running;
    std::uint32_t initial = tcb->m_timeSliceRemaining;

    m_scheduler.tick();

    EXPECT_EQ(tcb->m_timeSliceRemaining, initial - 1);
}

TEST_F(SchedulerTest, Tick_PendsContextSwitchOnExpiry)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1), 2);
    m_scheduler.addThread(id);
    m_scheduler.setCurrentThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    tcb->m_state = kernel::ThreadState::Running;

    // Tick once: remaining goes from 2 to 1
    bool switched = m_scheduler.tick();
    EXPECT_FALSE(switched);
    EXPECT_TRUE(test::g_contextSwitchTriggers.empty());

    // Tick again: remaining goes from 1 to 0, triggers switch
    switched = m_scheduler.tick();
    EXPECT_TRUE(switched);
    EXPECT_EQ(test::g_contextSwitchTriggers.size(), 1u);
}

TEST_F(SchedulerTest, TerminateThread_RemovesFromQueue)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);
    EXPECT_EQ(m_scheduler.readyCount(), 2u);

    m_scheduler.removeThread(id1);
    EXPECT_EQ(m_scheduler.readyCount(), 1u);

    // Next should be id2
    EXPECT_EQ(m_scheduler.pickNext(), id2);
}

TEST_F(SchedulerTest, IdleThread_RunsWhenQueueEmpty)
{
    // When queue is empty and no idle thread set, pickNext returns kInvalidThreadId
    EXPECT_EQ(m_scheduler.pickNext(), kernel::kInvalidThreadId);
}

TEST_F(SchedulerTest, Yield_TriggersContextSwitch)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1));
    m_scheduler.addThread(id);
    m_scheduler.setCurrentThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    tcb->m_state = kernel::ThreadState::Running;

    m_scheduler.yield();

    EXPECT_EQ(test::g_contextSwitchTriggers.size(), 1u);
}

TEST_F(SchedulerTest, Yield_ResetsTimeSlice)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    m_scheduler.addThread(id);
    m_scheduler.setCurrentThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    tcb->m_state = kernel::ThreadState::Running;

    // Consume some ticks
    m_scheduler.tick();
    m_scheduler.tick();

    ASSERT_NE(tcb, nullptr);
    EXPECT_EQ(tcb->m_timeSliceRemaining, 8u);

    // Yield resets time slice
    m_scheduler.yield();
    EXPECT_EQ(tcb->m_timeSliceRemaining, 10u);
}

TEST_F(SchedulerTest, TerminateThread_SetsStateInactive)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1));
    m_scheduler.addThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_EQ(tcb->m_state, kernel::ThreadState::Ready);

    m_scheduler.removeThread(id);
    EXPECT_EQ(tcb->m_state, kernel::ThreadState::Inactive);
}

TEST_F(SchedulerTest, SwitchContext_DequeuesNextThread)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);

    // No current thread (startup scenario)
    kernel::ThreadId nextId = m_scheduler.switchContext();
    EXPECT_EQ(nextId, id1);
    EXPECT_EQ(m_scheduler.currentThreadId(), id1);
    EXPECT_EQ(m_scheduler.readyCount(), 1u);  // id2 still in queue

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id1);
    EXPECT_EQ(tcb->m_state, kernel::ThreadState::Running);
}

TEST_F(SchedulerTest, SwitchContext_RotatesRunningToBack)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);

    // First switch: id1 becomes current
    m_scheduler.switchContext();
    EXPECT_EQ(m_scheduler.currentThreadId(), id1);

    // Second switch: id1 goes to back, id2 becomes current
    kernel::ThreadId nextId = m_scheduler.switchContext();
    EXPECT_EQ(nextId, id2);
    EXPECT_EQ(m_scheduler.currentThreadId(), id2);

    // id1 should be back in queue
    EXPECT_EQ(m_scheduler.readyCount(), 1u);
    EXPECT_EQ(m_scheduler.pickNext(), id1);
}

TEST_F(SchedulerTest, SwitchContext_ThreeThreadRoundRobin)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1));
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2));
    kernel::ThreadId id3 = createThread("t3", g_stack3, sizeof(g_stack3));

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);
    m_scheduler.addThread(id3);

    // Round 1: id1
    EXPECT_EQ(m_scheduler.switchContext(), id1);
    // Round 2: id2 (id1 goes to back)
    EXPECT_EQ(m_scheduler.switchContext(), id2);
    // Round 3: id3 (id2 goes to back)
    EXPECT_EQ(m_scheduler.switchContext(), id3);
    // Round 4: id1 again (full rotation)
    EXPECT_EQ(m_scheduler.switchContext(), id1);
}

TEST_F(SchedulerTest, Tick_ReturnsFalseWhenNoSwitch)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    m_scheduler.addThread(id);
    m_scheduler.setCurrentThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    tcb->m_state = kernel::ThreadState::Running;

    EXPECT_FALSE(m_scheduler.tick());
}
