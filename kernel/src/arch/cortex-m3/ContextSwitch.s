/**
 * Context switching assembly for Cortex-M3 (ARMv7-M, Thumb-2).
 *
 * PendSV_Handler:  Performs context switch between threads.
 * SVC_Handler:     Launches the very first thread (no outgoing context to save).
 *
 * Stack frame layout per thread (16 words = 64 bytes):
 *   [SP+0  .. SP+7]   r4-r11      (software-saved by PendSV)
 *   [SP+8  .. SP+15]  r0,r1,r2,r3,r12,LR,PC,xPSR  (hardware-saved on exception entry)
 *
 * Global pointers (defined in Kernel.cpp, extern "C"):
 *   g_currentTcb  -- TCB of currently running thread
 *   g_nextTcb     -- TCB of thread to switch to
 *
 * TCB layout: m_stackPointer is at offset 0 (first field).
 * MPU fields: mpuStackRbar at offset 36, mpuStackRasr at offset 40.
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

/* TCB offsets for MPU stack region fields */
    .equ OFFSET_MPU_RBAR, 36
    .equ OFFSET_MPU_RASR, 40

/* -------------------------------------------------------------------------- */
/*  PendSV_Handler -- context switch                                          */
/* -------------------------------------------------------------------------- */

    .section .text.PendSV_Handler, "ax", %progbits
    .global PendSV_Handler
    .type PendSV_Handler, %function

PendSV_Handler:
    /* Disable interrupts during context switch */
    cpsid   i

    /* Get current PSP (thread stack pointer of outgoing thread) */
    mrs     r0, psp

    /* Save r4-r11 onto the outgoing thread's stack */
    stmdb   r0!, {r4-r11}

    /* Store the updated PSP into g_currentTcb->m_stackPointer */
    ldr     r1, =g_currentTcb
    ldr     r2, [r1]            /* r2 = g_currentTcb */
    str     r0, [r2, #0]        /* g_currentTcb->m_stackPointer = r0 */

    /* Load g_nextTcb and make it the new current */
    ldr     r3, =g_nextTcb
    ldr     r2, [r3]            /* r2 = g_nextTcb */
    str     r2, [r1]            /* g_currentTcb = g_nextTcb */

    /* Update MPU thread stack region for incoming thread */
    ldr     r0, [r2, #OFFSET_MPU_RBAR]
    ldr     r1, [r2, #OFFSET_MPU_RASR]
    ldr     r3, =0xE000ED9C     /* MPU->RBAR address */
    stm     r3, {r0, r1}        /* Write RBAR + RASR atomically */

    /* Load the incoming thread's stack pointer */
    ldr     r0, [r2, #0]        /* r0 = g_nextTcb->m_stackPointer */

    /* Restore r4-r11 from the incoming thread's stack */
    ldmia   r0!, {r4-r11}

    /* Set PSP to the incoming thread's stack (pointing past r4-r11, at hw frame) */
    msr     psp, r0

    /* Re-enable interrupts */
    cpsie   i

    /* Return to thread mode using PSP (EXC_RETURN = 0xFFFFFFFD) */
    ldr     r0, =0xFFFFFFFD
    bx      r0

    .size PendSV_Handler, .-PendSV_Handler

/* -------------------------------------------------------------------------- */
/*  SVC_Handler -- launch first thread (no context to save)                   */
/* -------------------------------------------------------------------------- */

    .section .text.SVC_Handler, "ax", %progbits
    .global SVC_Handler
    .type SVC_Handler, %function

SVC_Handler:
    /* Load g_currentTcb (set by startScheduler before SVC) */
    ldr     r0, =g_currentTcb
    ldr     r1, [r0]            /* r1 = g_currentTcb */

    /* Update MPU thread stack region for first thread */
    ldr     r2, [r1, #OFFSET_MPU_RBAR]
    ldr     r3, [r1, #OFFSET_MPU_RASR]
    ldr     r0, =0xE000ED9C     /* MPU->RBAR address */
    stm     r0, {r2, r3}        /* Write RBAR + RASR */

    /* Reload g_currentTcb pointer (r0 was reused) */
    ldr     r0, =g_currentTcb
    ldr     r1, [r0]
    ldr     r0, [r1, #0]        /* r0 = g_currentTcb->m_stackPointer */

    /* Restore r4-r11 from the initial stack frame */
    ldmia   r0!, {r4-r11}

    /* Set PSP to point at the hardware frame (r0, r1, ..., xPSR) */
    msr     psp, r0

    /* Switch to using PSP for thread mode (set CONTROL.SPSEL = 1) */
    movs    r0, #2
    msr     control, r0
    isb                         /* Instruction sync barrier after CONTROL write */

    /* Return to thread mode using PSP */
    ldr     r0, =0xFFFFFFFD
    bx      r0

    .size SVC_Handler, .-SVC_Handler
