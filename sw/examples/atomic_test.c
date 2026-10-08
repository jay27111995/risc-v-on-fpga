// atomic_test.c - Test LR.W/SC.W atomic instructions (A extension)

#include "stdio.h"

// Inline assembly for LR.W and SC.W
// LR.W rd, (rs1) - Load-Reserved Word
// SC.W rd, rs2, (rs1) - Store-Conditional Word (returns 0 on success, 1 on fail)

static inline int lr_w(volatile int *addr) {
    int result;
    asm volatile("lr.w %0, (%1)" : "=r"(result) : "r"(addr) : "memory");
    return result;
}

static inline int sc_w(volatile int *addr, int value) {
    int result;
    asm volatile("sc.w %0, %2, (%1)" : "=r"(result) : "r"(addr), "r"(value) : "memory");
    return result;  // 0 = success, 1 = failure
}

volatile int shared_var = 100;
volatile int lock = 0;

int main(void) {
    puts("Atomic test (LR.W/SC.W)\n");
    
    // Print address of shared_var
    puts("shared_var addr: ");
    print_int((int)&shared_var);
    puts("\n");
    
    // Test: SC.W without LR.W - should FAIL (return 1)
    puts("\nTest: SC without LR (should return 1=fail)...\n");
    int sc_result = sc_w(&shared_var, 999);
    puts("SC result: ");
    print_int(sc_result);
    puts(" (expect 1)\n");
    
    // Check if store happened
    puts("shared_var = ");
    print_int(shared_var);
    puts(" (expect 100, unchanged)\n");
    
    puts("\n===END===\n");
    return 0;
}
