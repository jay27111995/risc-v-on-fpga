// FENCE instruction test
#include "../lib/stdio.h"
#include "../lib/stdint.h"

int main(void) {
    printf("\n=== FENCE Test ===\n\n");
    
    volatile uint32_t x = 0;
    
    // Test FENCE (should be NOP, just verify it doesn't crash)
    printf("Before FENCE: x = %u\n", x);
    x = 42;
    asm volatile ("fence");  // Memory fence
    printf("After FENCE: x = %u\n", x);
    
    // Test FENCE.I (should be NOP, just verify it doesn't crash)
    x = 100;
    asm volatile ("fence.i");  // Instruction fence
    printf("After FENCE.I: x = %u\n", x);
    
    // If we get here, FENCE instructions didn't crash
    printf("\nPASS: FENCE and FENCE.I executed without error\n");
    
    return 0;
}
