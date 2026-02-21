/**
 * Fault handler assembly entry points for Cortex-A9 (ARMv7-A).
 *
 * Data Abort, Prefetch Abort, and Undefined Instruction handlers
 * save the exception stack frame and call faultHandlerC (defined
 * in CrashDumpCommon.cpp) with:
 *   r0 = pointer to stacked registers (r0-r3, r12, LR, PC, CPSR)
 *   r1 = exception type indicator (used as excReturn on Cortex-M,
 *         repurposed as fault type code on Cortex-A9)
 *
 * Fault type codes (passed in r1):
 *   1 = Data Abort
 *   2 = Prefetch Abort
 *   3 = Undefined Instruction
 *
 * These are .global symbols that override the .weak defaults in Startup.s.
 */

    .syntax unified
    .cpu cortex-a9
    .arm

/* -------------------------------------------------------------------------- */
/*  DataAbort_Handler                                                         */
/* -------------------------------------------------------------------------- */

    .section .text.DataAbort_Handler, "ax", %progbits
    .global DataAbort_Handler
    .type DataAbort_Handler, %function

DataAbort_Handler:
    /* On data abort, LR = fault address + 8 */
    sub     lr, lr, #8

    /* Switch to SYS mode to access thread stack */
    srsdb   sp!, #0x1F
    cps     #0x1F

    /* Save registers that form the exception frame */
    push    {r0-r3, r12, lr}

    /* r0 = stack frame pointer, r1 = fault type */
    mov     r0, sp
    mov     r1, #1              /* 1 = Data Abort */
    b       faultHandlerC

    .size DataAbort_Handler, .-DataAbort_Handler

/* -------------------------------------------------------------------------- */
/*  PrefetchAbort_Handler                                                     */
/* -------------------------------------------------------------------------- */

    .section .text.PrefetchAbort_Handler, "ax", %progbits
    .global PrefetchAbort_Handler
    .type PrefetchAbort_Handler, %function

PrefetchAbort_Handler:
    /* On prefetch abort, LR = fault address + 4 */
    sub     lr, lr, #4

    srsdb   sp!, #0x1F
    cps     #0x1F

    push    {r0-r3, r12, lr}

    mov     r0, sp
    mov     r1, #2              /* 2 = Prefetch Abort */
    b       faultHandlerC

    .size PrefetchAbort_Handler, .-PrefetchAbort_Handler

/* -------------------------------------------------------------------------- */
/*  Undefined_Handler                                                         */
/* -------------------------------------------------------------------------- */

    .section .text.Undefined_Handler, "ax", %progbits
    .global Undefined_Handler
    .type Undefined_Handler, %function

Undefined_Handler:
    /* On undefined instruction, LR = faulting instruction + 4 (ARM)
     * or + 2 (Thumb). We don't adjust since the PC in the frame should
     * point to the faulting instruction for the crash dump. */
    sub     lr, lr, #4

    srsdb   sp!, #0x1F
    cps     #0x1F

    push    {r0-r3, r12, lr}

    mov     r0, sp
    mov     r1, #3              /* 3 = Undefined Instruction */
    b       faultHandlerC

    .size Undefined_Handler, .-Undefined_Handler
