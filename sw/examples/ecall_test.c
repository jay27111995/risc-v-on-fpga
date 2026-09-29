// Test for mscratch, mcycle, minstret, and ECALL
#include "../lib/stdio.h"
#include "../lib/stdint.h"

#define CSR_MSTATUS   0x300
#define CSR_MTVEC     0x305
#define CSR_MSCRATCH  0x340
#define CSR_MEPC      0x341
#define CSR_MCAUSE    0x342
#define CSR_MCYCLE    0xB00
#define CSR_MCYCLEH   0xB80
#define CSR_MINSTRET  0xB02
#define CSR_MINSTRETH 0xB82

volatile uint32_t ecall_count = 0;
volatile uint32_t ecall_mepc = 0;
volatile uint32_t ecall_mcause = 0;

// ECALL handler
__attribute__((naked, aligned(4)))
void ecall_handler(void) {
    asm volatile (
        // Save registers
        "addi sp, sp, -16\n"
        "sw ra, 0(sp)\n"
        "sw t0, 4(sp)\n"
        "sw t1, 8(sp)\n"
        
        // Read mepc and mcause
        "csrr t0, mepc\n"
        "la t1, ecall_mepc\n"
        "sw t0, 0(t1)\n"
        
        "csrr t0, mcause\n"
        "la t1, ecall_mcause\n"
        "sw t0, 0(t1)\n"
        
        // Increment ecall_count
        "la t0, ecall_count\n"
        "lw t1, 0(t0)\n"
        "addi t1, t1, 1\n"
        "sw t1, 0(t0)\n"
        
        // Advance mepc past the ecall instruction (mepc += 4)
        "csrr t0, mepc\n"
        "addi t0, t0, 4\n"
        "csrw mepc, t0\n"
        
        // Restore registers
        "lw ra, 0(sp)\n"
        "lw t0, 4(sp)\n"
        "lw t1, 8(sp)\n"
        "addi sp, sp, 16\n"
        
        // Return from exception
        "mret\n"
    );
}

int main(void) {
    printf("\n=== mscratch/mcycle/ecall Test ===\n\n");
    int errors = 0;
    
    // Test 1: mscratch read/write
    printf("1. Testing mscratch...\n");
    asm volatile ("csrw mscratch, %0" :: "r"(0xDEADBEEF));
    uint32_t scratch;
    asm volatile ("csrr %0, mscratch" : "=r"(scratch));
    if (scratch == 0xDEADBEEF) {
        printf("   PASS: mscratch = %x\n", scratch);
    } else {
        printf("   FAIL: mscratch = %x (expected 0xDEADBEEF)\n", scratch);
        errors++;
    }
    
    // Test 2: mcycle is incrementing
    printf("2. Testing mcycle...\n");
    uint32_t cycle1, cycle2;
    asm volatile ("csrr %0, mcycle" : "=r"(cycle1));
    for (volatile int i = 0; i < 100; i++);
    asm volatile ("csrr %0, mcycle" : "=r"(cycle2));
    if (cycle2 > cycle1) {
        printf("   PASS: mcycle incrementing (%u -> %u)\n", cycle1, cycle2);
    } else {
        printf("   FAIL: mcycle not incrementing (%u -> %u)\n", cycle1, cycle2);
        errors++;
    }
    
    // Test 3: minstret is incrementing
    printf("3. Testing minstret...\n");
    uint32_t instr1, instr2;
    asm volatile ("csrr %0, minstret" : "=r"(instr1));
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("csrr %0, minstret" : "=r"(instr2));
    if (instr2 > instr1) {
        printf("   PASS: minstret incrementing (%u -> %u, diff=%u)\n", instr1, instr2, instr2-instr1);
    } else {
        printf("   FAIL: minstret not incrementing (%u -> %u)\n", instr1, instr2);
        errors++;
    }
    
    // Test 4: ECALL
    printf("4. Testing ECALL...\n");
    
    // Set up trap handler
    uint32_t handler_addr = (uint32_t)ecall_handler;
    asm volatile ("csrw mtvec, %0" :: "r"(handler_addr));
    
    // Enable interrupts (needed for MRET to re-enable)
    asm volatile ("csrw mstatus, %0" :: "r"(0x08));
    
    printf("   Calling ecall...\n");
    ecall_count = 0;
    
    // Trigger ECALL
    asm volatile ("ecall");
    
    printf("   Returned from ecall!\n");
    printf("   ecall_count = %u\n", ecall_count);
    printf("   ecall_mepc = %x\n", ecall_mepc);
    printf("   ecall_mcause = %u\n", ecall_mcause);
    
    if (ecall_count == 1) {
        printf("   PASS: ECALL triggered handler\n");
    } else {
        printf("   FAIL: ECALL count = %u (expected 1)\n", ecall_count);
        errors++;
    }
    
    if (ecall_mcause == 11) {
        printf("   PASS: mcause = 11 (M-mode ecall)\n");
    } else {
        printf("   FAIL: mcause = %u (expected 11)\n", ecall_mcause);
        errors++;
    }
    
    // Test 5: Multiple ECALLs
    printf("5. Testing single ECALL with debug...\n");
    ecall_count = 0;
    printf("   Before ECALL, count=%u\n", ecall_count);
    asm volatile ("ecall");
    printf("   After ECALL 1, count=%u, mepc=%x\n", ecall_count, ecall_mepc);
    asm volatile ("ecall");
    printf("   After ECALL 2, count=%u, mepc=%x\n", ecall_count, ecall_mepc);
    asm volatile ("ecall");
    printf("   After ECALL 3, count=%u, mepc=%x\n", ecall_count, ecall_mepc);

    if (ecall_count == 3) {
        printf("   PASS: 3 ECALLs triggered %u handlers\n", ecall_count);
    } else {
        printf("   FAIL: ecall_count = %u (expected 3)\n", ecall_count);
        errors++;
    }
    
    printf("\n=== Results: %d errors ===\n", errors);
    if (errors == 0) {
        printf("ALL TESTS PASSED!\n");
    }
    
    // Halt here to prevent restart
    printf("DONE - halting\n");
    while(1);
    
    return errors;
}
