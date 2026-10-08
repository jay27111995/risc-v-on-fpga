// Minimal ECALL test
#include "../lib/stdio.h"
#include "../lib/stdint.h"

volatile uint32_t handler_called = 0;

// Minimal ECALL handler
__attribute__((naked, aligned(4)))
void ecall_handler(void) {
    asm volatile (
        // Increment handler_called
        "la t0, handler_called\n"
        "lw t1, 0(t0)\n"
        "addi t1, t1, 1\n"
        "sw t1, 0(t0)\n"
        
        // Advance mepc past ecall (mepc += 4)
        "csrr t0, mepc\n"
        "addi t0, t0, 4\n"
        "csrw mepc, t0\n"
        
        // Return immediately after CSR write (forwarding should handle it)
        "mret\n"
    );
}

int main(void) {
    printf("\n=== Minimal ECALL Test ===\n\n");
    
    // Set up trap handler
    uint32_t handler_addr = (uint32_t)ecall_handler;
    printf("Setting mtvec = 0x%x\n", handler_addr);
    asm volatile ("csrw mtvec, %0" :: "r"(handler_addr));
    
    // Enable mstatus.MIE (so MRET can restore it)
    asm volatile ("csrw mstatus, %0" :: "r"(0x08));
    
    printf("handler_called before = %u\n", handler_called);
    printf("Calling ecall...\n");
    
    asm volatile ("ecall");
    
    printf("Returned! handler_called = %u\n", handler_called);
    
    if (handler_called == 1) {
        printf("\nPASS: ECALL works!\n");
    } else {
        printf("\nFAIL: handler_called = %u\n", handler_called);
    }
    
    puts("===END===");
    return (handler_called == 1) ? 0 : 1;
}
