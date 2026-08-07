/* Simple test program for RISC-V SoC
 * Uses direct memory addresses (no pointers)
 */

// Direct write to memory address using inline assembly
#define DMEM_WRITE(addr, val) \
    __asm__ volatile ("sw %0, %1(zero)" : : "r"(val), "i"(addr))

#define DMEM_READ(addr) ({ \
    int _val; \
    __asm__ volatile ("lw %0, %1(zero)" : "=r"(_val) : "i"(addr)); \
    _val; \
})

int sum_to_n(int n) {
    int result = 0;
    for (int i = 1; i <= n; i++) {
        result += i;
    }
    return result;
}

void _start(void) {
    // Compute sum(1..10) = 55
    int result = sum_to_n(10);
    
    // Store to DMEM using direct addresses
    DMEM_WRITE(0, result);      // DMEM[0] = 55
    DMEM_WRITE(4, 10);          // DMEM[1] = 10 (input)
    DMEM_WRITE(8, 0xDEAD);      // DMEM[2] = marker
    DMEM_WRITE(12, result + 5); // DMEM[3] = 60
    DMEM_WRITE(16, result - 5); // DMEM[4] = 50
    
    // Halt - infinite loop
    while(1);
}
