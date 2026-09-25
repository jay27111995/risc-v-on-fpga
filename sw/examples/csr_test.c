// CSR Test Program
// Tests basic CSR read/write operations and timer

#include "../lib/stdio.h"
#include "../lib/stdint.h"

// CSR addresses
#define CSR_MSTATUS  0x300
#define CSR_MIE      0x304
#define CSR_MTVEC    0x305
#define CSR_MEPC     0x341
#define CSR_MCAUSE   0x342
#define CSR_MIP      0x344

// Read CSR
static inline uint32_t csr_read(int csr) {
    uint32_t val;
    switch (csr) {
        case CSR_MSTATUS: asm volatile ("csrr %0, mstatus" : "=r"(val)); break;
        case CSR_MIE:     asm volatile ("csrr %0, mie"     : "=r"(val)); break;
        case CSR_MTVEC:   asm volatile ("csrr %0, mtvec"   : "=r"(val)); break;
        case CSR_MEPC:    asm volatile ("csrr %0, mepc"    : "=r"(val)); break;
        case CSR_MCAUSE:  asm volatile ("csrr %0, mcause"  : "=r"(val)); break;
        case CSR_MIP:     asm volatile ("csrr %0, mip"     : "=r"(val)); break;
        default: val = 0;
    }
    return val;
}

// Write CSR
static inline void csr_write(int csr, uint32_t val) {
    switch (csr) {
        case CSR_MSTATUS: asm volatile ("csrw mstatus, %0" :: "r"(val)); break;
        case CSR_MIE:     asm volatile ("csrw mie, %0"     :: "r"(val)); break;
        case CSR_MTVEC:   asm volatile ("csrw mtvec, %0"   :: "r"(val)); break;
        case CSR_MEPC:    asm volatile ("csrw mepc, %0"    :: "r"(val)); break;
        case CSR_MCAUSE:  asm volatile ("csrw mcause, %0"  :: "r"(val)); break;
        default: break;
    }
}

// Test CSR read/write
int test_csr_rw(void) {
    printf("Testing CSR read/write...\n");
    
    // Test mtvec
    csr_write(CSR_MTVEC, 0x1234);
    uint32_t val = csr_read(CSR_MTVEC);
    printf("  mtvec: wrote 0x1234, read 0x%x - %s\n", val, val == 0x1234 ? "PASS" : "FAIL");
    if (val != 0x1234) return 1;
    
    // Test mepc
    csr_write(CSR_MEPC, 0xABCD0000);
    val = csr_read(CSR_MEPC);
    printf("  mepc: wrote 0xABCD0000, read 0x%x - %s\n", val, val == 0xABCD0000 ? "PASS" : "FAIL");
    if (val != 0xABCD0000) return 1;
    
    // Test mstatus
    csr_write(CSR_MSTATUS, 0x88);  // Set MIE and MPIE bits
    val = csr_read(CSR_MSTATUS);
    printf("  mstatus: wrote 0x88, read 0x%x - %s\n", val, val == 0x88 ? "PASS" : "FAIL");
    if (val != 0x88) return 1;
    
    // Test mie
    csr_write(CSR_MIE, 0x80);  // Enable timer interrupt
    val = csr_read(CSR_MIE);
    printf("  mie: wrote 0x80, read 0x%x - %s\n", val, val == 0x80 ? "PASS" : "FAIL");
    if (val != 0x80) return 1;
    
    return 0;
}

// Test CSRRS (set bits)
int test_csrrs(void) {
    printf("Testing CSRRS (set bits)...\n");
    
    csr_write(CSR_MSTATUS, 0x08);  // Start with bit 3 set
    asm volatile ("csrrs x0, mstatus, %0" :: "r"(0x80));  // Set bit 7
    uint32_t val = csr_read(CSR_MSTATUS);
    printf("  mstatus: started 0x08, set 0x80, result 0x%x - %s\n", val, val == 0x88 ? "PASS" : "FAIL");
    
    return (val == 0x88) ? 0 : 1;
}

// Test CSRRC (clear bits)
int test_csrrc(void) {
    printf("Testing CSRRC (clear bits)...\n");
    
    csr_write(CSR_MSTATUS, 0x88);  // Start with bits 3 and 7 set
    asm volatile ("csrrc x0, mstatus, %0" :: "r"(0x08));  // Clear bit 3
    uint32_t val = csr_read(CSR_MSTATUS);
    printf("  mstatus: started 0x88, cleared 0x08, result 0x%x - %s\n", val, val == 0x80 ? "PASS" : "FAIL");
    
    return (val == 0x80) ? 0 : 1;
}

int main(void) {
    printf("\n=== CSR Test Suite ===\n\n");
    
    int errors = 0;
    
    errors += test_csr_rw();
    errors += test_csrrs();
    errors += test_csrrc();
    
    printf("\n=== Results: %d errors ===\n", errors);
    
    if (errors == 0) {
        printf("All CSR tests PASSED!\n");
    } else {
        printf("Some tests FAILED.\n");
    }
    
    return errors;
}
