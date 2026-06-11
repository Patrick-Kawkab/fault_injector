/* =============================================================================
   lm3s6965evb.ld  —  linker script for bare-metal lm3s6965evb (Cortex-M3)
   
   Memory map (matches QEMU lm3s6965evb machine):
     Flash : 0x00000000  256K
     RAM   : 0x20000000   64K
   ============================================================================= */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Stack at top of RAM */
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* ── Vector table + code ── */
    .text :
    {
        KEEP(*(.vectors))       /* vector table must be first */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ── Initialised data — stored in flash, copied to RAM at startup ── */
    .data :
    {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    /* ── Zero-initialised data ── */
    .bss :
    {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* ── Stack ── */
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        . += 0x800;   /* 2 KB stack */
    } > RAM
}
