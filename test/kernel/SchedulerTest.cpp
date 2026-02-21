#include <gtest/gtest.h>

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
    // g_stack4 available for future tests
    // alignas(8) std::uint32_t g_stack4[128];
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

    // Helper: create a thread with explicit priority
    kernel::ThreadId createThread(const char *name, std::uint32_t *stack,
                                  std::uint32_t stackSize,
                                  std::uint8_t priority = kernel::kDefaultPriority,
                                  std::uint32_t timeSlice = 0)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = name;
        config.stack = stack;
        config.stackSize = stackSize;
        config.priority = priority;
        config.timeSlice = timeSlice;

        return kernel::threadCreate(config);
    }

    // Helper: add thread and make it the running current via switchContext
    kernel::ThreadId createAndRun(const char *name, std::uint32_t *stack,
                                  std::uint32_t stackSize,
                                  std::uint8_t priority = kernel::kDefaultPriority,
                                  std::uint32_t timeSlice = 0)
    {
        kernel::ThreadId id = createThread(name, stack, stackSize, priority, timeSlice);
        m_scheduler.addThread(id);
        m_scheduler.switchContext();
        return id;
    }
};

// ---- Basic state ----

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

// ---- Priority selection ----

TEST_F(SchedulerTest, PickNext_HighestPriorityThread)
{
    // Create three threads at different priorities
    kernel::ThreadId low = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId high = createThread("high", g_stack2, sizeof(g_stack2), 5);
    kernel::ThreadId mid = createThread("mid", g_stack3, sizeof(g_stack3), 10);

    m_scheduler.addThread(low);
    m_scheduler.addThread(high);
    m_scheduler.addThread(mid);

    // pickNext should return the highest-priority (lowest number) thread
    EXPECT_EQ(m_scheduler.pickNext(), high);
}

TEST_F(SchedulerTest, PickNext_SamePriorityFIFO)
{
    // Same priority: first added should be picked first
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);

    EXPECT_EQ(m_scheduler.pickNext(), id1);
}

TEST_F(SchedulerTest, SwitchContext_DequeuesHighestPriority)
{
    kernel::ThreadId low = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId high = createThread("high", g_stack2, sizeof(g_stack2), 5);

    m_scheduler.addThread(low);
    m_scheduler.addThread(high);

    // switchContext should pick the highest priority thread
    kernel::ThreadId nextId = m_scheduler.switchContext();
    EXPECT_EQ(nextId, high);
    EXPECT_EQ(m_scheduler.currentThreadId(), high);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(high);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Running);
}

TEST_F(SchedulerTest, SwitchContext_SamePriorityRoundRobin)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);
    kernel::ThreadId id3 = createThread("t3", g_stack3, sizeof(g_stack3), 10);

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);
    m_scheduler.addThread(id3);

    // Round 1: id1 (first in FIFO at priority 10)
    EXPECT_EQ(m_scheduler.switchContext(), id1);
    // Round 2: id2 (id1 goes to back of priority-10 list)
    EXPECT_EQ(m_scheduler.switchContext(), id2);
    // Round 3: id3
    EXPECT_EQ(m_scheduler.switchContext(), id3);
    // Round 4: id1 again (full rotation)
    EXPECT_EQ(m_scheduler.switchContext(), id1);
}

TEST_F(SchedulerTest, SwitchContext_HighPriorityAlwaysRunsFirst)
{
    kernel::ThreadId low = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId high = createThread("high", g_stack2, sizeof(g_stack2), 5);

    m_scheduler.addThread(low);
    m_scheduler.addThread(high);

    // high runs first
    EXPECT_EQ(m_scheduler.switchContext(), high);
    // high goes to back of priority-5 list, but it's still higher than low
    EXPECT_EQ(m_scheduler.switchContext(), high);
    // low never runs while high is ready
    EXPECT_EQ(m_scheduler.currentThreadId(), high);
}

// ---- Time-slicing ----

TEST_F(SchedulerTest, Tick_DecrementsTimeSlice)
{
    kernel::ThreadId id = createAndRun("t1", g_stack1, sizeof(g_stack1),
                                       kernel::kDefaultPriority, 10);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    std::uint32_t initial = tcb->timeSliceRemaining;

    m_scheduler.tick();

    EXPECT_EQ(tcb->timeSliceRemaining, initial - 1);
}

TEST_F(SchedulerTest, Tick_SamePriorityTimeSliceExpiry)
{
    // Create two threads at the same priority
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1),
                                        kernel::kDefaultPriority, 2);
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2),
                                        kernel::kDefaultPriority, 2);

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);

    // Make id1 current via switchContext (dequeues id1)
    m_scheduler.switchContext();
    ASSERT_EQ(m_scheduler.currentThreadId(), id1);

    // Tick once: remaining goes from 2 to 1, no switch
    bool switched = m_scheduler.tick();
    EXPECT_FALSE(switched);

    // Tick again: remaining goes from 1 to 0; id2 is a same-priority peer
    switched = m_scheduler.tick();
    EXPECT_TRUE(switched);
}

TEST_F(SchedulerTest, Tick_NoSwitchWhenAlone)
{
    // Single thread at a priority -- time slice expires but no switch needed
    kernel::ThreadId id = createAndRun("t1", g_stack1, sizeof(g_stack1),
                                       kernel::kDefaultPriority, 2);

    m_scheduler.tick();  // 2 -> 1
    bool switched = m_scheduler.tick();  // 1 -> 0, but no peers
    EXPECT_FALSE(switched);

    // Time slice should be reset since there are no peers
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    EXPECT_EQ(tcb->timeSliceRemaining, tcb->timeSlice);
}

