// Startup -- Cortex-A9 boot sequence for PYNQ-Z2 (Zynq-7020)
//
// ARM mode startup assembly:
// 1. Exception vector table (8 entries)
// 2. CPU1 parking (only CPU0 boots)
// 3. Disable MMU, caches (clean slate after FSBL)
// 4. Invalidate TLBs, branch predictor, caches
// 5. Set up flat 1:1 MMU mapping + enable MMU and caches
// 6. Set up per-mode stacks (IRQ, SVC, ABT, FIQ, UND, SYS)
// 7. Zero .bss
// 8. C++ static constructors
// 9. SystemInit() + main()
//
// MMU note: On Cortex-A9 with MMU disabled, all memory is Strongly Ordered.
// Strongly Ordered memory requires natural alignment for all accesses, which
// breaks normal C/C++ code (GCC emits unaligned stack accesses). We MUST
// enable the MMU with DDR mapped as Normal memory before calling any C code.

    .syntax unified
    .arm

// ---------- Exception Vector Table ----------

    .section .vectors, "ax"
    .global _vector_table
    .type _vector_table, %function
_vector_table:
    b       _boot                   // Reset
    b       Undefined_Handler       // Undefined instruction
    b       SVC_Handler             // Supervisor call (SWI)
    b       PrefetchAbort_Handler   // Prefetch abort
    b       DataAbort_Handler       // Data abort
    nop                             // Reserved
    b       IRQ_Handler             // IRQ
    b       FIQ_Handler             // FIQ

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
    // (FSBL may have left these enabled)
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

    // Invalidate L1 D-cache (iterate over all sets and ways)
    // Cortex-A9: 4-way, 256 sets, 32-byte line (32KB L1 D-cache)
    // DCISW format: [31:30]=Way, [12:5]=Set, [3:1]=Level (0 for L1)
    mov     r0, #0                   // Way counter
_inv_dcache_way:
    mov     r1, #0                   // Set counter
_inv_dcache_set:
    orr     r2, r1, r0               // Combine way + set
    mcr     p15, 0, r2, c7, c6, 2   // DCISW: invalidate by set/way
    add     r1, r1, #(1 << 5)       // Next set (bit 5)
    cmp     r1, #(256 << 5)          // 256 sets
    blt     _inv_dcache_set
    add     r0, r0, #(1 << 30)      // Next way (bit 30)
    cmp     r0, #0                   // Wraps to 0 after 4 ways
    bne     _inv_dcache_way
    dsb
    isb

    // Disable and invalidate L2 cache (PL310 controller)
    // Required for JTAG reload: L2 may hold stale code/data from a previous
    // binary that was overwritten in DRAM via the debug port.
    ldr     r0, =0xF8F02100          // L2C-310 Control Register
    mov     r1, #0
    str     r1, [r0]                 // Disable L2 cache
    dsb

    ldr     r0, =0xF8F0277C          // L2C-310 Invalidate by Way register
    mov     r1, #0xFF                // Invalidate all 8 ways
    str     r1, [r0]
3:  ldr     r2, [r0]                 // Poll until invalidation completes
    tst     r2, #0xFF
    bne     3b
    dsb

    // ---------- Set up flat 1:1 MMU (required for Normal memory semantics) ---
    bl      _setup_mmu

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
// Weak symbols so the kernel can override them with strong definitions.

    .weak   Undefined_Handler
    .type   Undefined_Handler, %function
Undefined_Handler:
    b       Undefined_Handler

    .weak   SVC_Handler
    .type   SVC_Handler, %function
SVC_Handler:
    b       SVC_Handler

    .weak   PrefetchAbort_Handler
    .type   PrefetchAbort_Handler, %function
PrefetchAbort_Handler:
    b       PrefetchAbort_Handler

    .weak   DataAbort_Handler
    .type   DataAbort_Handler, %function
DataAbort_Handler:
    b       DataAbort_Handler

    .weak   IRQ_Handler
    .type   IRQ_Handler, %function
IRQ_Handler:
    b       IRQ_Handler

    .weak   FIQ_Handler
    .type   FIQ_Handler, %function
FIQ_Handler:
    b       FIQ_Handler

// ---------- MMU Setup ----------
// Build a flat 1:1 L1 translation table (4096 x 1MB sections = 4GB)
// and enable MMU + caches.
//
// Memory map:
//   0x00000000-0x1FFFFFFF (512 MB): Normal, cacheable, shareable (DDR)
//   0x20000000-0xFFFFFFFF (3.5 GB): Device, shareable (peripherals, PL)
//
// Section descriptor format (ARMv7-A short descriptor):
//   [1:0]=10 (section), [2]=B, [3]=C, [4]=XN, [8:5]=Domain,
//   [11:10]=AP[1:0], [14:12]=TEX, [15]=AP[2], [16]=S, [31:20]=Base

    .equ    SECT_NORMAL, 0x00011C0E  // TEX=001 C=1 B=1 S=1 AP=11 section
                                     // -> Normal, Inner/Outer WB-WA, Shareable
    .equ    SECT_DEVICE, 0x00000C06  // TEX=000 C=0 B=1 AP=11 section
                                     // -> Device, Shareable
    .equ    DDR_SECTIONS, 512        // 512 MB of DDR

_setup_mmu:
    push    {r4, lr}

    // Fill translation table
    ldr     r0, =_mmu_table          // r0 = table pointer
    mov     r1, #0                   // r1 = section base address (increments by 1MB)

    // Entries 0-511: DDR as Normal, cacheable
    ldr     r2, =SECT_NORMAL
    mov     r3, #DDR_SECTIONS
1:
    orr     r4, r2, r1
    str     r4, [r0], #4
    add     r1, r1, #0x00100000      // Next 1MB section
    subs    r3, r3, #1
    bne     1b

    // Entries 512-4095: Everything else as Device
    ldr     r2, =SECT_DEVICE
    ldr     r3, =(4096 - DDR_SECTIONS)
2:
    orr     r4, r2, r1
    str     r4, [r0], #4
    add     r1, r1, #0x00100000
    subs    r3, r3, #1
    bne     2b

    // Set TTBR0 (translation table base)
    ldr     r0, =_mmu_table
    mcr     p15, 0, r0, c2, c0, 0   // Write TTBR0
    isb

    // Set DACR: all domains = Manager (full access, no permission check)
    mvn     r0, #0                   // 0xFFFFFFFF
    mcr     p15, 0, r0, c3, c0, 0   // Write DACR
    isb

    // Enable MMU, D-cache, I-cache, branch prediction
    mrc     p15, 0, r0, c1, c0, 0   // Read SCTLR
    orr     r0, r0, #(1 << 0)       // M: MMU enable
    orr     r0, r0, #(1 << 2)       // C: D-cache enable
    orr     r0, r0, #(1 << 11)      // Z: Branch prediction enable
    orr     r0, r0, #(1 << 12)      // I: I-cache enable
    mcr     p15, 0, r0, c1, c0, 0   // Write SCTLR
    dsb
    isb

    pop     {r4, pc}

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
