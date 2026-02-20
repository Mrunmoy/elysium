// Mock arch and board crash dump implementations for host-side testing.
//
// Provides mock implementations for arch-specific and board-specific
// crash dump functions. The real CrashDumpCommon.cpp is compiled into
// the test binary, so we only mock the functions it calls:
//   - arch: archPopulateFaultInfo, archDecodeFaultBits, archCrashDumpInit,
//           archTriggerTestFault
//   - board: boardEnsureOutput, boardFaultPutChar, boardFaultFlush,
//            boardFaultBlink
//
// boardFaultPutChar() captures output into test::g_crashOutput.
// boardFaultBlink() uses longjmp to escape back to the test.

#include "kernel/CrashDumpArch.h"
#include "kernel/CrashDumpBoard.h"
#include "MockCrashDump.h"

#include <csetjmp>
#include <cstring>

namespace test
{
    std::string g_crashOutput;

    std::uint32_t g_mockCfsr = 0;
    std::uint32_t g_mockHfsr = 0;
    std::uint32_t g_mockMmfar = 0;
    std::uint32_t g_mockBfar = 0;
    std::uint32_t g_mockShcsr = 0;
    std::uint32_t g_mockCcr = 0;

    std::uint32_t g_mockUsart1Cr1 = 0;

    std::uint32_t g_mockRccAhb1enr = 0;
    std::uint32_t g_mockRccApb2enr = 0;

    std::jmp_buf g_blinkJmpBuf;
    bool g_blinkJmpBufSet = false;

    void resetCrashDumpMockState()
    {
        g_crashOutput.clear();
        g_mockCfsr = 0;
        g_mockHfsr = 0;
        g_mockMmfar = 0;
        g_mockBfar = 0;
        g_mockShcsr = 0;
        g_mockCcr = 0;
        g_mockUsart1Cr1 = 0;
        g_mockRccAhb1enr = 0;
        g_mockRccApb2enr = 0;
        g_blinkJmpBufSet = false;
    }

}  // namespace test

namespace kernel
{
    // ---- Arch mocks ----

    void archPopulateFaultInfo(FaultInfo &info, std::uint32_t *stackFrame,
                               std::uint32_t excReturn)
    {
        // Read stacked registers from the test-provided stack frame
        info.r0 = stackFrame[0];
        info.r1 = stackFrame[1];
        info.r2 = stackFrame[2];
        info.r3 = stackFrame[3];
        info.r12 = stackFrame[4];
        info.lr = stackFrame[5];
        info.pc = stackFrame[6];
        info.statusReg = stackFrame[7];
        info.sp = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(stackFrame));

        // Use mock fault register values
        info.faultReg[0] = test::g_mockCfsr;
        info.faultReg[1] = test::g_mockHfsr;
        info.faultReg[2] = test::g_mockMmfar;
        info.faultReg[3] = test::g_mockBfar;

        info.faultRegNames[0] = "CFSR : ";
        info.faultRegNames[1] = "HFSR : ";
        info.faultRegNames[2] = "MMFAR: ";
        info.faultRegNames[3] = "BFAR : ";

        info.faultType = "MockFault";
        info.excInfo = excReturn;
    }

    void archDecodeFaultBits(const FaultInfo &)
    {
        // No-op in mock -- decoded bits are arch-specific
    }

    void archCrashDumpInit()
    {
        // No-op on host
    }

    void archTriggerTestFault(FaultType)
    {
        // No-op on host
    }

    // ---- Board mocks ----

    void boardEnsureOutput()
    {
        // No-op on host
    }

    void boardFaultPutChar(char c)
    {
        test::g_crashOutput += c;
    }

    void boardFaultFlush()
    {
        // No-op on host
    }

    [[noreturn]] void boardFaultBlink()
    {
        if (test::g_blinkJmpBufSet)
        {
            std::longjmp(test::g_blinkJmpBuf, 1);
        }
        // Fallback: spin forever (matches [[noreturn]] contract)
        while (true)
        {
        }
    }

}  // namespace kernel
