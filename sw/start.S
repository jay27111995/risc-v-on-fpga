# Startup code - must be at address 0
.section .text.init
.global _boot
_boot:
    j _start    # Jump to C entry point
