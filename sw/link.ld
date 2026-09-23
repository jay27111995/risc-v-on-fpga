/* Linker script for RISC-V SoC with separate IMEM/DMEM
 * 
 * Memory Map:
 *   IMEM: 0x00000000+ - instruction fetch only (128KB)
 *   DMEM: 0x00000000+ - data load/store only (32KB)
 *
 * The ELF loader will:
 *   - Put .text sections in IMEM
 *   - Put .rodata/.data/.bss sections in DMEM
 *
 * DMEM layout:
 *   0x000-0x0FF: Reserved (general purpose)
 *   0x100-0x13F: TX_BUF (16 words)
 *   0x140:       TX_HEAD
 *   0x144:       TX_TAIL
 *   0x148-0x1FF: Reserved
 *   0x200-0x23F: RX_BUF (16 words)
 *   0x240:       RX_HEAD
 *   0x244:       RX_TAIL
 *   0x248-0x3FF: Reserved
 *   0x400+:      Program data (.rodata, .data, .bss, stack)
 */

MEMORY
{
    IMEM (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    DMEM (rw)  : ORIGIN = 0x00000400, LENGTH = 31K
}

ENTRY(_boot)

SECTIONS
{
    /* Code - goes to IMEM (executable) */
    .text : {
        *(.text.boot)
        *(.text.init)
        *(.text._start)
        *(.text*)
    } > IMEM

    /* Read-only data - goes to DMEM (not executable) */
    .rodata : {
        . = ALIGN(4);
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > DMEM

    /* Initialized data */
    .data : {
        . = ALIGN(4);
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
    } > DMEM

    /* BSS - uninitialized data */
    .bss : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DMEM

    /* Stack */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + 0x800;
        __stack_top = .;
    } > DMEM
}
