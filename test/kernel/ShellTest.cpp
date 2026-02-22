// Shell tests.
//
// Tests the kernel shell command parsing, line editing, and built-in
// commands using mock I/O (no UART hardware required).

#include "kernel/Shell.h"
#include "kernel/Thread.h"
#include "kernel/Scheduler.h"
#include "kernel/Heap.h"
#include "kernel/Ipc.h"
#include "MockKernel.h"

#include <gtest/gtest.h>
#include <string>
#include <cstring>

namespace
{
    std::string g_output;

    void mockWrite(const char *str)
    {
        g_output += str;
    }

    void sendLine(const char *cmd)
    {
        for (const char *p = cmd; *p != '\0'; ++p)
        {
            kernel::shellProcessChar(*p);
        }
        kernel::shellProcessChar('\r');
    }

}  // namespace

class ShellTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::ipcReset();
        kernel::shellReset();
        g_output.clear();

        kernel::ShellConfig config{};
        config.writeFn = mockWrite;
        kernel::shellInit(config);
    }
};

// ---- Prompt ----

TEST_F(ShellTest, Prompt_PrintsExpectedString)
{
    kernel::shellPrompt();
    EXPECT_EQ(g_output, "ms-os> ");
}

// ---- Character echo ----

TEST_F(ShellTest, Echo_PrintableCharsEchoed)
{
    kernel::shellProcessChar('a');
    EXPECT_EQ(g_output, "a");

    kernel::shellProcessChar('b');
    EXPECT_EQ(g_output, "ab");
}

TEST_F(ShellTest, Echo_ControlCharsNotEchoed)
{
    kernel::shellProcessChar('\x01');
    EXPECT_EQ(g_output, "");
}

// ---- Enter / newline ----

TEST_F(ShellTest, Enter_EmptyLineShowsPrompt)
{
    kernel::shellProcessChar('\r');
    // Should output: \r\n + prompt
    EXPECT_NE(g_output.find("\r\n"), std::string::npos);
    EXPECT_NE(g_output.find("ms-os> "), std::string::npos);
}

TEST_F(ShellTest, Enter_LineFeedAlsoWorks)
{
    kernel::shellProcessChar('\n');
    EXPECT_NE(g_output.find("\r\n"), std::string::npos);
    EXPECT_NE(g_output.find("ms-os> "), std::string::npos);
}

// ---- Backspace ----

TEST_F(ShellTest, Backspace_ErasesCharacter)
{
    kernel::shellProcessChar('a');
    kernel::shellProcessChar('b');
    g_output.clear();
    kernel::shellProcessChar('\b');
    EXPECT_EQ(g_output, "\b \b");
}

TEST_F(ShellTest, Backspace_AtStartDoesNothing)
{
    kernel::shellProcessChar('\b');
    EXPECT_EQ(g_output, "");
}

TEST_F(ShellTest, Delete_AlsoErases)
{
    kernel::shellProcessChar('x');
    g_output.clear();
    kernel::shellProcessChar(0x7F);
    EXPECT_EQ(g_output, "\b \b");
}

TEST_F(ShellTest, Backspace_ThenEnter_ExecutesCorrectCommand)
{
    // Type "pss", backspace, enter -> should execute "ps"
    kernel::shellProcessChar('p');
    kernel::shellProcessChar('s');
    kernel::shellProcessChar('s');
    kernel::shellProcessChar('\b');
    g_output.clear();
    kernel::shellProcessChar('\r');
    // Output should contain thread listing header (ps command)
    EXPECT_NE(g_output.find("TID"), std::string::npos);
}

// ---- Unknown command ----

TEST_F(ShellTest, UnknownCommand_PrintsError)
{
    sendLine("bogus");
    EXPECT_NE(g_output.find("unknown command: bogus"), std::string::npos);
}

// ---- help command ----

TEST_F(ShellTest, Help_ListsCommands)
{
    sendLine("help");
    EXPECT_NE(g_output.find("commands:"), std::string::npos);
    EXPECT_NE(g_output.find("help"), std::string::npos);
    EXPECT_NE(g_output.find("ps"), std::string::npos);
    EXPECT_NE(g_output.find("mem"), std::string::npos);
    EXPECT_NE(g_output.find("uptime"), std::string::npos);
    EXPECT_NE(g_output.find("version"), std::string::npos);
}

// ---- version command ----

TEST_F(ShellTest, Version_PrintsVersionString)
{
    sendLine("version");
    EXPECT_NE(g_output.find("ms-os v0.9.0"), std::string::npos);
}

// ---- uptime command ----

TEST_F(ShellTest, Uptime_ShowsTicksAndSeconds)
{
    test::g_tickCount = 5000;
    sendLine("uptime");
    EXPECT_NE(g_output.find("5000 ticks"), std::string::npos);
    EXPECT_NE(g_output.find("5s"), std::string::npos);
}

TEST_F(ShellTest, Uptime_ZeroTicks)
{
    test::g_tickCount = 0;
    sendLine("uptime");
    EXPECT_NE(g_output.find("0 ticks"), std::string::npos);
    EXPECT_NE(g_output.find("0s"), std::string::npos);
}

// ---- ps command ----

