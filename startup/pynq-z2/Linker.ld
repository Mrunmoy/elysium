/*
 * Linker script for PYNQ-Z2 (Zynq-7020 Cortex-A9)
 *
 * Memory map (JTAG DDR load):
 *   DDR - 511M at 0x00100000  (skip first 1 MB, reserved by BootROM/OCM)
 *
 * Everything is placed in DDR. For JTAG loading, VMA == LMA so no
 * .data copy is needed at startup.
 */

ENTRY(_vector_table)

MEMORY
{
    DDR (rwx) : ORIGIN = 0x00100000, LENGTH = 511M
}

SECTIONS
{
    /* Exception vector table at load address */
    .vectors 0x00100000 :
    {
        KEEP(*(.vectors))
    } > DDR

    /* Boot code (runs before C environment is ready) */
    .text.boot :
    {
        *(.text.boot)
    } > DDR

    /* Program code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } > DDR

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > DDR

    /* ARM exception handling */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > DDR

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > DDR

    /* C++ constructors */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > DDR

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > DDR

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > DDR

    /* Initialized data (VMA == LMA in DDR, no copy needed) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > DDR

    /* Uninitialized data (zeroed at startup) */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > DDR

    /* Per-mode stacks (Cortex-A9 has banked SP per exception mode) */
    .stacks (NOLOAD) :
    {
        . = ALIGN(8);
        __irq_stack_start = .;
        . += 1K;
        __irq_stack_end = .;

        . = ALIGN(8);
        __svc_stack_start = .;
        . += 2K;
        __svc_stack_end = .;

        . = ALIGN(8);
        __abt_stack_start = .;
        . += 1K;
        __abt_stack_end = .;

        . = ALIGN(8);
        __fiq_stack_start = .;
        . += 1K;
        __fiq_stack_end = .;

        . = ALIGN(8);
        __und_stack_start = .;
        . += 1K;
        __und_stack_end = .;

        . = ALIGN(8);
        __sys_stack_start = .;
        . += 4K;
        __sys_stack_end = .;
    } > DDR

    /* Heap (for future use) */
    .heap (NOLOAD) :
    {
        . = ALIGN(16);
        _heap_start = .;
        . += 16K;
        _heap_end = .;
    } > DDR

    /* Discard debug sections from vendor code */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }
}
