/* Linker script for RISC-V SoC with separate IMEM/DMEM
 * 
 * IMEM and DMEM are physically separate memories.
 * To avoid linker VMA overlap complaints, we use different address ranges:
 *   IMEM VMA: 0x00000000+ (code)
 *   DMEM VMA: 0x10000000+ (data) - CPU hardware ignores upper bits
 *
 * The actual hardware only uses lower bits of address for DMEM indexing.
 */

MEMORY
{
    IMEM (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    DMEM (rw)  : ORIGIN = 0x10000400, LENGTH = 31K
}

ENTRY(_boot)

SECTIONS
{
    .text : {
        *(.text.boot)
        *(.text.init) 
        *(.text._start)
        *(.text*)
        . = ALIGN(4);
    } > IMEM
    
    __text_end = .;

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