TEST_F(ShellTest, Ps_NoThreads_ShowsHeaderOnly)
{
    sendLine("ps");
    EXPECT_NE(g_output.find("TID"), std::string::npos);
    EXPECT_NE(g_output.find("NAME"), std::string::npos);
    // No thread lines (all Inactive)
}

TEST_F(ShellTest, Ps_ShowsActiveThreads)
{
    // Create a thread to have something to list
    alignas(8) static std::uint32_t stack[64];
    kernel::ThreadConfig config{};
    config.function = [](void *) {};
    config.arg = nullptr;
    config.name = "test-thd";
    config.stack = stack;
    config.stackSize = sizeof(stack);
    config.priority = 10;
    config.timeSlice = 0;
    kernel::ThreadId tid = kernel::threadCreate(config);

    // Mark it as Ready (threadCreate sets it to Ready)
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(tid);
    ASSERT_NE(tcb, nullptr);
    ASSERT_EQ(tcb->state, kernel::ThreadState::Ready);

    sendLine("ps");
    EXPECT_NE(g_output.find("test-thd"), std::string::npos);
    EXPECT_NE(g_output.find("Ready"), std::string::npos);
}

TEST_F(ShellTest, Ps_ShowsBlockedState)
{
    alignas(8) static std::uint32_t stack[64];
    kernel::ThreadConfig config{};
    config.function = [](void *) {};
    config.arg = nullptr;
    config.name = "blocked";
    config.stack = stack;
    config.stackSize = sizeof(stack);
    config.priority = 5;
    config.timeSlice = 0;
    kernel::ThreadId tid = kernel::threadCreate(config);

    // Manually set to Blocked for test
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(tid);
    tcb->state = kernel::ThreadState::Blocked;

    sendLine("ps");
    EXPECT_NE(g_output.find("blocked"), std::string::npos);
    EXPECT_NE(g_output.find("Block"), std::string::npos);
}

TEST_F(ShellTest, Ps_ShowsPriority)
{
    alignas(8) static std::uint32_t stack[64];
    kernel::ThreadConfig config{};
    config.function = [](void *) {};
    config.arg = nullptr;
    config.name = "pri-thd";
    config.stack = stack;
    config.stackSize = sizeof(stack);
    config.priority = 25;
    config.timeSlice = 0;
    kernel::threadCreate(config);

    sendLine("ps");
    EXPECT_NE(g_output.find("pri-thd"), std::string::npos);
    EXPECT_NE(g_output.find("25"), std::string::npos);
}

// ---- mem command ----

TEST_F(ShellTest, Mem_ShowsHeapStats)
{
    // Initialize heap with a small test region
    alignas(8) static std::uint8_t heapMem[512];
    kernel::heapReset();
    kernel::heapInit(heapMem, heapMem + sizeof(heapMem));

    sendLine("mem");
    EXPECT_NE(g_output.find("total:"), std::string::npos);
    EXPECT_NE(g_output.find("used:"), std::string::npos);
    EXPECT_NE(g_output.find("free:"), std::string::npos);
    EXPECT_NE(g_output.find("peak:"), std::string::npos);
    EXPECT_NE(g_output.find("allocs:"), std::string::npos);
    EXPECT_NE(g_output.find("largest:"), std::string::npos);
}

TEST_F(ShellTest, Mem_ReflectsAllocation)
{
    alignas(8) static std::uint8_t heapMem[1024];
    kernel::heapReset();
    kernel::heapInit(heapMem, heapMem + sizeof(heapMem));

    // Allocate some memory
    void *p = kernel::heapAlloc(64);
    ASSERT_NE(p, nullptr);

    sendLine("mem");
    // Should show at least 1 allocation
    EXPECT_NE(g_output.find("allocs:   1"), std::string::npos);

    kernel::heapFree(p);
}

// ---- Leading whitespace ----

TEST_F(ShellTest, LeadingWhitespace_Trimmed)
{
    sendLine("  version");
    EXPECT_NE(g_output.find("ms-os v0.9.0"), std::string::npos);
}

// ---- Line buffer overflow ----

TEST_F(ShellTest, LineBuffer_TruncatesAtMax)
{
    // Send 90 characters (more than kMaxLineLength=80)
    for (int i = 0; i < 90; ++i)
    {
        kernel::shellProcessChar('a');
    }
    g_output.clear();
    kernel::shellProcessChar('\r');
    // Should execute the truncated line (all 'a's) -- unknown command
    EXPECT_NE(g_output.find("unknown command:"), std::string::npos);
}

// ---- Multiple commands in sequence ----

TEST_F(ShellTest, MultipleCommands_EachExecutes)
{
    sendLine("version");
    EXPECT_NE(g_output.find("ms-os v0.9.0"), std::string::npos);

    g_output.clear();
    sendLine("help");
    EXPECT_NE(g_output.find("commands:"), std::string::npos);
}

// ---- Reset ----

TEST_F(ShellTest, Reset_ClearsState)
{
    kernel::shellProcessChar('a');
    kernel::shellProcessChar('b');
    kernel::shellReset();

    // After reset, the write function should be null
    g_output.clear();
    kernel::shellProcessChar('x');
    // No output because writeFn is null after reset
    EXPECT_EQ(g_output, "");
}
