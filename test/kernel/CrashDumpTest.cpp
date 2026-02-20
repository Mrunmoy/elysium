// Unit tests for crash dump formatting logic (CrashDumpCommon.cpp).
//
// These tests compile the real CrashDumpCommon.cpp and use mock
// implementations for arch/board functions. boardFaultPutChar() captures
// output into test::g_crashOutput, and boardFaultBlink() uses longjmp
// to return control to the test.

#include <gtest/gtest.h>

#include "kernel/CrashDump.h"
#include "kernel/CrashDumpArch.h"
#include "kernel/CrashDumpBoard.h"
#include "kernel/Thread.h"
#include "kernel/CortexM.h"

#include "MockCrashDump.h"
#include "MockKernel.h"

#include <csetjmp>
#include <cstdint>
#include <string>

// faultHandlerC is extern "C" in CrashDumpCommon.cpp
extern "C" void faultHandlerC(std::uint32_t *stackFrame, std::uint32_t excReturn);

// faultPrint and faultPrintHex are in kernel namespace (CrashDumpInternal.h)
namespace kernel
{
    void faultPrint(const char *str);
    void faultPrintHex(std::uint32_t value);
}  // namespace kernel

namespace
{
    alignas(8) std::uint32_t g_testStack[128];
}  // namespace

class CrashDumpTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test::resetCrashDumpMockState();
        test::resetKernelMockState();
        kernel::threadReset();
        kernel::g_currentTcb = nullptr;
        kernel::g_nextTcb = nullptr;
    }

    // Call faultHandlerC with setjmp to catch the boardFaultBlink longjmp
    void callFaultHandler(std::uint32_t *stackFrame, std::uint32_t excReturn)
    {
        if (setjmp(test::g_blinkJmpBuf) == 0)
        {
            test::g_blinkJmpBufSet = true;
            faultHandlerC(stackFrame, excReturn);
            // Should not reach here (boardFaultBlink longjmps)
            FAIL() << "faultHandlerC returned without calling boardFaultBlink";
        }
        // Returned via longjmp from boardFaultBlink
        test::g_blinkJmpBufSet = false;
    }
};

// ---- faultPrintHex tests ----

TEST_F(CrashDumpTest, FaultPrintHex_Zero)
{
    kernel::faultPrintHex(0);
    EXPECT_EQ(test::g_crashOutput, "00000000");
}

TEST_F(CrashDumpTest, FaultPrintHex_DeadBeef)
{
    kernel::faultPrintHex(0xDEADBEEF);
    EXPECT_EQ(test::g_crashOutput, "DEADBEEF");
}

TEST_F(CrashDumpTest, FaultPrintHex_AllFs)
{
    kernel::faultPrintHex(0xFFFFFFFF);
    EXPECT_EQ(test::g_crashOutput, "FFFFFFFF");
}

TEST_F(CrashDumpTest, FaultPrintHex_SmallValue)
{
    kernel::faultPrintHex(0x42);
    EXPECT_EQ(test::g_crashOutput, "00000042");
}

// ---- faultPrint tests ----

TEST_F(CrashDumpTest, FaultPrint_BasicString)
{
    kernel::faultPrint("hello");
    EXPECT_EQ(test::g_crashOutput, "hello");
}

TEST_F(CrashDumpTest, FaultPrint_EmptyString)
{
    kernel::faultPrint("");
    EXPECT_EQ(test::g_crashOutput, "");
}

// ---- faultHandlerC full output ----

TEST_F(CrashDumpTest, FaultHandler_ContainsBeginEndMarkers)
{
    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("=== CRASH DUMP BEGIN ==="),
              std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("=== CRASH DUMP END ==="),
              std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsFaultType)
{
    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("Fault: MockFault"),
              std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsRegisters)
{
    // R0=0x11, R1=0x22, R2=0x33, R3=0x44, R12=0x55, LR=0x66, PC=0x77, xPSR=0x88
    std::uint32_t stackFrame[8] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("PC  : 00000077"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("LR  : 00000066"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("R0  : 00000011"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("R1  : 00000022"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("R2  : 00000033"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("R3  : 00000044"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("R12 : 00000055"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("xPSR: 00000088"), std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsFaultStatusRegisters)
{
    test::g_mockCfsr = 0x00008200;
    test::g_mockHfsr = 0x40000000;
    test::g_mockMmfar = 0x00000000;
    test::g_mockBfar = 0xCCCCCCCC;

    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("CFSR : 00008200"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("HFSR : 40000000"), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find("BFAR : CCCCCCCC"), std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsExcReturn)
{
    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("EXC_RETURN: FFFFFFFD"),
              std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsThreadContext)
{
    // Create a thread so g_currentTcb can point to it
    kernel::ThreadConfig config{};
    config.function = nullptr;
    config.arg = nullptr;
    config.name = "test-thread";
    config.stack = g_testStack;
    config.stackSize = sizeof(g_testStack);
    config.priority = 10;

    kernel::ThreadId tid = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(tid);
    kernel::g_currentTcb = tcb;

    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("Thread: test-thread"),
              std::string::npos);
    // Thread ID 0 (first created)
    EXPECT_NE(test::g_crashOutput.find("(id=0)"), std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsNoneWhenNoThread)
{
    kernel::g_currentTcb = nullptr;

    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    EXPECT_NE(test::g_crashOutput.find("Thread: (none)"),
              std::string::npos);
}

TEST_F(CrashDumpTest, FaultHandler_PrintsStackInfo)
{
    kernel::ThreadConfig config{};
    config.function = nullptr;
    config.arg = nullptr;
    config.name = "stack-test";
    config.stack = g_testStack;
    config.stackSize = sizeof(g_testStack);
    config.priority = 10;

    kernel::ThreadId tid = kernel::threadCreate(config);
    kernel::ThreadControlBlock *tcb = kernel::threadGetTcb(tid);
    kernel::g_currentTcb = tcb;

    std::uint32_t stackFrame[8] = {0};
    callFaultHandler(stackFrame, 0xFFFFFFFD);

    // Should contain "Stack: base=" followed by hex and " size="
    EXPECT_NE(test::g_crashOutput.find("Stack: base="), std::string::npos);
    EXPECT_NE(test::g_crashOutput.find(" size="), std::string::npos);
}
