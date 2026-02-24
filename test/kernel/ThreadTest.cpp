#include <gtest/gtest.h>

#include "kernel/Thread.h"
#include "kernel/Kernel.h"
#include "kernel/Ipc.h"
#include "kernel/Arch.h"

#include "MockKernel.h"

namespace
{
    void dummyThread(void *arg)
    {
        (void)arg;
    }

    void argThread(void *arg)
    {
        auto *value = static_cast<std::uint32_t *>(arg);
        *value = 42;
    }

    // Stack buffers for test threads
    alignas(8) std::uint32_t g_testStack1[128];  // 512 bytes
    alignas(8) std::uint32_t g_testStack2[128];
}  // namespace

class ThreadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
    }
};

TEST_F(ThreadTest, CreateThread_AssignsValidId)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);

    EXPECT_NE(id, kernel::kInvalidThreadId);
    EXPECT_LT(id, kernel::kMaxThreads);
}

TEST_F(ThreadTest, CreateThread_SetsStateToReady)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);

    ASSERT_NE(tcb, nullptr);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Ready);
}

TEST_F(ThreadTest, CreateThread_InitializesStackFrame)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    // Stack pointer should point into the stack buffer
    EXPECT_GE(tcb->stackPointer, g_testStack1);
    EXPECT_LT(tcb->stackPointer, g_testStack1 + 128);

    // Initial stack frame layout (16 words):
    // SP+0:  r4  (0)             software context
    // SP+1:  r5  (0)
    // ...
    // SP+7:  r11 (0)
    // SP+8:  r0  (arg)           hardware exception frame
    // SP+9:  r1  (0)
    // SP+10: r2  (0)
    // SP+11: r3  (0)
    // SP+12: r12 (0)
    // SP+13: LR  (kernelThreadExit)
    // SP+14: PC  (entry function)
    // SP+15: xPSR (0x01000000)

    std::uint32_t *sp = tcb->stackPointer;

    // r4-r11 should be zero (software context)
    for (int i = 0; i <= 7; ++i)
    {
        EXPECT_EQ(sp[i], 0u) << "r" << (i + 4) << " should be 0";
    }

    // r0 = arg (nullptr = 0)
    EXPECT_EQ(sp[8], 0u);

    // LR = kernelThreadExit (truncated through uintptr_t, same as Thread.cpp)
    EXPECT_EQ(sp[13], static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&kernel::kernelThreadExit)));

    // PC = entry function
    EXPECT_EQ(sp[14], static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&dummyThread)));

    // xPSR with Thumb bit set
    EXPECT_EQ(sp[15], 0x01000000u);
}

TEST_F(ThreadTest, CreateThread_StackAligned8Bytes)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    // Stack pointer must be 8-byte aligned
    std::uintptr_t sp = reinterpret_cast<std::uintptr_t>(tcb->stackPointer);
    EXPECT_EQ(sp % 8, 0u);
}

TEST_F(ThreadTest, CreateThread_MaxThreadsReturnsInvalid)
{
    alignas(8) static std::uint32_t stacks[kernel::kMaxThreads][128];

    for (std::uint8_t i = 0; i < kernel::kMaxThreads; ++i)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = "thread";
        config.stack = stacks[i];
        config.stackSize = sizeof(stacks[i]);

        kernel::ThreadId id = kernel::threadCreate(config);
        EXPECT_NE(id, kernel::kInvalidThreadId) << "Thread " << (int)i << " should succeed";
    }

    // One more should fail
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "overflow";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    EXPECT_EQ(id, kernel::kInvalidThreadId);
}

TEST_F(ThreadTest, CreateThread_ArgumentPassedInR0)
{
    std::uint32_t argValue = 0xDEADBEEF;

    kernel::ThreadConfig config{};
    config.function = argThread;
    config.arg = &argValue;
    config.name = "arg-test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    // r0 in stack frame should be the arg pointer (at index 8)
    std::uint32_t *sp = tcb->stackPointer;
    EXPECT_EQ(sp[8], static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&argValue)));
}

TEST_F(ThreadTest, CreateThread_SetsNameAndStackInfo)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "worker";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    EXPECT_STREQ(tcb->name, "worker");
    EXPECT_EQ(tcb->stackBase, g_testStack1);
    EXPECT_EQ(tcb->stackSize, sizeof(g_testStack1));
}

TEST_F(ThreadTest, CreateThread_DefaultTimeSlice)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);
    config.timeSlice = 0;  // use default

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    EXPECT_EQ(tcb->timeSlice, kernel::kDefaultTimeSlice);
    EXPECT_EQ(tcb->timeSliceRemaining, kernel::kDefaultTimeSlice);
}

