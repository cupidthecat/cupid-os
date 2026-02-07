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
    .bss : {
        *(.bss)
    }
    
    /* Mark the end of the kernel for memory management */
    _kernel_end = .;
}