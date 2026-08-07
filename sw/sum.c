/* Simple test program for RISC-V SoC
 * Computes sum of 1 to 10 and stores result to DMEM[0]
 * (No multiply - RV32I doesn't have MUL instruction)
 */

// DMEM is at address 0 from CPU's perspective
volatile int *dmem = (volatile int *)0;

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
    
    // Store result to DMEM[0]
    dmem[0] = result;
    
    // Store some values for debugging
    dmem[1] = 10;     // input
    dmem[2] = 0xDEAD; // marker to show we got here
    
    // Also test some arithmetic
    dmem[3] = result + 5;    // 60
    dmem[4] = result - 5;    // 50
    dmem[5] = (result >> 1); // 27 (shift right)
    dmem[6] = (result << 2); // 220 (shift left)
    
    // Halt - infinite loop
    while(1);
}
