OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text : {
        *(.text.start)
        *(.text)
    }
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    .data : {
        *(.data)
    }

    _loaded_end = .;

    /*
     * Safety check: the kernel binary (text + rodata + data) must fit
     * below the BIOS/VGA memory hole that starts at physical 0xA0000.
     * If this assertion fires, the kernel has grown too large.
     */
    ASSERT(_loaded_end <= 0xA0000,
           "Kernel binary (text+data) exceeds limit — overlaps BIOS hole at 0xA0000")

    /*
     * BSS is placed above the BIOS/VGA memory hole (0xA0000-0xFFFFF).
     * The region 0xA0000-0xFFFFF is mapped to VGA framebuffer and BIOS
     * ROM — NOT usable RAM.  Placing BSS here caused silent corruption
     * of kernel data structures whenever the kernel grew large enough.
     *
     * The kernel zeros BSS explicitly in protected mode (_start).
     */
    . = 0x100000;
    _bss_start = .;
    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Mark the end of the kernel for memory management */
    _kernel_end = .;
}