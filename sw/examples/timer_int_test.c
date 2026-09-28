// Timer Interrupt Test
// Tests that timer interrupts fire and MRET works correctly

#include "../lib/stdio.h"
#include "../lib/stdint.h"

// CSR addresses
#define CSR_MSTATUS  0x300
#define CSR_MIE      0x304
#define CSR_MTVEC    0x305
#define CSR_MEPC     0x341
#define CSR_MCAUSE   0x342
#define CSR_MIP      0x344

// Timer memory-mapped addresses
#define MTIME_LO     ((volatile uint32_t*)0x10001000)
#define MTIME_HI     ((volatile uint32_t*)0x10001004)
#define MTIMECMP_LO  ((volatile uint32_t*)0x10001008)
#define MTIMECMP_HI  ((volatile uint32_t*)0x1000100C)

// Global counter - incremented by interrupt handler
volatile uint32_t interrupt_count = 0;
volatile uint32_t last_mcause = 0;
volatile uint32_t last_mepc = 0;

// Interrupt handler (called from assembly trampoline)
void timer_handler(void) {
    // Save mcause and mepc for debugging
    asm volatile ("csrr %0, mcause" : "=r"(last_mcause));
    asm volatile ("csrr %0, mepc" : "=r"(last_mepc));
    
    // Increment counter
    interrupt_count++;
    
    // Clear the interrupt by setting mtimecmp far in the future
    *MTIMECMP_HI = 0xFFFFFFFF;
    *MTIMECMP_LO = 0xFFFFFFFF;
}

// Assembly interrupt trampoline
// Saves registers, calls C handler, restores registers, executes MRET
__attribute__((naked, aligned(4)))
void interrupt_trampoline(void) {
    asm volatile (
        // Save registers to stack
        "addi sp, sp, -64\n"
        "sw ra, 0(sp)\n"
        "sw t0, 4(sp)\n"
        "sw t1, 8(sp)\n"
        "sw t2, 12(sp)\n"
        "sw a0, 16(sp)\n"
        "sw a1, 20(sp)\n"
        "sw a2, 24(sp)\n"
        "sw a3, 28(sp)\n"
        "sw a4, 32(sp)\n"
        "sw a5, 36(sp)\n"
        "sw a6, 40(sp)\n"
        "sw a7, 44(sp)\n"
        "sw t3, 48(sp)\n"
        "sw t4, 52(sp)\n"
        "sw t5, 56(sp)\n"
        "sw t6, 60(sp)\n"
        
        // Call C handler
        "jal ra, timer_handler\n"
        
        // Restore registers
        "lw ra, 0(sp)\n"
        "lw t0, 4(sp)\n"
        "lw t1, 8(sp)\n"
        "lw t2, 12(sp)\n"
        "lw a0, 16(sp)\n"
        "lw a1, 20(sp)\n"
        "lw a2, 24(sp)\n"
        "lw a3, 28(sp)\n"
        "lw a4, 32(sp)\n"
        "lw a5, 36(sp)\n"
        "lw a6, 40(sp)\n"
        "lw a7, 44(sp)\n"
        "lw t3, 48(sp)\n"
        "lw t4, 52(sp)\n"
        "lw t5, 56(sp)\n"
        "lw t6, 60(sp)\n"
        "addi sp, sp, 64\n"
        
        // Return from interrupt
        "mret\n"
    );
}

// Read 64-bit mtime
uint64_t read_mtime(void) {
    uint32_t lo, hi, hi2;
    do {
        hi = *MTIME_HI;
        lo = *MTIME_LO;
        hi2 = *MTIME_HI;
    } while (hi != hi2);  // Handle rollover
    return ((uint64_t)hi << 32) | lo;
}

// Set mtimecmp
void set_mtimecmp(uint64_t val) {
    // Write high first to prevent spurious interrupts
    *MTIMECMP_HI = 0xFFFFFFFF;
    *MTIMECMP_LO = (uint32_t)val;
    *MTIMECMP_HI = (uint32_t)(val >> 32);
}

int main(void) {
    printf("\n=== Timer Interrupt Test ===\n\n");
    
    // Step 1: Set up interrupt vector
    printf("1. Setting up mtvec...\n");
    uint32_t handler_addr = (uint32_t)interrupt_trampoline;
    asm volatile ("csrw mtvec, %0" :: "r"(handler_addr));
    
    uint32_t mtvec_check;
    asm volatile ("csrr %0, mtvec" : "=r"(mtvec_check));
    printf("   mtvec = 0x%x\n", mtvec_check);
    
    // Step 2: Read current time
    printf("2. Reading mtime...\n");
    uint64_t now = read_mtime();
    printf("   mtime = %u (low 32 bits)\n", (uint32_t)now);
    
    // Step 3: Set mtimecmp to trigger soon (1000 cycles from now)
    printf("3. Setting mtimecmp = mtime + 1000...\n");
    set_mtimecmp(now + 1000);
    
    // Step 4: Enable timer interrupt in mie
    printf("4. Enabling timer interrupt (mie.MTIE)...\n");
    asm volatile ("csrw mie, %0" :: "r"(0x80));  // Bit 7 = MTIE
    
    // Step 5: Enable global interrupts in mstatus
    printf("5. Enabling global interrupts (mstatus.MIE)...\n");
    printf("   interrupt_count before = %u\n", interrupt_count);
    asm volatile ("csrw mstatus, %0" :: "r"(0x08));  // Bit 3 = MIE
    
    // Step 6: Wait for interrupt
    printf("6. Waiting for interrupt...\n");
    
    // Spin wait - interrupt should fire and increment counter
    for (volatile int i = 0; i < 10000; i++) {
        if (interrupt_count > 0) break;
    }
    
    // Step 7: Check results
    printf("\n=== Results ===\n");
    printf("   interrupt_count = %u\n", interrupt_count);
    printf("   last_mcause = 0x%x\n", last_mcause);
    printf("   last_mepc = 0x%x\n", last_mepc);
    
    // Verify
    if (interrupt_count > 0) {
        printf("\n   Timer interrupt: PASSED!\n");
        
        // Check mcause = 0x80000007 (timer interrupt)
        if (last_mcause == 0x80000007) {
            printf("   mcause check: PASSED (timer interrupt)\n");
        } else {
            printf("   mcause check: FAILED (expected 0x80000007)\n");
        }
        
        printf("\n   MRET worked - we returned from interrupt!\n");
    } else {
        printf("\n   Timer interrupt: FAILED (no interrupt received)\n");
        
        // Debug info
        uint32_t mstatus, mie, mip;
        asm volatile ("csrr %0, mstatus" : "=r"(mstatus));
        asm volatile ("csrr %0, mie" : "=r"(mie));
        asm volatile ("csrr %0, mip" : "=r"(mip));
        printf("   Debug: mstatus=0x%x mie=0x%x mip=0x%x\n", mstatus, mie, mip);
        printf("   Debug: mtime=%u mtimecmp_lo=%u\n", *MTIME_LO, *MTIMECMP_LO);
    }
    
    printf("\n=== Test Complete ===\n");
    
    return (interrupt_count > 0) ? 0 : 1;
}
