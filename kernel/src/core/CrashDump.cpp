// Crash dump handler for Cortex-M3 faults.
//
// Called from FaultHandlers.s when HardFault, MemManage, BusFault, or
// UsageFault occurs. Extracts the hardware-pushed register frame, reads
// SCB fault status registers, identifies the faulting thread, and outputs
// a structured crash dump via polled UART.
//
// Constraints: no heap, no exceptions, no RTOS calls, no interrupts.
// All output uses polling UART (safe in any fault context).

#include "kernel/CrashDump.h"
#include "kernel/CortexM.h"
#include "kernel/Thread.h"

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

    // USART1 registers for exception-safe init
    constexpr std::uint32_t kUsart1Base = 0x40011000;
    constexpr std::uint32_t kUsart1Sr = kUsart1Base + 0x00;
    constexpr std::uint32_t kUsart1Dr = kUsart1Base + 0x04;
    constexpr std::uint32_t kUsart1Brr = kUsart1Base + 0x08;
    constexpr std::uint32_t kUsart1Cr1 = kUsart1Base + 0x0C;

    // USART1 CR1 bits
    constexpr std::uint32_t kUsartUe = 1U << 13;
    constexpr std::uint32_t kUsartTe = 1U << 3;

    // USART1 SR bits
    constexpr std::uint32_t kUsartTxe = 1U << 7;
    constexpr std::uint32_t kUsartTc = 1U << 6;

    // RCC register for enabling USART1 and GPIOA clocks
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kRccAhb1enr = kRccBase + 0x30;
    constexpr std::uint32_t kRccApb2enr = kRccBase + 0x44;

    // GPIO registers for PA9 AF7 config
    constexpr std::uint32_t kGpioaBase = 0x40020000;
    constexpr std::uint32_t kGpioaModer = kGpioaBase + 0x00;
    constexpr std::uint32_t kGpioaOspeedr = kGpioaBase + 0x08;
    constexpr std::uint32_t kGpioaAfrh = kGpioaBase + 0x24;

    // GPIOC registers for LED on PC13
    constexpr std::uint32_t kGpiocBase = 0x40020800;
    constexpr std::uint32_t kGpiocModer = kGpiocBase + 0x00;
    constexpr std::uint32_t kGpiocOdr = kGpiocBase + 0x14;

    // Stack frame indices (hardware-pushed by Cortex-M3 on exception entry)
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

    // Exception-safe UART initialization.
    // Initializes USART1 via direct register writes if not already enabled.
    // No HAL calls, no RTOS, no interrupts -- safe in any fault context.
    void ensureUartReady()
    {
        // Check if USART1 is already enabled
        if (reg(kUsart1Cr1) & kUsartUe)
        {
            return;
        }

        // Enable GPIOA clock (bit 0 of AHB1ENR)
        reg(kRccAhb1enr) |= (1U << 0);

        // Enable USART1 clock (bit 4 of APB2ENR)
        reg(kRccApb2enr) |= (1U << 4);

        // Configure PA9 as AF7 (USART1_TX)
        // MODER: bits 19:18 = 10 (alternate function)
        std::uint32_t moder = reg(kGpioaModer);
        moder &= ~(3U << 18);
        moder |= (2U << 18);
        reg(kGpioaModer) = moder;

        // OSPEEDR: bits 19:18 = 11 (very high speed)
        std::uint32_t ospeedr = reg(kGpioaOspeedr);
        ospeedr |= (3U << 18);
        reg(kGpioaOspeedr) = ospeedr;

        // AFRH: bits 7:4 = 0111 (AF7 for PA9, pin 9 is AFRH bit group 1)
        std::uint32_t afrh = reg(kGpioaAfrh);
        afrh &= ~(0xFU << 4);
        afrh |= (7U << 4);
        reg(kGpioaAfrh) = afrh;

        // Configure USART1: 115200 baud at APB2 = 60 MHz
        reg(kUsart1Cr1) = 0;
        reg(kUsart1Brr) = 60000000U / 115200U;  // = 520 (0x208)
        reg(kUsart1Cr1) = kUsartUe | kUsartTe;
    }

    // Blocking single-character UART output (polling, no interrupts).
    void faultPutChar(char c)
    {
        while (!(reg(kUsart1Sr) & kUsartTxe))
        {
        }
        reg(kUsart1Dr) = static_cast<std::uint32_t>(c);
    }

    // Blocking string output.
    void faultPrint(const char *str)
    {
        while (*str)
        {
            faultPutChar(*str++);
        }
        // Wait for transmission complete
        while (!(reg(kUsart1Sr) & kUsartTc))
        {
        }
    }

    // Print a 32-bit value as 8-char uppercase hex.
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

    // Print "  label: HEXVALUE\r\n"
    void faultPrintReg(const char *label, std::uint32_t value)
    {
        faultPrint("  ");
        faultPrint(label);
        faultPrintHex(value);
        faultPrint("\r\n");
    }

    // Decode and print CFSR fault bits.
    void decodeCfsr(std::uint32_t cfsr, std::uint32_t mmfar, std::uint32_t bfar)
    {
        // MemManage faults
        if (cfsr & kMmIaccviol)
        {
            faultPrint("  -> IACCVIOL: Instruction access violation\r\n");
        }
        if (cfsr & kMmDaccviol)
        {
            faultPrint("  -> DACCVIOL: Data access violation\r\n");
        }
        if (cfsr & kMmMunstkerr)
        {
            faultPrint("  -> MUNSTKERR: Unstacking error (MemManage)\r\n");
        }
        if (cfsr & kMmMstkerr)
        {
            faultPrint("  -> MSTKERR: Stacking error (MemManage)\r\n");
        }
        if (cfsr & kMmMmarvalid)
        {
            faultPrint("  -> MMARVALID: MMFAR = ");
            faultPrintHex(mmfar);
            faultPrint("\r\n");
        }

        // BusFault
        if (cfsr & kBfIbuserr)
        {
            faultPrint("  -> IBUSERR: Instruction bus error\r\n");
        }
        if (cfsr & kBfPreciserr)
        {
            faultPrint("  -> PRECISERR: Precise data bus error\r\n");
        }
        if (cfsr & kBfImpreciserr)
        {
            faultPrint("  -> IMPRECISERR: Imprecise data bus error\r\n");
        }
        if (cfsr & kBfUnstkerr)
        {
            faultPrint("  -> UNSTKERR: Unstacking error (BusFault)\r\n");
        }
        if (cfsr & kBfStkerr)
        {
            faultPrint("  -> STKERR: Stacking error (BusFault)\r\n");
        }
        if (cfsr & kBfBfarvalid)
        {
            faultPrint("  -> BFARVALID: BFAR = ");
            faultPrintHex(bfar);
            faultPrint("\r\n");
        }

        // UsageFault
        if (cfsr & kUfUndefinstr)
        {
            faultPrint("  -> UNDEFINSTR: Undefined instruction\r\n");
        }
        if (cfsr & kUfInvstate)
        {
            faultPrint("  -> INVSTATE: Invalid EPSR.T bit (ARM mode on Thumb-only CPU)\r\n");
        }
        if (cfsr & kUfInvpc)
        {
            faultPrint("  -> INVPC: Invalid EXC_RETURN\r\n");
        }
        if (cfsr & kUfNocp)
        {
            faultPrint("  -> NOCP: Coprocessor access\r\n");
        }
        if (cfsr & kUfUnaligned)
        {
            faultPrint("  -> UNALIGNED: Unaligned memory access\r\n");
        }
        if (cfsr & kUfDivbyzero)
        {
            faultPrint("  -> DIVBYZERO: Divide by zero\r\n");
        }
    }

    // Decode and print HFSR bits.
    void decodeHfsr(std::uint32_t hfsr)
    {
        if (hfsr & kHfVecttbl)
        {
            faultPrint("  -> VECTTBL: Vector table read error\r\n");
        }
        if (hfsr & kHfForced)
        {
            faultPrint("  -> FORCED: Escalated to HardFault\r\n");
        }
    }

    // Determine fault type name from IPSR or HFSR.
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

    // Simple busy-wait delay (approximate).
    void faultDelayMs(std::uint32_t ms)
    {
        // At 120 MHz, ~120000 cycles per ms. Inner loop ~4 cycles.
        for (std::uint32_t i = 0; i < ms; ++i)
        {
            for (volatile std::uint32_t j = 0; j < 30000; ++j)
            {
            }
        }
    }

    // Blink LED on PC13 to indicate fault.
    void faultBlinkLed()
    {
        // Ensure GPIOC clock is enabled
        reg(kRccAhb1enr) |= (1U << 2);

        // Configure PC13 as output
        std::uint32_t moder = reg(kGpiocModer);
        moder &= ~(3U << 26);
        moder |= (1U << 26);
        reg(kGpiocModer) = moder;

        // Blink indefinitely (fast blink = fault indicator)
        while (true)
        {
            reg(kGpiocOdr) ^= (1U << 13);
            faultDelayMs(100);
        }
    }

}  // namespace

