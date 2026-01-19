ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH
    
    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    
    /* نهاية الـ text section = بداية الـ .data في الـ FLASH */
    _sidata = .;
    
    .data : AT(_sidata) {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM
    
    .bss : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM
    
    /* Stack - نهاية الـ SRAM */
    .stack (NOLOAD) : {
        . = ORIGIN(SRAM) + LENGTH(SRAM);
        _estack = .;
    } > SRAM
    
    /DISCARD/ : {
        *(.note*)
        *(.comment*)
    }
}
