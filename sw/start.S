# Startup code for RISC-V SoC
# Must be at address 0 in IMEM

.equ DMEM_BASE, 0x10000000

.section .text.boot
.global _boot

_boot:
    # Set up stack pointer
    la sp, __stack_top
    
    # Clear BSS section
    la t0, __bss_start
    la t1, __bss_end
1:
    bge t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    
    # Clear UART buffer pointers
    li t0, DMEM_BASE + 0x140   # TX_HEAD
    sw zero, 0(t0)
    li t0, DMEM_BASE + 0x144   # TX_TAIL
    sw zero, 0(t0)
    li t0, DMEM_BASE + 0x240   # RX_HEAD
    sw zero, 0(t0)
    li t0, DMEM_BASE + 0x244   # RX_TAIL
    sw zero, 0(t0)
    
    # Call main
    call main
    
    # If main returns, halt with EBREAK
    ebreak
    
    # Fallback infinite loop
3:
    j 3b
