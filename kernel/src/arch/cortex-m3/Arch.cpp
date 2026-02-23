// Cortex-M3 architecture-specific kernel support.
// NVIC priority configuration, SysTick setup, critical sections, PendSV trigger.

#include "kernel/Arch.h"

#include <cstdint>

namespace
{
    // System Control Block (SCB) registers
    constexpr std::uint32_t kScbBase = 0xE000ED00;
    constexpr std::uint32_t kScbShpr2 = kScbBase + 0x1C;  // SVCall priority
    constexpr std::uint32_t kScbShpr3 = kScbBase + 0x20;  // PendSV + SysTick priority
    constexpr std::uint32_t kScbIcsr = kScbBase + 0x04;    // Interrupt control state
    constexpr std::uint32_t kScbScr = kScbBase + 0x10;     // System control register

    // SCR bits
    constexpr std::uint32_t kScrSleepOnExit = 1U << 1;
    constexpr std::uint32_t kScrSleepDeep = 1U << 2;

    // SysTick registers
    constexpr std::uint32_t kSysTickCtrl = 0xE000E010;
    constexpr std::uint32_t kSysTickLoad = 0xE000E014;
    constexpr std::uint32_t kSysTickVal = 0xE000E018;

    // ICSR bits
    constexpr std::uint32_t kIcsrPendSvSet = 1U << 28;

    // SysTick CTRL bits
    constexpr std::uint32_t kSysTickEnable = 1U << 0;
    constexpr std::uint32_t kSysTickTickInt = 1U << 1;
    constexpr std::uint32_t kSysTickClkSrc = 1U << 2;  // Processor clock

    volatile std::uint32_t &reg(std::uint32_t addr)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(addr);
    }
}  // namespace

namespace kernel
{
// Syscall flag: set by svcDispatch to indicate that kernel functions
// (sleep, messageSend, etc.) should treat this as thread context, not ISR.
volatile bool g_inSyscall = false;
namespace arch
{
    void triggerContextSwitch()
    {
        // Set PendSV pending bit in SCB->ICSR
        reg(kScbIcsr) = kIcsrPendSvSet;
    }

    void configureSysTick(std::uint32_t ticks)
    {
        reg(kSysTickLoad) = ticks - 1;
        reg(kSysTickVal) = 0;
        reg(kSysTickCtrl) = kSysTickEnable | kSysTickTickInt | kSysTickClkSrc;
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
        // Trigger SVC to launch the first thread
        // SVC_Handler will load g_currentTcb, restore context, and switch to PSP
        __asm volatile("svc 0");

        // Should never reach here -- SVC_Handler returns to the first thread
    }

    void setInterruptPriorities()
    {
        // Cortex-M3 priority: lower number = higher priority
        // PendSV: 0xFF (lowest possible, ensures context switch after all ISRs)
        // SysTick: 0xFE (just above PendSV)
        //
        // SHPR3 layout: [31:24] = SysTick, [23:16] = PendSV
        reg(kScbShpr3) = (0xFEU << 24) | (0xFFU << 16);
    }

    std::uint32_t initialStatusRegister()
    {
        return 0x01000000u;    // xPSR: Thumb bit set
    }

    bool inIsrContext()
    {
        // During SVC dispatch, we are in handler mode but semantically acting
        // on behalf of a thread. Return false so kernel functions can block.
        if (g_inSyscall)
        {
            return false;
        }

        // ICSR bits 8:0 (VECTACTIVE) hold the active exception number.
        // Non-zero means we are in an exception handler.
        return (reg(kScbIcsr) & 0x1FFu) != 0;
    }

    void waitForInterrupt()
    {
        __asm volatile("wfi" ::: "memory");
    }

    void enableSleepOnExit()
    {
        reg(kScbScr) |= kScrSleepOnExit;
    }

    void disableSleepOnExit()
    {
        reg(kScbScr) &= ~kScrSleepOnExit;
    }

    void enableDeepSleep()
    {
        reg(kScbScr) |= kScrSleepDeep;
    }

    void disableDeepSleep()
    {
        reg(kScbScr) &= ~kScrSleepDeep;
    }

}  // namespace arch
}  // namespace kernel
