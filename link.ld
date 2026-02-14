OUTPUT_FORMAT("binary")
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

    _loaded_end = .;

    /* BSS immediately follows data — no gap needed since we're above 1MB */
    . = ALIGN(4096);
    _bss_start = .;
    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* Mark the end of the kernel for memory management */
    _kernel_end = .;

    /*
     * Safety check: kernel must fit within the 32MB identity-mapped region.
     * The floppy limit (1.44MB) is the practical ceiling for now.
     */
        ASSERT(_loaded_end <= 0x267600,
            "Kernel binary (text+data) exceeds available floppy image capacity")
}