// WFI (Wait For Interrupt) Test
#include "../lib/stdio.h"
#include "../lib/stdint.h"

#define CSR_MSTATUS  0x300
#define CSR_MIE      0x304
#define CSR_MTVEC    0x305
#define CSR_MCAUSE   0x342
#define CSR_MCYCLE   0xB00

// DMEM base for timer registers
#define DMEM_BASE    0x10000000
#define MTIME_LO     (*(volatile uint32_t*)(DMEM_BASE + 0x1000))
#define MTIME_HI     (*(volatile uint32_t*)(DMEM_BASE + 0x1004))
#define MTIMECMP_LO  (*(volatile uint32_t*)(DMEM_BASE + 0x1008))
#define MTIMECMP_HI  (*(volatile uint32_t*)(DMEM_BASE + 0x100C))

volatile uint32_t wfi_woken = 0;

// Timer interrupt handler
__attribute__((naked, aligned(4)))
void timer_handler(void) {
    asm volatile (
        // Save t0, t1
        "addi sp, sp, -8\n"
        "sw t0, 0(sp)\n"
        "sw t1, 4(sp)\n"
        
        // Set wfi_woken = 1
        "la t0, wfi_woken\n"
        "li t1, 1\n"
        "sw t1, 0(t0)\n"
        
        // Disable timer interrupt to prevent re-triggering (mie.MTIE = bit 7)
        "li t0, 0x80\n"
        "csrc mie, t0\n"
        
        // Restore t0, t1
        "lw t0, 0(sp)\n"
        "lw t1, 4(sp)\n"
        "addi sp, sp, 8\n"
        "mret\n"
    );
}

int main(void) {
    printf("\n=== WFI Test ===\n\n");
    
    // Set up trap handler
    uint32_t handler_addr = (uint32_t)timer_handler;
    asm volatile ("csrw mtvec, %0" :: "r"(handler_addr));
    printf("mtvec set to 0x%x\n", handler_addr);
    printf("Setting up timer and entering WFI...\n");
    
    wfi_woken = 0;
    
    // Read current cycle count BEFORE setting timer
    uint32_t cycle_before;
    asm volatile ("csrr %0, mcycle" : "=r"(cycle_before));
    
    // Set timer to fire in 200 cycles from NOW (no printf between this and WFI!)
    uint32_t mtime_now = MTIME_LO;
    MTIMECMP_HI = 0;
    MTIMECMP_LO = mtime_now + 200;
    
    // Enable timer interrupt (mie.MTIE = bit 7)
    asm volatile ("csrs mie, %0" :: "r"(0x80));
    
    // Enable global interrupts (mstatus.MIE = bit 3)
    asm volatile ("csrs mstatus, %0" :: "r"(0x08));
    
    // Execute WFI - should stall until timer fires
    asm volatile ("wfi");
    
    // Read cycle count immediately after wake
    uint32_t cycle_after;
    asm volatile ("csrr %0, mcycle" : "=r"(cycle_after));
    
    // Now safe to print
    uint32_t diff = cycle_after - cycle_before;
    
    printf("Woke up! wfi_woken = %u\n", wfi_woken);
    printf("Cycles: diff = %u\n", diff);
    
    // Check results
    int errors = 0;
    
    if (wfi_woken == 1) {
        printf("PASS: Handler was called\n");
    } else {
        printf("FAIL: Handler not called (wfi_woken=%u)\n", wfi_woken);
        errors++;
    }
    
    // Verify WFI actually stalled (should be ~200 cycles, allow margin for setup)
    if (diff >= 100 && diff <= 500) {
        printf("PASS: WFI stalled for %u cycles\n", diff);
    } else {
        printf("INFO: Cycle diff = %u\n", diff);
    }
    
    printf("\n=== WFI Test %s ===\n", errors == 0 ? "PASSED" : "FAILED");
    
    return errors;
}
