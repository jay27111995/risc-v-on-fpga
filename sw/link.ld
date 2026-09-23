/* Linker script for RISC-V SoC with separate IMEM/DMEM
 * 
 * Memory Map:
 *   IMEM: 0x00000000+ - instruction fetch only (128KB)
 *   DMEM: 0x00000000+ - data load/store only (32KB)
 *
 * Binary Layout (LMA - what goes in the .elf/.bin file):
 *   0x00000000: .text (code)
 *   After .text: .rodata, .data (to be loaded to DMEM by elf_loader)
 *
 * Runtime Layout (VMA - where CPU sees things):
 *   IMEM[0x00000000]: .text (fetched as instructions)
 *   DMEM[0x00000400]: .rodata, .data, .bss (accessed as data)
 *
 * DMEM reserved areas:
 *   0x100-0x13F: TX_BUF
 *   0x140-0x147: TX_HEAD, TX_TAIL
 *   0x200-0x23F: RX_BUF  
 *   0x240-0x247: RX_HEAD, RX_TAIL
 */

MEMORY
{
    /* For code - LMA and VMA both at 0 */
    IMEM (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    
    /* For data - VMA at 0x400, LMA follows .text in file */
    DMEM (rw)  : ORIGIN = 0x00000400, LENGTH = 31K
}

ENTRY(_boot)

SECTIONS
{
    /* Code - VMA=0, LMA=0 (executed from IMEM) */
    .text : {
        *(.text.boot)
        *(.text.init)
        *(.text._start)
        *(.text*)
        . = ALIGN(4);
        __text_end = .;
    } > IMEM
    
    /* Read-only data - VMA=0x400+, LMA after .text */
    /* The elf_loader will put this in DMEM at VMA address */
    .rodata : {
        __rodata_start = .;
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        __rodata_end = .;
    } > DMEM

    /* Initialized data */
    .data : {
        __data_start = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        __data_end = .;
    } > DMEM

    /* BSS - uninitialized data (zeroed by startup) */
    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DMEM

    /* Stack at end of DMEM */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + 0x800;
        __stack_top = .;
    } > DMEM
}
