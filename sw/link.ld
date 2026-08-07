/* Linker script for RISC-V SoC
 * 
 * Memory Map (from CPU's perspective):
 *   0x00000000 - 0x0001FFFF : IMEM (128KB) - code runs from here
 *   0x00000000 - 0x00007FFF : DMEM (32KB)  - data (overlaps, but separate in HW)
 *
 * Note: Our CPU uses address 0 for both IMEM fetch and DMEM access.
 * The hardware routes based on the access type (instruction vs data).
 */

MEMORY
{
    IMEM (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    DMEM (rw)  : ORIGIN = 0x00000000, LENGTH = 32K
}

ENTRY(_start)

SECTIONS
{
    /* Code section - goes to IMEM */
    .text : {
        *(.text.init)      /* Startup code with jump to _start */
        *(.text._start)
        *(.text*)
    } > IMEM

    /* Read-only data */
    .rodata : {
        *(.rodata*)
    } > IMEM

    /* Initialized data - we put in IMEM since we can't initialize DMEM */
    .data : {
        *(.data*)
    } > IMEM

    /* BSS - uninitialized data */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > IMEM

    /* Stack - not really used in bare metal, but define anyway */
    .stack (NOLOAD) : {
        . = . + 0x400;
        __stack_top = .;
    } > IMEM
}
