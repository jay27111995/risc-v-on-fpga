/* Simple test program for RISC-V SoC
 * Computes factorial of 5 and stores result to DMEM[0]
 */

// DMEM is at address 0 from CPU's perspective
volatile int *dmem = (volatile int *)0;

int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

void _start(void) {
    // Compute factorial(5) = 120
    int result = factorial(5);
    
    // Store result to DMEM[0]
    dmem[0] = result;
    
    // Store intermediate values for debugging
    dmem[1] = 5;      // input
    dmem[2] = 0xDEAD; // marker to show we got here
    
    // Halt - infinite loop
    while(1);
}
