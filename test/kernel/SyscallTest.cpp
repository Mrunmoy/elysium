// Tests for privilege field in TCB and thread creation.
//
// Verifies that threads can be created as privileged or unprivileged,
// and that the default is privileged.

#include "kernel/Thread.h"
#include "kernel/Kernel.h"
#include "kernel/Scheduler.h"
#include "MockKernel.h"

#include <gtest/gtest.h>
#include <cstring>

class SyscallTest : public ::testing::Test
{
protected:
    alignas(512) std::uint32_t m_stack[128];

    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::internal::scheduler().init();
    }
};

TEST_F(SyscallTest, DefaultThreadIsPrivileged)
{
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "priv";
    cfg.stack = m_stack;
    cfg.stackSize = sizeof(m_stack);
    cfg.priority = 16;
    cfg.privileged = true;

    kernel::ThreadId id = kernel::threadCreate(cfg);
    auto *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_TRUE(tcb->privileged);
}

TEST_F(SyscallTest, UnprivilegedThreadHasPrivilegedFalse)
{
    kernel::ThreadConfig cfg{};
    cfg.function = [](void *) {};
    cfg.name = "unpriv";
    cfg.stack = m_stack;
    cfg.stackSize = sizeof(m_stack);
    cfg.priority = 16;
    cfg.privileged = false;

    kernel::ThreadId id = kernel::threadCreate(cfg);
    auto *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_FALSE(tcb->privileged);
}

TEST_F(SyscallTest, CreateThreadDefaultIsPrivileged)
{
    // kernel::createThread with default privileged parameter
    kernel::ThreadId id = kernel::createThread(
        [](void *) {}, nullptr, "def",
        m_stack, sizeof(m_stack), 16, 0);
    auto *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_TRUE(tcb->privileged);
}

TEST_F(SyscallTest, CreateThreadExplicitUnprivileged)
{
    kernel::ThreadId id = kernel::createThread(
        [](void *) {}, nullptr, "unpriv",
        m_stack, sizeof(m_stack), 16, 0, false);
    auto *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_FALSE(tcb->privileged);
}

TEST_F(SyscallTest, CreateThreadExplicitPrivileged)
{
    kernel::ThreadId id = kernel::createThread(
        [](void *) {}, nullptr, "priv",
        m_stack, sizeof(m_stack), 16, 0, true);
    auto *tcb = kernel::threadGetTcb(id);
    ASSERT_NE(tcb, nullptr);
    EXPECT_TRUE(tcb->privileged);
}