TEST_F(ThreadTest, CreateThread_CustomTimeSlice)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);
    config.timeSlice = 20;

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    EXPECT_EQ(tcb->timeSlice, 20u);
    EXPECT_EQ(tcb->timeSliceRemaining, 20u);
}

TEST_F(ThreadTest, GetTcb_InvalidIdReturnsNull)
{
    EXPECT_EQ(kernel::threadGetTcb(kernel::kInvalidThreadId), nullptr);
    EXPECT_EQ(kernel::threadGetTcb(kernel::kMaxThreads), nullptr);
}

TEST_F(ThreadTest, CreateThread_SequentialIds)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "t1";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id1 = kernel::threadCreate(config);

    config.name = "t2";
    config.stack = g_testStack2;
    config.stackSize = sizeof(g_testStack2);

    kernel::ThreadId id2 = kernel::threadCreate(config);

    EXPECT_EQ(id2, id1 + 1);
}

TEST_F(ThreadTest, CreateThread_SetsPriorityFields)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "pri-test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);
    config.priority = 7;

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    EXPECT_EQ(tcb->basePriority, 7u);
    EXPECT_EQ(tcb->currentPriority, 7u);
}

TEST_F(ThreadTest, CreateThread_InitializesLinkedListPointers)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "list-test";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);

    EXPECT_EQ(tcb->nextReady, kernel::kInvalidThreadId);
    EXPECT_EQ(tcb->nextWait, kernel::kInvalidThreadId);
    EXPECT_EQ(tcb->wakeupTick, 0u);
}

// ---- Dynamic thread destruction tests ----

TEST_F(ThreadTest, DestroyThread_MarksSlotInactive)
{
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "doomed";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);

    kernel::ThreadId id = kernel::threadCreate(config);
    ASSERT_NE(id, kernel::kInvalidThreadId);

    kernel::threadDestroy(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Inactive);
    EXPECT_EQ(tcb->id, kernel::kInvalidThreadId);
    EXPECT_EQ(tcb->name, nullptr);
    EXPECT_EQ(tcb->stackBase, nullptr);
}

TEST_F(ThreadTest, DestroyThread_InvalidIdDoesNothing)
{
    // Should not crash
    kernel::threadDestroy(kernel::kInvalidThreadId);
    kernel::threadDestroy(kernel::kMaxThreads);
}

TEST_F(ThreadTest, CreateThread_ReusesDestroyedSlot)
{
    // Fill all slots
    alignas(8) static std::uint32_t stacks[kernel::kMaxThreads][128];
    kernel::ThreadId ids[kernel::kMaxThreads];

    for (std::uint8_t i = 0; i < kernel::kMaxThreads; ++i)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = "thread";
        config.stack = stacks[i];
        config.stackSize = sizeof(stacks[i]);

        ids[i] = kernel::threadCreate(config);
        ASSERT_NE(ids[i], kernel::kInvalidThreadId);
    }

    // Should fail when all full
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "extra";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);
    EXPECT_EQ(kernel::threadCreate(config), kernel::kInvalidThreadId);

    // Destroy slot 3
    kernel::threadDestroy(ids[3]);

    // Now creation should succeed and reuse slot 3
    config.name = "reused";
    kernel::ThreadId newId = kernel::threadCreate(config);
    EXPECT_EQ(newId, ids[3]);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(newId);
    ASSERT_NE(tcb, nullptr);
    EXPECT_STREQ(tcb->name, "reused");
    EXPECT_EQ(tcb->state, kernel::ThreadState::Ready);
}

TEST_F(ThreadTest, CreateThread_ReusesFirstAvailableSlot)
{
    // Create 3 threads
    alignas(8) static std::uint32_t stacks[3][128];
    kernel::ThreadId ids[3];

    for (int i = 0; i < 3; ++i)
    {
        kernel::ThreadConfig config{};
        config.function = dummyThread;
        config.arg = nullptr;
        config.name = "t";
        config.stack = stacks[i];
        config.stackSize = sizeof(stacks[i]);
        ids[i] = kernel::threadCreate(config);
    }

    // Destroy threads 0 and 2
    kernel::threadDestroy(ids[0]);
    kernel::threadDestroy(ids[2]);

    // Next create should get slot 0 (first available)
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "new";
    config.stack = g_testStack1;
    config.stackSize = sizeof(g_testStack1);
    kernel::ThreadId newId = kernel::threadCreate(config);
    EXPECT_EQ(newId, ids[0]);
}

// ---- Kernel destroyThread integration tests ----

class DestroyThreadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::ipcReset();
        kernel::internal::scheduler().init();
    }

    kernel::ThreadId createTestThread(const char *name, std::uint32_t *stack,
                                      std::uint32_t stackSize, std::uint8_t priority = 10)
    {
        return kernel::createThread(dummyThread, nullptr, name,
                                    stack, stackSize, priority);
    }
};

