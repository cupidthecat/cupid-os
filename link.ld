/* Output format is selected on the ld command line — `--oformat binary`
 * for the bootable kernel.bin and default elf32-i386 for the pass-1
 * symbol-extraction step (see Makefile two-pass kernel link). */
ENTRY(_start)
SECTIONS
{
    /*
     * Kernel is loaded at 1MB (0x100000) by the bootloader via unreal mode.
     * This gives us ~31MB of contiguous space (up to the 32MB identity map)
     * instead of the old 576KB limit below the BIOS hole.
     */
    . = 0x100000;
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

    /* Kernel symbol blob for panic backtrace. Placed after .data so its
     * size doesn't shift any code addresses between two-pass links —
     * mksyms.sh extracts symbols from a pass-1 ELF, then the populated
     * ksyms_data.o is linked in for pass 2 with stable text addresses. */
    .ksyms ALIGN(4) : {
        __ksyms_start = .;
        *(.ksyms)
        __ksyms_end = .;
    }

    _loaded_end = .;

    /* BSS immediately follows data - no gap needed since we're above 1MB */
    . = ALIGN(4096);
    _bss_start = .;
    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Mark the end of the kernel for memory management */
    _kernel_end = .;

    /*
     * Safety check: kernel text+data must fit in bootloader kernel area
     * (LBA 5..4095 = 4091 sectors = 0x1FF600 bytes) loaded at 0x100000.
     */
    ASSERT(_loaded_end <= 0x2FF600,
           "Kernel binary (text+data) exceeds reserved HDD kernel area")
}