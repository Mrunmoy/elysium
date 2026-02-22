// Cortex-A9 architecture-specific kernel support.
// GIC v1 (distributor + CPU interface), SCU private timer, critical sections,
// and SGI-based context switch trigger.
//
// Register addresses are for Zynq-7000 SoC (Cortex-A9 MPCore).
// The GIC and private timer sit in the per-CPU region at 0xF8F0_0000.

#include "kernel/Arch.h"

#include <cstdint>

namespace
{
    // SCU peripheral base (Cortex-A9 MPCore)
    constexpr std::uint32_t kPeriphBase = 0xF8F00000;

    // GIC CPU interface registers
    constexpr std::uint32_t kGicCpuBase = kPeriphBase + 0x100;
    constexpr std::uint32_t kIccicr  = kGicCpuBase + 0x00;   // CPU interface control
    constexpr std::uint32_t kIccpmr  = kGicCpuBase + 0x04;   // Priority mask
    constexpr std::uint32_t kIccbpr  = kGicCpuBase + 0x08;   // Binary point
    constexpr std::uint32_t kIcciar  = kGicCpuBase + 0x0C;   // Interrupt acknowledge
    constexpr std::uint32_t kIcceoir = kGicCpuBase + 0x10;   // End of interrupt

    // GIC distributor registers
    constexpr std::uint32_t kGicDistBase = kPeriphBase + 0x1000;
    constexpr std::uint32_t kIcddcr     = kGicDistBase + 0x000;  // Distributor control
    constexpr std::uint32_t kIcdiser0   = kGicDistBase + 0x100;  // Interrupt set-enable
    constexpr std::uint32_t kIcdipr     = kGicDistBase + 0x400;  // Interrupt priority
    constexpr std::uint32_t kIcdiptr    = kGicDistBase + 0x800;  // Interrupt processor targets
    constexpr std::uint32_t kIcdicfr    = kGicDistBase + 0xC00;  // Interrupt configuration
    constexpr std::uint32_t kIcdsgir    = kGicDistBase + 0xF00;  // Software generated interrupt

    // SCU private timer registers (per-CPU)
    constexpr std::uint32_t kTimerBase = kPeriphBase + 0x600;
    constexpr std::uint32_t kTimerLoad    = kTimerBase + 0x00;
    constexpr std::uint32_t kTimerCounter = kTimerBase + 0x04;
    constexpr std::uint32_t kTimerControl = kTimerBase + 0x08;
    constexpr std::uint32_t kTimerIsr     = kTimerBase + 0x0C;

    // Timer control bits
    constexpr std::uint32_t kTimerEnable    = 1u << 0;
    constexpr std::uint32_t kTimerAutoRload = 1u << 1;
    constexpr std::uint32_t kTimerIrqEnable = 1u << 2;

    // Private timer interrupt ID
    constexpr std::uint32_t kTimerIrqId = 29;

    // SGI 0 is used as a context switch trigger (like PendSV on Cortex-M)
    constexpr std::uint32_t kSgiContextSwitch = 0;

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }

}  // namespace

namespace kernel
{
namespace arch
{
    void triggerContextSwitch()
    {
        // Generate SGI #0 targeting self (CPU0).
        // ICDSGIR format: [25:24] = target list filter (0 = use target list),
        //                 [23:16] = CPU target list (bit 0 = CPU0),
        //                 [3:0]   = SGI ID.
        reg(kIcdsgir) = (1u << 16) | kSgiContextSwitch;
    }

    void configureSysTick(std::uint32_t ticks)
    {
        // Private timer clocks at CPU_CLK / 2 (PERIPHCLK).
        // The caller passes SystemCoreClock / 1000 (based on CPU_CLK).
        // We halve it here for the private timer.
        std::uint32_t loadVal = (ticks / 2) - 1;

        // Stop timer
        reg(kTimerControl) = 0;

        // Set load value
        reg(kTimerLoad) = loadVal;

        // Clear any pending interrupt
        reg(kTimerIsr) = 1;

        // Enable: auto-reload, IRQ, start
        reg(kTimerControl) = kTimerEnable | kTimerAutoRload | kTimerIrqEnable;

        // Enable timer interrupt (ID 29) in GIC distributor
        // ID 29 is in register ICDISER0 (IDs 0-31), bit 29
        reg(kIcdiser0) |= (1u << kTimerIrqId);

        // Set priority for timer interrupt (lower value = higher priority)
        // Priority registers are byte-accessible; ID 29 is in ICDIPR[7], byte 1
        volatile std::uint8_t *priReg =
            reinterpret_cast<volatile std::uint8_t *>(kIcdipr + kTimerIrqId);
        *priReg = 0xA0;  // Priority 160 (mid-range)
    }

    void enterCritical()
    {
        __asm volatile("cpsid i" ::: "memory");
    }

    void exitCritical()
    {
        __asm volatile("cpsie i" ::: "memory");
    }

    void startFirstThread()
    {
        // Trigger SVC to launch the first thread.
        // SVC_Handler in ContextSwitch.s will load g_currentTcb,
        // restore the initial context, and switch to SYS mode.
        __asm volatile("svc 0");
    }

    void setInterruptPriorities()
    {
        // -- GIC Distributor --
        // Enable distributor (secure + non-secure)
        reg(kIcddcr) = 0x3;

        // Enable SGI 0 (context switch trigger) in distributor
        reg(kIcdiser0) |= (1u << kSgiContextSwitch);

        // Set SGI 0 to lowest usable priority (0xF0)
        volatile std::uint8_t *sgiPri =
            reinterpret_cast<volatile std::uint8_t *>(kIcdipr + kSgiContextSwitch);
        *sgiPri = 0xF0;

        // -- GIC CPU Interface --
        // Set priority mask to allow all priorities (0xFF = lowest filter)
        reg(kIccpmr) = 0xFF;

        // Set binary point to 0 (all priority bits used for preemption)
        reg(kIccbpr) = 0;

        // Enable CPU interface (secure + non-secure signaling)
        reg(kIccicr) = 0x3;
    }

    std::uint32_t initialStatusRegister()
    {
        // CPSR for new threads: SYS mode (0x1F), ARM state (T=0),
        // IRQ enabled (I=0), FIQ enabled (F=0)
        return 0x1Fu;
    }

    bool inIsrContext()
    {
        // Read CPSR mode bits (4:0).
        // USR=0x10 and SYS=0x1F are thread modes; anything else is a handler.
        std::uint32_t cpsr;
        __asm volatile("mrs %0, cpsr" : "=r"(cpsr));
        std::uint32_t mode = cpsr & 0x1Fu;
        return (mode != 0x10u) && (mode != 0x1Fu);
    }

    void waitForInterrupt()
    {
        __asm volatile("wfi" ::: "memory");
    }

    // Cortex-A9 has no SCB->SCR equivalent for sleep-on-exit or deep sleep.
    void enableSleepOnExit() {}
    void disableSleepOnExit() {}
    void enableDeepSleep() {}
    void disableDeepSleep() {}

}  // namespace arch
}  // namespace kernel

// Private timer ISR -- called by the IRQ dispatcher in ContextSwitch.s
// when interrupt ID 29 fires.
extern "C" void PrivateTimer_Handler()
{
    // Clear timer interrupt (write 1 to ISR event flag)
    *reinterpret_cast<volatile std::uint32_t *>(kTimerBase + 0x0C) = 1;
}
