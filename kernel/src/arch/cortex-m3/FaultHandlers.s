/**
 * Fault handler assembly entry points for Cortex-M3.
 *
 * All four configurable fault handlers (HardFault, MemManage, BusFault,
 * UsageFault) use the same pattern:
 *   1. Test bit 2 of EXC_RETURN (in LR) to determine MSP vs PSP
 *   2. Pass the correct stack frame pointer in R0
 *   3. Pass EXC_RETURN in R1
 *   4. Branch to faultHandlerC (defined in CrashDump.cpp)
 *
 * These are .global symbols that override the .weak defaults in Startup.s.
 * The --whole-archive linker flag (already configured) ensures these are
 * pulled in from the kernel static library.
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

/* -------------------------------------------------------------------------- */
/*  HardFault_Handler                                                         */
/* -------------------------------------------------------------------------- */

    .section .text.HardFault_Handler, "ax", %progbits
    .global HardFault_Handler
    .type HardFault_Handler, %function

HardFault_Handler:
    tst     lr, #4              /* Test bit 2 of EXC_RETURN */
    ite     eq
    mrseq   r0, msp             /* Bit 2 = 0: fault used MSP */
    mrsne   r0, psp             /* Bit 2 = 1: fault used PSP */
    mov     r1, lr              /* Pass EXC_RETURN as second arg */
    b       faultHandlerC       /* Branch to C handler */

    .size HardFault_Handler, .-HardFault_Handler

/* -------------------------------------------------------------------------- */
/*  MemManage_Handler                                                         */
/* -------------------------------------------------------------------------- */

    .section .text.MemManage_Handler, "ax", %progbits
    .global MemManage_Handler
    .type MemManage_Handler, %function

MemManage_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mov     r1, lr
    b       faultHandlerC

    .size MemManage_Handler, .-MemManage_Handler

/* -------------------------------------------------------------------------- */
/*  BusFault_Handler                                                          */
/* -------------------------------------------------------------------------- */

    .section .text.BusFault_Handler, "ax", %progbits
    .global BusFault_Handler
    .type BusFault_Handler, %function

BusFault_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mov     r1, lr
    b       faultHandlerC

    .size BusFault_Handler, .-BusFault_Handler

/* -------------------------------------------------------------------------- */
/*  UsageFault_Handler                                                        */
/* -------------------------------------------------------------------------- */

    .section .text.UsageFault_Handler, "ax", %progbits
    .global UsageFault_Handler
    .type UsageFault_Handler, %function

UsageFault_Handler:
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mov     r1, lr
    b       faultHandlerC

    .size UsageFault_Handler, .-UsageFault_Handler
