/* CupidLD emits the pass ELF32 executable used for symbol extraction.
 * CupidObj then flattens the final executable for the bootloader. */
ENTRY(_start)
SECTIONS
{
    /*
     * Kernel is loaded at 1MB (0x100000) by the bootloader via unreal mode.
     * File-backed bytes must end by 0xAFF600 for the bootloader's reserved
     * disk area, and the complete memory image must end below the fixed
     * kernel stack at 0xF00000. Both boundaries are asserted below.
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
     * tools/hostbuild.py mksyms extracts symbols from a pass-1 ELF, then the
     * populated ksyms_data.o is linked in for pass 2 with stable text
     * addresses. */
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
     * (LBA 5..20479 = 20475 sectors = 0x9FF600 bytes) loaded at 0x100000.
     * The partition table moves with this boundary. See boot/boot.asm and
     * Makefile FAT_START_LBA.
     */
    ASSERT(_loaded_end <= 0xAFF600,
           "Kernel binary (text+data) exceeds reserved HDD kernel area")
    ASSERT(_kernel_end <= 0xF00000,
           "Kernel memory image overlaps the fixed kernel stack")
}