TEST_F(DestroyThreadTest, DestroyThread_ReturnsTrue)
{
    alignas(8) static std::uint32_t stack[128];
    kernel::ThreadId id = createTestThread("victim", stack, sizeof(stack));
    ASSERT_NE(id, kernel::kInvalidThreadId);

    EXPECT_TRUE(kernel::destroyThread(id));
}

TEST_F(DestroyThreadTest, DestroyThread_MarksInactive)
{
    alignas(8) static std::uint32_t stack[128];
    kernel::ThreadId id = createTestThread("victim", stack, sizeof(stack));

    kernel::destroyThread(id);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
    EXPECT_EQ(tcb->state, kernel::ThreadState::Inactive);
}

TEST_F(DestroyThreadTest, DestroyThread_RemovesFromScheduler)
{
    alignas(8) static std::uint32_t stack[128];
    kernel::ThreadId id = createTestThread("victim", stack, sizeof(stack), 10);

    std::uint8_t countBefore = kernel::internal::scheduler().readyCount();
    kernel::destroyThread(id);
    std::uint8_t countAfter = kernel::internal::scheduler().readyCount();

    EXPECT_EQ(countAfter, countBefore - 1);
}

TEST_F(DestroyThreadTest, DestroyThread_CleansUpMailbox)
{
    alignas(8) static std::uint32_t stack[128];
    kernel::ThreadId id = createTestThread("victim", stack, sizeof(stack));

    // Put something in the mailbox
    kernel::Message msg{};
    msg.type = static_cast<std::uint8_t>(kernel::MessageType::OneWay);
    msg.payloadSize = 0;
    kernel::messageTrySend(id, msg);

    kernel::ThreadMailbox *box = kernel::ipcGetMailbox(id);
    ASSERT_NE(box, nullptr);
    EXPECT_GT(box->count, 0);

    kernel::destroyThread(id);

    // Mailbox should be reset
    EXPECT_EQ(box->count, 0);
    EXPECT_EQ(box->head, 0);
    EXPECT_EQ(box->tail, 0);
}

TEST_F(DestroyThreadTest, DestroyThread_IdCanBeReused)
{
    alignas(8) static std::uint32_t stack1[128];
    alignas(8) static std::uint32_t stack2[128];

    kernel::ThreadId id1 = createTestThread("first", stack1, sizeof(stack1));
    kernel::destroyThread(id1);

    kernel::ThreadId id2 = createTestThread("second", stack2, sizeof(stack2));
    EXPECT_EQ(id1, id2);

    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id2);
    EXPECT_STREQ(tcb->name, "second");
}

TEST_F(DestroyThreadTest, DestroyThread_InvalidIdReturnsFalse)
{
    EXPECT_FALSE(kernel::destroyThread(kernel::kInvalidThreadId));
    EXPECT_FALSE(kernel::destroyThread(kernel::kMaxThreads));
}

TEST_F(DestroyThreadTest, DestroyThread_InactiveReturnsFalse)
{
    // Slot 0 is inactive (never created beyond reset)
    EXPECT_FALSE(kernel::destroyThread(0));
}

TEST_F(DestroyThreadTest, DestroyThread_IdleThreadReturnsFalse)
{
    // Create idle thread in slot 0 and register it with scheduler
    alignas(8) static std::uint32_t idleStack[128];
    kernel::ThreadConfig config{};
    config.function = dummyThread;
    config.arg = nullptr;
    config.name = "idle";
    config.stack = idleStack;
    config.stackSize = sizeof(idleStack);
    config.priority = kernel::kIdlePriority;
    kernel::ThreadId idleId = kernel::threadCreate(config);
    ASSERT_EQ(idleId, kernel::kIdleThreadId);
    kernel::internal::scheduler().setIdleThread(idleId);

    EXPECT_FALSE(kernel::destroyThread(idleId));
}

TEST_F(DestroyThreadTest, DestroyThread_MultipleCreateDestroyReusesCycles)
{
    alignas(8) static std::uint32_t stack[128];

    // Create and destroy the same slot multiple times
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        kernel::ThreadId id = createTestThread("cyclic", stack, sizeof(stack));
        ASSERT_NE(id, kernel::kInvalidThreadId) << "Cycle " << cycle;

        kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(id);
        EXPECT_EQ(tcb->state, kernel::ThreadState::Ready);

        kernel::destroyThread(id);

        tcb = kernel::threadGetTcb(id);
        EXPECT_EQ(tcb->state, kernel::ThreadState::Inactive);
    }
}