namespace kernel
{
    void crashDumpInit()
    {
        // Enable MemManage, BusFault, and UsageFault handlers.
        // Without this, all faults escalate to HardFault.
        reg(kScbShcsr) |= kShcsrMemFaultEna | kShcsrBusFaultEna | kShcsrUsageFaultEna;

        // Enable divide-by-zero and unaligned-access traps.
        reg(kScbCcr) |= kCcrDiv0Trp | kCcrUnalignTrp;
    }

    void triggerTestFault(FaultType type)
    {
        switch (type)
        {
        case FaultType::DivideByZero:
        {
            volatile int zero = 0;
            volatile int result = 1 / zero;
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

    extern "C" void faultHandlerC(std::uint32_t *stackFrame, std::uint32_t excReturn)
    {
        // Ensure UART is ready (may init from scratch if needed)
        ensureUartReady();

        // Read stacked registers
        std::uint32_t r0 = stackFrame[kR0];
        std::uint32_t r1 = stackFrame[kR1];
        std::uint32_t r2 = stackFrame[kR2];
        std::uint32_t r3 = stackFrame[kR3];
        std::uint32_t r12 = stackFrame[kR12];
        std::uint32_t lr = stackFrame[kLr];
        std::uint32_t pc = stackFrame[kPc];
        std::uint32_t xpsr = stackFrame[kXpsr];

        // Read SCB fault status registers
        std::uint32_t cfsr = reg(kScbCfsr);
        std::uint32_t hfsr = reg(kScbHfsr);
        std::uint32_t mmfar = reg(kScbMmfar);
        std::uint32_t bfar = reg(kScbBfar);

        // Read thread context
        ThreadControlBlock *tcb = g_currentTcb;

        // --- Output structured crash dump ---

        faultPrint("\r\n=== CRASH DUMP BEGIN ===\r\n");

        // Fault type
        faultPrint("Fault: ");
        faultPrint(faultTypeName(xpsr));
        faultPrint("\r\n");

        // Thread context
        faultPrint("Thread: ");
        if (tcb != nullptr && tcb->m_name != nullptr)
        {
            faultPrint(tcb->m_name);
            faultPrint(" (id=");
            // Print thread ID as single digit (IDs are 0-7)
            char idBuf[2];
            idBuf[0] = '0' + tcb->m_id;
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
        faultPrintReg("PC  : ", pc);
        faultPrintReg("LR  : ", lr);
        faultPrintReg("SP  : ", reinterpret_cast<std::uint32_t>(stackFrame));
        faultPrintReg("R0  : ", r0);
        faultPrintReg("R1  : ", r1);
        faultPrintReg("R2  : ", r2);
        faultPrintReg("R3  : ", r3);
        faultPrintReg("R12 : ", r12);
        faultPrintReg("xPSR: ", xpsr);

        // Fault status registers
        faultPrint("Fault Status:\r\n");
        faultPrintReg("CFSR : ", cfsr);
        faultPrintReg("HFSR : ", hfsr);
        faultPrintReg("MMFAR: ", mmfar);
        faultPrintReg("BFAR : ", bfar);

        // Decoded fault bits
        faultPrint("Decoded:\r\n");
        decodeHfsr(hfsr);
        decodeCfsr(cfsr, mmfar, bfar);

        // Stack info
        if (tcb != nullptr)
        {
            faultPrint("Stack: base=");
            faultPrintHex(reinterpret_cast<std::uint32_t>(tcb->m_stackBase));
            faultPrint(" size=");
            faultPrintHex(tcb->m_stackSize);
            faultPrint("\r\n");
        }

        // EXC_RETURN
        faultPrintReg("EXC_RETURN: ", excReturn);

        faultPrint("=== CRASH DUMP END ===\r\n");

        // Visual indicator + halt
        faultBlinkLed();
    }

}  // namespace kernel
