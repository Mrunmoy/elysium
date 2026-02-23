/**
 * Context switching assembly for Cortex-M4 (ARMv7-M, Thumb-2, soft-float).
 *
 * PendSV_Handler:  Performs context switch between threads.
 * SVC_Handler:     Dispatches syscalls (SVC 1+) or launches first thread (SVC 0).
 *
 * Stack frame layout per thread (16 words = 64 bytes):
 *   [SP+0  .. SP+7]   r4-r11      (software-saved by PendSV)
 *   [SP+8  .. SP+15]  r0,r1,r2,r3,r12,LR,PC,xPSR  (hardware-saved on exception entry)
 *
 * Note: With -mfloat-abi=soft, no FPU registers (S0-S31, FPSCR) are saved.
 * When hard-float is enabled, this file must be updated to save/restore
 * FP context and use EXC_RETURN 0xFFFFFFED instead of 0xFFFFFFFD.
 *
 * Global pointers (defined in Kernel.cpp, extern "C"):
 *   g_currentTcb  -- TCB of currently running thread
 *   g_nextTcb     -- TCB of thread to switch to
 *
 * TCB layout:
 *   offset  0: stackPointer
 *   offset 36: mpuStackRbar
 *   offset 40: mpuStackRasr
 *   offset 44: privileged (bool/uint8_t)
 */

    .syntax unified
    .cpu cortex-m4
    .thumb

/* TCB field offsets */
    .equ OFFSET_MPU_RBAR,   36
    .equ OFFSET_MPU_RASR,   40
    .equ OFFSET_PRIVILEGED, 44

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

    /* Store the updated PSP into g_currentTcb->stackPointer */
    ldr     r1, =g_currentTcb
    ldr     r2, [r1]            /* r2 = g_currentTcb */
    str     r0, [r2, #0]        /* g_currentTcb->stackPointer = r0 */

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
    ldr     r0, [r2, #0]        /* r0 = g_nextTcb->stackPointer */

    /* Restore r4-r11 from the incoming thread's stack */
    ldmia   r0!, {r4-r11}

    /* Set PSP to the incoming thread's stack (pointing past r4-r11, at hw frame) */
    msr     psp, r0

    /* Set CONTROL register based on incoming thread's privilege level */
    ldrb    r0, [r2, #OFFSET_PRIVILEGED]
    cmp     r0, #0
    ite     eq
    moveq   r0, #3              /* unprivileged: nPRIV=1 | SPSEL=1 */
    movne   r0, #2              /* privileged:   nPRIV=0 | SPSEL=1 */
    msr     control, r0
    isb

    /* Re-enable interrupts */
    cpsie   i

    /* Return to thread mode using PSP (EXC_RETURN = 0xFFFFFFFD) */
    ldr     r0, =0xFFFFFFFD
    bx      r0

    .size PendSV_Handler, .-PendSV_Handler

/* -------------------------------------------------------------------------- */
/*  SVC_Handler -- syscall dispatch + first thread launch                     */
/* -------------------------------------------------------------------------- */

    .section .text.SVC_Handler, "ax", %progbits
    .global SVC_Handler
    .type SVC_Handler, %function

SVC_Handler:
    /* Determine which stack the caller was using.
     * EXC_RETURN bit 2: 0 = MSP, 1 = PSP. */
    tst     lr, #4
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp             /* r0 = exception frame pointer */

    /* Extract SVC number from the SVC instruction.
     * The stacked PC points to the instruction after SVC.
     * SVC instruction is at [PC - 2]; the immediate is the low byte. */
    ldr     r1, [r0, #24]       /* r1 = stacked PC */
    ldrb    r1, [r1, #-2]       /* r1 = SVC number */

    /* SVC 0: launch first thread (special path) */
    cmp     r1, #0
    beq     .L_start_first

    /* --- SVC 1+: general syscall dispatch --- */
    push    {r0, lr}            /* save frame pointer + EXC_RETURN */
    mov     r2, r0              /* r2 = frame pointer (temp) */
    mov     r0, r1              /* r0 = svc number (arg 1 for svcDispatch) */
    mov     r1, r2              /* r1 = frame pointer (arg 2 for svcDispatch) */
    bl      svcDispatch         /* uint32_t svcDispatch(uint8_t, uint32_t*) */
    pop     {r1, lr}            /* r1 = frame pointer, lr = EXC_RETURN */
    str     r0, [r1, #0]        /* write return value to stacked r0 */
    bx      lr                  /* return to caller */

.L_start_first:
    /* Load g_currentTcb (set by startScheduler before SVC 0) */
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
    ldr     r0, [r1, #0]        /* r0 = g_currentTcb->stackPointer */

    /* Restore r4-r11 from the initial stack frame */
    ldmia   r0!, {r4-r11}

    /* Set PSP to point at the hardware frame (r0, r1, ..., xPSR) */
    msr     psp, r0

    /* Set CONTROL based on first thread's privilege level */
    ldrb    r0, [r1, #OFFSET_PRIVILEGED]
    cmp     r0, #0
    ite     eq
    moveq   r0, #3              /* unprivileged: nPRIV=1 | SPSEL=1 */
    movne   r0, #2              /* privileged:   nPRIV=0 | SPSEL=1 */
    msr     control, r0
    isb

    /* Return to thread mode using PSP */
    ldr     r0, =0xFFFFFFFD
    bx      r0

    .size SVC_Handler, .-SVC_Handler
