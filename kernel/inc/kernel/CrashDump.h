// Crash dump system for Cortex-M3 fault handling.
//
// Captures register state, fault status registers, and thread context
// when a fault occurs, then outputs a structured crash dump via UART.
//
// Call crashDumpInit() during kernel initialization to enable configurable
// fault handlers (MemManage, BusFault, UsageFault) and trap settings.

#pragma once

#include <cstdint>

namespace kernel
{
    // Enable configurable fault handlers and trap settings.
    // Must be called before threads start (typically from kernel::init()).
    void crashDumpInit();

    // Test fault types for on-target crash dump verification.
    enum class FaultType : std::uint8_t
    {
        DivideByZero,
        InvalidMemory,
        UndefinedInstruction
    };

    // Trigger a test fault to exercise the crash dump system.
    // WARNING: This will crash the system -- use only for testing.
    void triggerTestFault(FaultType type);

    extern "C"
    {
        // C handler called from FaultHandlers.s assembly stubs.
        // stackFrame points to the hardware-pushed exception frame on MSP or PSP.
        // excReturn is the EXC_RETURN value from LR at exception entry.
        void faultHandlerC(std::uint32_t *stackFrame, std::uint32_t excReturn);
    }

}  // namespace kernel
