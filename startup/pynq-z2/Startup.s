// Startup -- Cortex-A9 boot sequence for PYNQ-Z2 (Zynq-7020)
//
// ARM mode startup assembly:
// 1. Exception vector table (8 entries)
// 2. CPU1 parking (only CPU0 boots)
// 3. Disable MMU, caches (clean slate after u-boot)
// 4. Invalidate TLBs, branch predictor
// 5. Set up per-mode stacks (IRQ, SVC, ABT, FIQ, UND, SYS)
// 6. Zero .bss
// 7. C++ static constructors
// 8. SystemInit() + main()

    .syntax unified
    .arm

// ---------- Exception Vector Table ----------

    .section .vectors, "ax"
    .global _vector_table
    .type _vector_table, %function
_vector_table:
    b       _boot                   // Reset
    b       _undef_handler          // Undefined instruction
    b       _svc_handler            // Supervisor call (SWI)
    b       _pabt_handler           // Prefetch abort
    b       _dabt_handler           // Data abort
    nop                             // Reserved
    b       _irq_handler            // IRQ
    b       _fiq_handler            // FIQ

// ---------- Boot Sequence ----------

    .section .text.boot, "ax"
    .global _boot
    .type _boot, %function
_boot:
    // Disable IRQ and FIQ
    cpsid   if

    // Check CPU ID -- only CPU0 proceeds, CPU1 parks
    mrc     p15, 0, r0, c0, c0, 5   // Read MPIDR
    and     r0, r0, #0x3
    cmp     r0, #0
    bne     _cpu1_wait

    // Set VBAR to our vector table
    ldr     r0, =_vector_table
    mcr     p15, 0, r0, c12, c0, 0  // Write VBAR
    isb

    // Disable MMU, I-cache, D-cache, branch prediction, alignment check
    // (u-boot may have left these enabled for Linux)
    mrc     p15, 0, r0, c1, c0, 0   // Read SCTLR
    bic     r0, r0, #(1 << 0)       // M: MMU disable
    bic     r0, r0, #(1 << 1)       // A: Alignment check disable
    bic     r0, r0, #(1 << 2)       // C: D-cache disable
    bic     r0, r0, #(1 << 11)      // Z: Branch prediction disable
    bic     r0, r0, #(1 << 12)      // I: I-cache disable
    mcr     p15, 0, r0, c1, c0, 0   // Write SCTLR
    isb

    // Invalidate TLBs
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0   // TLBIALL: invalidate unified TLB
    dsb
    isb

    // Invalidate I-cache
    mcr     p15, 0, r0, c7, c5, 0   // ICIALLU: invalidate all I-cache
    dsb
    isb

    // Invalidate branch predictor
    mcr     p15, 0, r0, c7, c5, 6   // BPIALL: invalidate branch predictor
    dsb
    isb

    // ---------- Set up per-mode stacks ----------
    // Each ARM mode has a banked SP register.
    // Switch mode via CPS, set SP, repeat.

    // IRQ mode (0x12)
    cps     #0x12
    ldr     sp, =__irq_stack_end

    // Abort mode (0x17)
    cps     #0x17
    ldr     sp, =__abt_stack_end

    // Undefined mode (0x1B)
    cps     #0x1B
    ldr     sp, =__und_stack_end

    // FIQ mode (0x11)
    cps     #0x11
    ldr     sp, =__fiq_stack_end

    // SVC mode (0x13)
    cps     #0x13
    ldr     sp, =__svc_stack_end

    // System mode (0x1F) -- main execution mode
    cps     #0x1F
    ldr     sp, =__sys_stack_end

    // ---------- Zero .bss ----------
    ldr     r0, =_sbss
    ldr     r1, =_ebss
    mov     r2, #0
_bss_loop:
    cmp     r0, r1
    bge     _bss_done
    str     r2, [r0], #4
    b       _bss_loop
_bss_done:

    // ---------- C++ static constructors ----------
    bl      __libc_init_array

    // ---------- System initialization ----------
    bl      SystemInit

    // ---------- Enter main ----------
    bl      main

    // If main() returns, spin forever
_hang:
    wfe
    b       _hang

// ---------- CPU1 Wait Loop ----------
_cpu1_wait:
    wfe
    b       _cpu1_wait

// ---------- Default Exception Handlers ----------
// Weak symbols so the kernel can override them in Phase 4B.

    .weak   _undef_handler
    .type   _undef_handler, %function
_undef_handler:
    b       _undef_handler

    .weak   _svc_handler
    .type   _svc_handler, %function
_svc_handler:
    b       _svc_handler

    .weak   _pabt_handler
    .type   _pabt_handler, %function
_pabt_handler:
    b       _pabt_handler

    .weak   _dabt_handler
    .type   _dabt_handler, %function
_dabt_handler:
    b       _dabt_handler

    .weak   _irq_handler
    .type   _irq_handler, %function
_irq_handler:
    b       _irq_handler

    .weak   _fiq_handler
    .type   _fiq_handler, %function
_fiq_handler:
    b       _fiq_handler

// ---------- _init / _fini stubs ----------
// Required by __libc_init_array when linking with -nostartfiles.

    .global _init
    .type   _init, %function
_init:
    bx      lr

    .global _fini
    .type   _fini, %function
_fini:
    bx      lr
