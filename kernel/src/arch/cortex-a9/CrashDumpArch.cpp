// Cortex-A9 crash dump architecture support.
//
// Implements arch-specific fault info population and fault bit decoding
// for the Cortex-A9 (ARMv7-A). Uses CP15 fault status registers instead
// of the SCB CFSR/HFSR used on Cortex-M.
//
// Cortex-A9 fault registers (accessed via MRC instructions):
//   DFSR (c5, c0, 0) -- Data Fault Status Register
//   IFSR (c5, c0, 1) -- Instruction Fault Status Register
//   DFAR (c6, c0, 0) -- Data Fault Address Register
//   IFAR (c6, c0, 2) -- Instruction Fault Address Register
//
// Stack frame layout from FaultHandlers.s:
//   [SP+0..5] = r0, r1, r2, r3, r12, LR (thread)
//   [SP+6]    = PC (return address from exception)
//   [SP+7]    = CPSR (saved by SRS instruction)
//
// The excReturn parameter is repurposed as a fault type code:
//   1 = Data Abort, 2 = Prefetch Abort, 3 = Undefined Instruction

#include "kernel/CrashDumpArch.h"
#include "CrashDumpInternal.h"

#include <cstdint>

namespace
{
    // Stack frame indices (as pushed by FaultHandlers.s)
    enum StackFrame : std::uint8_t
    {
        kR0 = 0,
        kR1 = 1,
        kR2 = 2,
        kR3 = 3,
        kR12 = 4,
        kLr = 5,
        kPc = 6,
        kCpsr = 7
    };

    // Fault type codes (passed from FaultHandlers.s in r1)
    constexpr std::uint32_t kFaultDataAbort = 1;
    constexpr std::uint32_t kFaultPrefetchAbort = 2;
    constexpr std::uint32_t kFaultUndefined = 3;

    // Read CP15 fault status registers via inline assembly
    std::uint32_t readDfsr()
    {
        std::uint32_t val;
        __asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(val));
        return val;
    }

    std::uint32_t readIfsr()
    {
        std::uint32_t val;
        __asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(val));
        return val;
    }

    std::uint32_t readDfar()
    {
        std::uint32_t val;
        __asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(val));
        return val;
    }

    std::uint32_t readIfar()
    {
        std::uint32_t val;
        __asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(val));
        return val;
    }

    const char *faultTypeName(std::uint32_t faultCode)
    {
        switch (faultCode)
        {
        case kFaultDataAbort:
            return "DataAbort";
        case kFaultPrefetchAbort:
            return "PrefetchAbort";
        case kFaultUndefined:
            return "UndefinedInstruction";
        default:
            return "Unknown";
        }
    }

    // Decode DFSR/IFSR fault status bits (bits [10, 3:0] form a 5-bit status)
    void decodeFaultStatus(std::uint32_t fsr, const char *prefix)
    {
        std::uint32_t status = (fsr & 0xF) | ((fsr >> 6) & 0x10);

        kernel::faultPrint(prefix);

        switch (status)
        {
        case 0x01:
            kernel::faultPrint("Alignment fault\r\n");
            break;
        case 0x04:
            kernel::faultPrint("Instruction cache maintenance fault\r\n");
            break;
        case 0x05:
            kernel::faultPrint("Translation fault (section)\r\n");
            break;
        case 0x07:
            kernel::faultPrint("Translation fault (page)\r\n");
            break;
        case 0x03:
            kernel::faultPrint("Access flag fault (section)\r\n");
            break;
        case 0x06:
            kernel::faultPrint("Access flag fault (page)\r\n");
            break;
        case 0x09:
            kernel::faultPrint("Domain fault (section)\r\n");
            break;
        case 0x0B:
            kernel::faultPrint("Domain fault (page)\r\n");
            break;
        case 0x0D:
            kernel::faultPrint("Permission fault (section)\r\n");
            break;
        case 0x0F:
            kernel::faultPrint("Permission fault (page)\r\n");
            break;
        case 0x08:
            kernel::faultPrint("Synchronous external abort\r\n");
            break;
        case 0x16:
            kernel::faultPrint("Asynchronous external abort\r\n");
            break;
        case 0x19:
            kernel::faultPrint("Synchronous parity error\r\n");
            break;
        default:
            kernel::faultPrint("Status code ");
            kernel::faultPrintHex(status);
            kernel::faultPrint("\r\n");
            break;
        }
    }

}  // namespace

namespace kernel
{
    void archPopulateFaultInfo(FaultInfo &info, std::uint32_t *stackFrame,
                               std::uint32_t excReturn)
    {
        // Read stacked registers
        info.r0 = stackFrame[kR0];
        info.r1 = stackFrame[kR1];
        info.r2 = stackFrame[kR2];
        info.r3 = stackFrame[kR3];
        info.r12 = stackFrame[kR12];
        info.lr = stackFrame[kLr];
        info.pc = stackFrame[kPc];
        info.statusReg = stackFrame[kCpsr];
        info.sp = reinterpret_cast<std::uint32_t>(stackFrame);

        // Read CP15 fault status and address registers
        info.faultReg[0] = readDfsr();
        info.faultReg[1] = readIfsr();
        info.faultReg[2] = readDfar();
        info.faultReg[3] = readIfar();

        info.faultRegNames[0] = "DFSR : ";
        info.faultRegNames[1] = "IFSR : ";
        info.faultRegNames[2] = "DFAR : ";
        info.faultRegNames[3] = "IFAR : ";

        info.faultType = faultTypeName(excReturn);
        info.excInfo = excReturn;
    }

    void archDecodeFaultBits(const FaultInfo &info)
    {
        if (info.excInfo == kFaultDataAbort)
        {
            decodeFaultStatus(info.faultReg[0], "  -> DFSR: ");
            faultPrint("  -> DFAR: ");
            faultPrintHex(info.faultReg[2]);
            faultPrint("\r\n");
        }
        else if (info.excInfo == kFaultPrefetchAbort)
        {
            decodeFaultStatus(info.faultReg[1], "  -> IFSR: ");
            faultPrint("  -> IFAR: ");
            faultPrintHex(info.faultReg[3]);
            faultPrint("\r\n");
        }
        else if (info.excInfo == kFaultUndefined)
        {
            faultPrint("  -> Undefined instruction at PC\r\n");
        }
    }

    void archCrashDumpInit()
    {
        // On Cortex-A9, data abort and prefetch abort are always enabled.
        // Undefined instruction exceptions are always enabled.
        // No additional hardware configuration needed (unlike Cortex-M
        // where SHCSR enables individual fault handlers).
    }

    void archTriggerTestFault(FaultType type)
    {
        switch (type)
        {
        case FaultType::DivideByZero:
        {
            // ARM does not trap integer divide-by-zero.
            // Trigger an undefined instruction instead.
            __asm volatile(".word 0xE7F000F0");  // UDF #0 (permanently undefined)
            break;
        }
        case FaultType::InvalidMemory:
        {
            volatile std::uint32_t *bad =
                reinterpret_cast<volatile std::uint32_t *>(0xCCCCCCCC);
            *bad = 0xDEADBEEF;
            break;
        }
        case FaultType::UndefinedInstruction:
        {
            __asm volatile(".word 0xE7F000F0");  // UDF #0
            break;
        }
        }
    }

}  // namespace kernel
