// Portable crash dump handler.
//
// Called from FaultHandlers.s (via arch assembly) when a fault occurs.
// Uses the arch-agnostic FaultInfo struct populated by arch-specific code,
// then outputs a structured crash dump via board-specific output functions.
//
// This file contains NO hardware addresses. All peripheral access is
// delegated to board-specific code (CrashDumpBoard.cpp) and all
// CPU-specific code lives in arch-specific files (CrashDumpArch.cpp).
//
// Constraints: no heap, no exceptions, no RTOS calls, no interrupts.

#include "kernel/CrashDump.h"
#include "kernel/CrashDumpArch.h"
#include "kernel/CrashDumpBoard.h"
#include "kernel/Thread.h"
#include "CrashDumpInternal.h"

#include <cstdint>

namespace kernel
{
    // Defined in Kernel.cpp, used by ContextSwitch.s (extern "C" linkage)
    extern "C"
    {
        extern ThreadControlBlock *g_currentTcb;
    }
}

namespace
{
    // Print "  label: HEXVALUE\r\n"
    void faultPrintReg(const char *label, std::uint32_t value)
    {
        kernel::faultPrint("  ");
        kernel::faultPrint(label);
        kernel::faultPrintHex(value);
        kernel::faultPrint("\r\n");
    }

}  // namespace

namespace kernel
{
    // Output helpers -- visible to arch code via CrashDumpInternal.h

    void faultPrint(const char *str)
    {
        while (*str)
        {
            boardFaultPutChar(*str++);
        }
        boardFaultFlush();
    }

    void faultPrintHex(std::uint32_t value)
    {
        constexpr char kHexDigits[] = "0123456789ABCDEF";
        char buf[9];
        for (int i = 7; i >= 0; --i)
        {
            buf[i] = kHexDigits[value & 0xF];
            value >>= 4;
        }
        buf[8] = '\0';
        faultPrint(buf);
    }

    void crashDumpInit()
    {
        archCrashDumpInit();
    }

    void triggerTestFault(FaultType type)
    {
        archTriggerTestFault(type);
    }

    extern "C" void faultHandlerC(std::uint32_t *stackFrame, std::uint32_t excReturn)
    {
        // Ensure output device is ready (may init from scratch if needed)
        boardEnsureOutput();

        // Populate arch-specific fault info
        FaultInfo info;
        archPopulateFaultInfo(info, stackFrame, excReturn);

        // Read thread context
        ThreadControlBlock *tcb = g_currentTcb;

        // --- Output structured crash dump ---

        faultPrint("\r\n=== CRASH DUMP BEGIN ===\r\n");

        // Fault type
        faultPrint("Fault: ");
        faultPrint(info.faultType);
        faultPrint("\r\n");

        // Thread context
        faultPrint("Thread: ");
        if (tcb != nullptr && tcb->name != nullptr)
        {
            faultPrint(tcb->name);
            faultPrint(" (id=");
            // Print thread ID as single digit (IDs are 0-7)
            char idBuf[2];
            idBuf[0] = '0' + tcb->id;
            idBuf[1] = '\0';
            faultPrint(idBuf);
            faultPrint(")");
        }
        else
        {
            faultPrint("(none)");
        }
        faultPrint("\r\n");

        // Registers
        faultPrint("Registers:\r\n");
        faultPrintReg("PC  : ", info.pc);
        faultPrintReg("LR  : ", info.lr);
        faultPrintReg("SP  : ", info.sp);
        faultPrintReg("R0  : ", info.r0);
        faultPrintReg("R1  : ", info.r1);
        faultPrintReg("R2  : ", info.r2);
        faultPrintReg("R3  : ", info.r3);
        faultPrintReg("R12 : ", info.r12);
        faultPrintReg("xPSR: ", info.statusReg);

        // Fault status registers
        faultPrint("Fault Status:\r\n");
        for (int i = 0; i < 4; ++i)
        {
            faultPrintReg(info.faultRegNames[i], info.faultReg[i]);
        }

        // Decoded fault bits
        faultPrint("Decoded:\r\n");
        archDecodeFaultBits(info);

        // Stack info
        if (tcb != nullptr)
        {
            faultPrint("Stack: base=");
            faultPrintHex(static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(tcb->stackBase)));
            faultPrint(" size=");
            faultPrintHex(tcb->stackSize);
            faultPrint("\r\n");
        }

        // EXC_RETURN
        faultPrintReg("EXC_RETURN: ", info.excInfo);

        faultPrint("=== CRASH DUMP END ===\r\n");

        // Visual indicator + halt
        boardFaultBlink();
    }

}  // namespace kernel
