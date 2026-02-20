#include <gtest/gtest.h>

#include "kernel/Thread.h"
#include "kernel/CortexM.h"

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
