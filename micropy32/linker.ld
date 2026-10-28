SECTIONS
{
    . = 0x00200000;
    .text : {
        *(.magic)
        *(.entry)
        *(.text*)
    }
    .rdata : {
        *(.rdata*)
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
