/* Linker script for RISC-V SoC with separate IMEM/DMEM
 * 
 * IMEM and DMEM are physically separate memories, both addressed from 0.
 * - Instruction fetch goes to IMEM
 * - Data load/store goes to DMEM
 *
 * Binary Layout:
 *   [0x0000 - text_end]:  .text (goes to IMEM)
 *   [text_end - ...]:     .rodata, .data (goes to DMEM at VMA 0x400+)
 *
 * The elf_loader parses sections and routes them correctly.
 */

MEMORY
{
    IMEM (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    DMEM (rw)  : ORIGIN = 0x00000400, LENGTH = 31K
}

ENTRY(_boot)

SECTIONS
{
    /* Code - in IMEM */
    .text : {
        *(.text.boot)
        *(.text.init) 
        *(.text._start)
        *(.text*)
        . = ALIGN(4);
    } > IMEM
    
    __text_end = .;

    /* Data sections - VMA in DMEM, LMA after .text */
    .rodata : AT(__text_end) {
        __rodata_start = .;
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
        __rodata_end = .;
    } > DMEM
    
    __rodata_lma_end = __text_end + (__rodata_end - __rodata_start);

    .data : AT(__rodata_lma_end) {
        __data_start = .;
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
        __data_end = .;
    } > DMEM

    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DMEM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + 0x800;
        __stack_top = .;
    } > DMEM
}
