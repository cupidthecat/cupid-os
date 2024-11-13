OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS
{
    . = 0x1000;
    .text : {
        *(.text.start)
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
}