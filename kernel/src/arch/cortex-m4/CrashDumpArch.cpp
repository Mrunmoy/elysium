// Cortex-M3/M4 crash dump architecture support.
//
// Implements arch-specific fault info population and fault bit decoding
// for ARMv7-M processors. Both Cortex-M3 and M4 share the same SCB
// registers, fault model, and hardware stack frame layout.

#include "kernel/CrashDumpArch.h"
#include "CrashDumpInternal.h"

#include <cstdint>

namespace
{
    // SCB fault status registers
    constexpr std::uint32_t kScbCfsr = 0xE000ED28;   // Configurable Fault Status
    constexpr std::uint32_t kScbHfsr = 0xE000ED2C;   // HardFault Status
    constexpr std::uint32_t kScbMmfar = 0xE000ED34;  // MemManage Fault Address
    constexpr std::uint32_t kScbBfar = 0xE000ED38;   // BusFault Address
    constexpr std::uint32_t kScbShcsr = 0xE000ED24;  // System Handler Control and State
    constexpr std::uint32_t kScbCcr = 0xE000ED14;    // Configuration and Control

    // SHCSR bits for enabling fault handlers
    constexpr std::uint32_t kShcsrMemFaultEna = 1U << 16;
    constexpr std::uint32_t kShcsrBusFaultEna = 1U << 17;
    constexpr std::uint32_t kShcsrUsageFaultEna = 1U << 18;

    // CCR trap bits
    constexpr std::uint32_t kCcrDiv0Trp = 1U << 4;
    constexpr std::uint32_t kCcrUnalignTrp = 1U << 3;

    // CFSR bit masks -- MemManage (bits 0-7)
    constexpr std::uint32_t kMmIaccviol = 1U << 0;
    constexpr std::uint32_t kMmDaccviol = 1U << 1;
    constexpr std::uint32_t kMmMunstkerr = 1U << 3;
    constexpr std::uint32_t kMmMstkerr = 1U << 4;
    constexpr std::uint32_t kMmMmarvalid = 1U << 7;

    // CFSR bit masks -- BusFault (bits 8-15)
    constexpr std::uint32_t kBfIbuserr = 1U << 8;
    constexpr std::uint32_t kBfPreciserr = 1U << 9;
    constexpr std::uint32_t kBfImpreciserr = 1U << 10;
    constexpr std::uint32_t kBfUnstkerr = 1U << 11;
    constexpr std::uint32_t kBfStkerr = 1U << 12;
    constexpr std::uint32_t kBfBfarvalid = 1U << 15;

    // CFSR bit masks -- UsageFault (bits 16-31)
    constexpr std::uint32_t kUfUndefinstr = 1U << 16;
    constexpr std::uint32_t kUfInvstate = 1U << 17;
    constexpr std::uint32_t kUfInvpc = 1U << 18;
    constexpr std::uint32_t kUfNocp = 1U << 19;
    constexpr std::uint32_t kUfUnaligned = 1U << 24;
    constexpr std::uint32_t kUfDivbyzero = 1U << 25;

    // HFSR bit masks
    constexpr std::uint32_t kHfVecttbl = 1U << 1;
    constexpr std::uint32_t kHfForced = 1U << 30;

    // Stack frame indices (hardware-pushed by Cortex-M on exception entry)
    enum StackFrame : std::uint8_t
    {
        kR0 = 0,
        kR1 = 1,
        kR2 = 2,
        kR3 = 3,
        kR12 = 4,
        kLr = 5,
        kPc = 6,
        kXpsr = 7
    };

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