TEST_F(SchedulerTest, Tick_HigherPriorityPreempts)
{
    // Running a low-priority thread; a high-priority thread is ready
    kernel::ThreadId low = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId high = createThread("high", g_stack2, sizeof(g_stack2), 5);

    // Only add low first, make it run
    m_scheduler.addThread(low);
    m_scheduler.switchContext();
    ASSERT_EQ(m_scheduler.currentThreadId(), low);

    // Now add the high-priority thread (simulates it becoming ready)
    m_scheduler.addThread(high);

    // Next tick should detect preemption
    bool switched = m_scheduler.tick();
    EXPECT_TRUE(switched);
}

// ---- Yield ----

TEST_F(SchedulerTest, Yield_ResetsTimeSlice)
{
    kernel::ThreadId id = createAndRun("t1", g_stack1, sizeof(g_stack1),
                                       kernel::kDefaultPriority, 10);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);

    // Consume some ticks
    m_scheduler.tick();
    m_scheduler.tick();
    EXPECT_EQ(tcb->timeSliceRemaining, 8u);

    // Yield resets time slice
    m_scheduler.yield();
    EXPECT_EQ(tcb->timeSliceRemaining, 10u);
}

// ---- Thread removal ----

TEST_F(SchedulerTest, RemoveThread_SetsStateInactive)
{
    kernel::ThreadId id = createThread("t1", g_stack1, sizeof(g_stack1));
    m_scheduler.addThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Ready);

    m_scheduler.removeThread(id);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Inactive);
}

TEST_F(SchedulerTest, RemoveThread_RemovesFromQueue)
{
    kernel::ThreadId id1 = createThread("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    m_scheduler.addThread(id1);
    m_scheduler.addThread(id2);
    EXPECT_EQ(m_scheduler.readyCount(), 2u);

    m_scheduler.removeThread(id1);
    EXPECT_EQ(m_scheduler.readyCount(), 1u);
    EXPECT_EQ(m_scheduler.pickNext(), id2);
}

// ---- Idle thread ----

TEST_F(SchedulerTest, IdleThread_RunsWhenQueueEmpty)
{
    EXPECT_EQ(m_scheduler.pickNext(), kernel::kInvalidThreadId);
}

TEST_F(SchedulerTest, IdleThread_FallbackOnSwitchContext)
{
    kernel::ThreadId idle = createThread("idle", g_stack1, sizeof(g_stack1), kernel::kIdlePriority);
    m_scheduler.setIdleThread(idle);

    // No ready threads -- switchContext falls back to idle
    kernel::ThreadId nextId = m_scheduler.switchContext();
    EXPECT_EQ(nextId, idle);
}

// ---- Block / unblock ----

TEST_F(SchedulerTest, BlockCurrentThread_SetsStateBlocked)
{
    kernel::ThreadId id = createAndRun("t1", g_stack1, sizeof(g_stack1));

    m_scheduler.blockCurrentThread();

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Blocked);
}

TEST_F(SchedulerTest, UnblockThread_ReturnsToReadyQueue)
{
    createAndRun("t1", g_stack1, sizeof(g_stack1), 10);
    kernel::ThreadId id2 = createThread("t2", g_stack2, sizeof(g_stack2), 10);

    // Block id2 manually
    kernel::ThreadControlBlock *tcb2 = kernel::threadGetTcb(id2);
    tcb2->state = kernel::ThreadState::Blocked;

    // Unblock it
    m_scheduler.unblockThread(id2);

    EXPECT_EQ(tcb2->state, kernel::ThreadState::Ready);
    EXPECT_EQ(m_scheduler.readyCount(), 1u);  // id2 is now in ready queue
}

TEST_F(SchedulerTest, UnblockThread_ReturnsTrueIfHigherPriority)
{
    createAndRun("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId high = createThread("high", g_stack2, sizeof(g_stack2), 5);

    kernel::ThreadControlBlock *highTcb = kernel::threadGetTcb(high);
    highTcb->state = kernel::ThreadState::Blocked;

    bool preempt = m_scheduler.unblockThread(high);
    EXPECT_TRUE(preempt);
}

TEST_F(SchedulerTest, UnblockThread_ReturnsFalseIfLowerPriority)
{
    createAndRun("high", g_stack1, sizeof(g_stack1), 5);
    kernel::ThreadId low = createThread("low", g_stack2, sizeof(g_stack2), 20);

    kernel::ThreadControlBlock *lowTcb = kernel::threadGetTcb(low);
    lowTcb->state = kernel::ThreadState::Blocked;

    bool preempt = m_scheduler.unblockThread(low);
    EXPECT_FALSE(preempt);
}

// ---- Priority change ----

TEST_F(SchedulerTest, SetThreadPriority_ReposInReadyQueue)
{
    kernel::ThreadId low = createThread("low", g_stack1, sizeof(g_stack1), 20);
    kernel::ThreadId mid = createThread("mid", g_stack2, sizeof(g_stack2), 10);

    m_scheduler.addThread(low);
    m_scheduler.addThread(mid);

    // mid is at priority 10, which is higher than low at 20
    EXPECT_EQ(m_scheduler.pickNext(), mid);

    // Boost low to priority 5 (higher than mid)
    m_scheduler.setThreadPriority(low, 5);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(low);
    EXPECT_EQ(tcb->currentPriority, 5u);
    EXPECT_EQ(m_scheduler.pickNext(), low);
}
