# Startup code - must be at address 0
# Sets up stack pointer and calls main()

.section .text.init
.global _boot
.global _start

_boot:
_start:
    # Set up stack pointer (top of stack section from linker script)
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
    
    # Call main
    call main
    
    # If main returns, halt with EBREAK
    ebreak
    
    # Fallback infinite loop
3:
    j 3b