    // Decode and print CFSR fault bits.
    void decodeCfsr(std::uint32_t cfsr, std::uint32_t mmfar, std::uint32_t bfar)
    {
        // MemManage faults
        if (cfsr & kMmIaccviol)
        {
            kernel::faultPrint("  -> IACCVIOL: Instruction access violation\r\n");
        }
        if (cfsr & kMmDaccviol)
        {
            kernel::faultPrint("  -> DACCVIOL: Data access violation\r\n");
        }
        if (cfsr & kMmMunstkerr)
        {
            kernel::faultPrint("  -> MUNSTKERR: Unstacking error (MemManage)\r\n");
        }
        if (cfsr & kMmMstkerr)
        {
            kernel::faultPrint("  -> MSTKERR: Stacking error (MemManage)\r\n");
        }
        if (cfsr & kMmMmarvalid)
        {
            kernel::faultPrint("  -> MMARVALID: MMFAR = ");
            kernel::faultPrintHex(mmfar);
            kernel::faultPrint("\r\n");
        }

        // BusFault
        if (cfsr & kBfIbuserr)
        {
            kernel::faultPrint("  -> IBUSERR: Instruction bus error\r\n");
        }
        if (cfsr & kBfPreciserr)
        {
            kernel::faultPrint("  -> PRECISERR: Precise data bus error\r\n");
        }
        if (cfsr & kBfImpreciserr)
        {
            kernel::faultPrint("  -> IMPRECISERR: Imprecise data bus error\r\n");
        }
        if (cfsr & kBfUnstkerr)
        {
            kernel::faultPrint("  -> UNSTKERR: Unstacking error (BusFault)\r\n");
        }
        if (cfsr & kBfStkerr)
        {
            kernel::faultPrint("  -> STKERR: Stacking error (BusFault)\r\n");
        }
        if (cfsr & kBfBfarvalid)
        {
            kernel::faultPrint("  -> BFARVALID: BFAR = ");
            kernel::faultPrintHex(bfar);
            kernel::faultPrint("\r\n");
        }

        // UsageFault
        if (cfsr & kUfUndefinstr)
        {
            kernel::faultPrint("  -> UNDEFINSTR: Undefined instruction\r\n");
        }
        if (cfsr & kUfInvstate)
        {
            kernel::faultPrint("  -> INVSTATE: Invalid EPSR.T bit (ARM mode on Thumb-only CPU)\r\n");
        }
        if (cfsr & kUfInvpc)
        {
            kernel::faultPrint("  -> INVPC: Invalid EXC_RETURN\r\n");
        }
        if (cfsr & kUfNocp)
        {
            kernel::faultPrint("  -> NOCP: Coprocessor access\r\n");
        }
        if (cfsr & kUfUnaligned)
        {
            kernel::faultPrint("  -> UNALIGNED: Unaligned memory access\r\n");
        }
        if (cfsr & kUfDivbyzero)
        {
            kernel::faultPrint("  -> DIVBYZERO: Divide by zero\r\n");
        }
    }

    // Decode and print HFSR bits.
    void decodeHfsr(std::uint32_t hfsr)
    {
        if (hfsr & kHfVecttbl)
        {
            kernel::faultPrint("  -> VECTTBL: Vector table read error\r\n");
        }
        if (hfsr & kHfForced)
        {
            kernel::faultPrint("  -> FORCED: Escalated to HardFault\r\n");
        }
    }

    // Determine fault type name from IPSR field of xPSR.
    const char *faultTypeName(std::uint32_t xpsr)
    {
        std::uint32_t ipsr = xpsr & 0xFF;

        switch (ipsr)
        {
        case 3:
            return "HardFault";
        case 4:
            return "MemManage";
        case 5:
            return "BusFault";
        case 6:
            return "UsageFault";
        default:
            return "Unknown";
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
        info.statusReg = stackFrame[kXpsr];
        info.sp = reinterpret_cast<std::uint32_t>(stackFrame);

        // Read SCB fault status registers
        info.faultReg[0] = reg(kScbCfsr);
        info.faultReg[1] = reg(kScbHfsr);
        info.faultReg[2] = reg(kScbMmfar);
        info.faultReg[3] = reg(kScbBfar);

        info.faultRegNames[0] = "CFSR : ";
        info.faultRegNames[1] = "HFSR : ";
        info.faultRegNames[2] = "MMFAR: ";
        info.faultRegNames[3] = "BFAR : ";

        info.faultType = faultTypeName(info.statusReg);
        info.excInfo = excReturn;
    }

    void archDecodeFaultBits(const FaultInfo &info)
    {
        decodeHfsr(info.faultReg[1]);
        decodeCfsr(info.faultReg[0], info.faultReg[2], info.faultReg[3]);
    }

    void archCrashDumpInit()
    {
        // Enable MemManage, BusFault, and UsageFault handlers.
        // Without this, all faults escalate to HardFault.
        reg(kScbShcsr) |= kShcsrMemFaultEna | kShcsrBusFaultEna | kShcsrUsageFaultEna;

        // Enable divide-by-zero and unaligned-access traps.
        reg(kScbCcr) |= kCcrDiv0Trp | kCcrUnalignTrp;
    }

    void archTriggerTestFault(FaultType type)
    {
        switch (type)
        {
        case FaultType::DivideByZero:
        {
            // Use inline assembly to prevent the compiler from optimizing
            // away the division (integer divide-by-zero is UB in C++).
            std::uint32_t one = 1;
            std::uint32_t zero = 0;
            std::uint32_t result;
            __asm volatile("udiv %0, %1, %2"
                           : "=r"(result)
                           : "r"(one), "r"(zero));
            (void)result;
            break;
        }
        case FaultType::InvalidMemory:
        {
            volatile std::uint32_t *bad = reinterpret_cast<volatile std::uint32_t *>(0xCCCCCCCC);
            *bad = 0xDEADBEEF;
            break;
        }
        case FaultType::UndefinedInstruction:
        {
            // Execute an undefined instruction (Thumb permanently undefined encoding)
            __asm volatile(".short 0xDEFE");
            break;
        }
        }
    }

}  // namespace kernel
