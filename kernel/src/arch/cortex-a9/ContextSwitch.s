/**
 * Context switching assembly for Cortex-A9 (ARMv7-A, ARM mode, soft-float).
 *
 * IRQ_Handler:   Dispatches interrupts via GIC, performs context switch if needed.
 * SVC_Handler:   Launches the very first thread (no outgoing context to save).
 *
 * Stack frame layout per thread (16 words = 64 bytes, matches Cortex-M layout):
 *   [SP+0  .. SP+7]   r4-r11       (software-saved by IRQ handler)
 *   [SP+8  .. SP+15]  r0,r1,r2,r3,r12,LR,PC,CPSR  (saved on IRQ entry)
 *
 * On Cortex-A9, there is no PendSV. Context switches happen at the end of
 * the IRQ handler if g_nextTcb differs from g_currentTcb.
 *
 * Threads run in SYS mode (0x1F). The IRQ handler runs in IRQ mode (0x12).
 * On IRQ entry, hardware banks LR_irq and SPSR_irq. We use SRS/RFE for
 * clean save/restore of the return state.
 *
 * Global pointers (defined in Kernel.cpp, extern "C"):
 *   g_currentTcb  -- TCB of currently running thread
 *   g_nextTcb     -- TCB of thread to switch to
 *
 * TCB layout: stackPointer is at offset 0 (first field).
 *
 * External C functions called:
 *   SysTick_Handler()     -- kernel tick (increments tick count, runs scheduler)
 *   PrivateTimer_Handler() -- clears timer interrupt flag
 */

    .syntax unified
    .cpu cortex-a9
    .arm

/* GIC CPU interface registers */
    .equ GIC_CPU_BASE,   0xF8F00100
    .equ ICCIAR_OFFSET,  0x0C
    .equ ICCEOIR_OFFSET, 0x10

/* Interrupt IDs */
    .equ SGI_CONTEXT_SWITCH, 0
    .equ PRIVATE_TIMER_IRQ,  29

/* -------------------------------------------------------------------------- */
/*  IRQ_Handler -- interrupt dispatch + context switch                        */
/* -------------------------------------------------------------------------- */

    .section .text.IRQ_Handler, "ax", %progbits
    .global IRQ_Handler
    .type IRQ_Handler, %function

IRQ_Handler:
    /* On IRQ entry we are in IRQ mode with:
     *   LR_irq = return address + 4 (ARM pipeline)
     *   SPSR_irq = interrupted thread's CPSR
     * Adjust LR to point to the actual return address */
    sub     lr, lr, #4

    /* Save return address and SPSR onto the SYS mode stack using SRS.
     * SRS stores {LR_irq, SPSR_irq} at [SP_sys], decrementing SP_sys.
     * This puts the PC and CPSR words at the top of the thread stack. */
    srsdb   sp!, #0x1F          /* SRS: store return state to SYS mode stack (DB = decrement before) */

    /* Switch to SYS mode (same registers as thread, keeps IRQs disabled) */
    cps     #0x1F

    /* Now we are in SYS mode. SP points to the thread stack with
     * {LR_irq(=PC), SPSR(=CPSR)} already pushed.
     * Save the rest of the exception frame: r0-r3, r12, LR (thread) */
    push    {r0-r3, r12, lr}

    /* CRITICAL: Do NOT use r4-r11 before saving them in the context switch!
     * Those registers belong to the interrupted thread. If we clobber them
     * here and a context switch happens, the outgoing thread's callee-saved
     * registers would be corrupted with our values instead of the thread's.
     * Use only r0-r3 (already saved above) and the stack for temporaries. */

    /* Read GIC ICCIAR to acknowledge the interrupt (get interrupt ID) */
    ldr     r0, =GIC_CPU_BASE
    ldr     r1, [r0, #ICCIAR_OFFSET]

    /* Save full ICCIAR value on stack for later EOI (replaces use of r5) */
    push    {r1}

    /* Extract interrupt ID (bits 9:0) and dispatch */
    ubfx    r0, r1, #0, #10
    cmp     r0, #PRIVATE_TIMER_IRQ
    beq     .Ltimer_irq

    cmp     r0, #SGI_CONTEXT_SWITCH
    beq     .Lsgi_context_switch

    /* Unknown interrupt -- just acknowledge and return */
    b       .Lirq_eoi

.Ltimer_irq:
    /* Clear timer interrupt and run kernel tick */
    bl      PrivateTimer_Handler
    bl      SysTick_Handler
    b       .Lirq_eoi

.Lsgi_context_switch:
    /* SGI 0: context switch requested -- nothing to do here,
     * the switch happens in the epilogue below */
    b       .Lirq_eoi

.Lirq_eoi:
    /* Restore ICCIAR value from stack and write EOI */
    pop     {r1}
    ldr     r0, =GIC_CPU_BASE
    str     r1, [r0, #ICCEOIR_OFFSET]

    /* -- Context switch check -- */
    ldr     r0, =g_currentTcb
    ldr     r1, =g_nextTcb
    ldr     r2, [r0]            /* r2 = g_currentTcb */
    ldr     r3, [r1]            /* r3 = g_nextTcb */
    cmp     r2, r3
    beq     .Lno_switch         /* Same thread -- skip context switch */

    /* -- Save outgoing context -- */
    /* Push r4-r11 onto thread stack (software-saved context).
     * These are the INTERRUPTED THREAD'S register values, untouched
     * since we only used r0-r3 above. */
    push    {r4-r11}

    /* Save SP into g_currentTcb->stackPointer (offset 0) */
    str     sp, [r2, #0]

    /* -- Load incoming context -- */
    /* g_currentTcb = g_nextTcb */
    str     r3, [r0]

    /* Load SP from g_nextTcb->stackPointer */
    ldr     sp, [r3, #0]

    /* Restore r4-r11 from incoming thread's stack */
    pop     {r4-r11}

.Lno_switch:
    /* Restore r0-r3, r12, LR (thread) */
    pop     {r0-r3, r12, lr}

    /* Return from exception: RFE loads PC and CPSR from stack.
     * This restores the thread's execution state atomically. */
    rfeia   sp!

    .size IRQ_Handler, .-IRQ_Handler

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
    ldr     sp, [r1, #0]        /* SP = g_currentTcb->stackPointer */

    /* Restore r4-r11 from the initial stack frame */
    pop     {r4-r11}

    /* Restore r0-r3, r12, LR */
    pop     {r0-r3, r12, lr}

    /* RFE: load PC and CPSR from stack, atomically returning to
     * the first thread in SYS mode with interrupts enabled. */
    rfeia   sp!

    .size SVC_Handler, .-SVC_Handler
