// Cortex-M4 architecture-specific kernel support.
// NVIC priority configuration, SysTick setup, critical sections, PendSV trigger.
//
// All SCB, SysTick, and NVIC registers are at identical addresses across
// Cortex-M3 and Cortex-M4 (part of the ARMv7-M architecture).

#include "kernel/CortexM.h"

#include <cstdint>

namespace
{
    // System Control Block (SCB) registers
    constexpr std::uint32_t kScbBase = 0xE000ED00;
    constexpr std::uint32_t kScbShpr2 = kScbBase + 0x1C;  // SVCall priority
    constexpr std::uint32_t kScbShpr3 = kScbBase + 0x20;  // PendSV + SysTick priority
    constexpr std::uint32_t kScbIcsr = kScbBase + 0x04;    // Interrupt control state

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
        // Cortex-M4 priority: lower number = higher priority
        // PendSV: 0xFF (lowest possible, ensures context switch after all ISRs)
        // SysTick: 0xFE (just above PendSV)
        //
        // SHPR3 layout: [31:24] = SysTick, [23:16] = PendSV
        reg(kScbShpr3) = (0xFEU << 24) | (0xFFU << 16);
    }

}  // namespace arch
}  // namespace kernel
