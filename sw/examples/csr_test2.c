// Comprehensive CSR Test Program
// Tests corner cases and edge conditions

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

static int errors = 0;

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

void check(const char* name, uint32_t expected, uint32_t actual) {
    if (expected == actual) {
        printf("  PASS: %s = 0x%x\n", name, actual);
    } else {
        printf("  FAIL: %s expected 0x%x, got 0x%x\n", name, expected, actual);
        errors++;
    }
}

// Test 1: Back-to-back CSR writes to same register
void test_back_to_back_writes(void) {
    printf("\n=== Test: Back-to-back writes ===\n");
    
    csr_write(CSR_MTVEC, 0x1000);
    csr_write(CSR_MTVEC, 0x2000);
    csr_write(CSR_MTVEC, 0x3000);
    uint32_t val = csr_read(CSR_MTVEC);
    check("mtvec after 3 writes", 0x3000, val);
}

// Test 2: Write then immediate read (forwarding test)
void test_write_read_forwarding(void) {
    printf("\n=== Test: Write-read forwarding ===\n");
    
    // Single instruction gap
    csr_write(CSR_MEPC, 0xDEADBEEF);
    uint32_t val = csr_read(CSR_MEPC);
    check("mepc immediate read", 0xDEADBEEF, val);
    
    // Test with different CSRs
    csr_write(CSR_MTVEC, 0x12345678);
    val = csr_read(CSR_MTVEC);
    check("mtvec immediate read", 0x12345678, val);
}

// Test 3: CSRRW returns old value
void test_csrrw_return_value(void) {
    printf("\n=== Test: CSRRW return value ===\n");
    
    csr_write(CSR_MSTATUS, 0xAA);
    
    uint32_t old_val;
    asm volatile ("csrrw %0, mstatus, %1" : "=r"(old_val) : "r"(0x55));
    check("csrrw old value", 0xAA, old_val);
    
    uint32_t new_val = csr_read(CSR_MSTATUS);
    check("csrrw new value", 0x55, new_val);
}

// Test 4: CSRRS returns old value and sets bits
void test_csrrs_return_value(void) {
    printf("\n=== Test: CSRRS return value ===\n");
    
    csr_write(CSR_MSTATUS, 0x0F);
    
    uint32_t old_val;
    asm volatile ("csrrs %0, mstatus, %1" : "=r"(old_val) : "r"(0xF0));
    check("csrrs old value", 0x0F, old_val);
    
    uint32_t new_val = csr_read(CSR_MSTATUS);
    check("csrrs new value (0x0F | 0xF0)", 0xFF, new_val);
}

// Test 5: CSRRC returns old value and clears bits
void test_csrrc_return_value(void) {
    printf("\n=== Test: CSRRC return value ===\n");
    
    csr_write(CSR_MSTATUS, 0xFF);
    
    uint32_t old_val;
    asm volatile ("csrrc %0, mstatus, %1" : "=r"(old_val) : "r"(0x0F));
    check("csrrc old value", 0xFF, old_val);
    
    uint32_t new_val = csr_read(CSR_MSTATUS);
    check("csrrc new value (0xFF & ~0x0F)", 0xF0, new_val);
}

// Test 6: CSRRS/CSRRC with x0 (read-only, no modify)
void test_csr_readonly_ops(void) {
    printf("\n=== Test: CSR read-only ops (rs1=x0) ===\n");
    
    csr_write(CSR_MSTATUS, 0x42);
    
    // CSRRS with x0 should only read, not modify
    uint32_t val;
    asm volatile ("csrrs %0, mstatus, x0" : "=r"(val));
    check("csrrs x0 reads value", 0x42, val);
    
    val = csr_read(CSR_MSTATUS);
    check("csrrs x0 didn't modify", 0x42, val);
    
    // CSRRC with x0 should only read, not modify
    asm volatile ("csrrc %0, mstatus, x0" : "=r"(val));
    check("csrrc x0 reads value", 0x42, val);
    
    val = csr_read(CSR_MSTATUS);
    check("csrrc x0 didn't modify", 0x42, val);
}

// Test 7: Immediate CSR instructions
void test_csr_immediate(void) {
    printf("\n=== Test: CSR immediate instructions ===\n");
    
    csr_write(CSR_MSTATUS, 0x00);
    
    // CSRRSI - set bits with immediate
    asm volatile ("csrrsi x0, mstatus, 0x1F");  // Set bits 0-4
    uint32_t val = csr_read(CSR_MSTATUS);
    check("csrrsi 0x1F", 0x1F, val);
    
    // CSRRCI - clear bits with immediate
    asm volatile ("csrrci x0, mstatus, 0x0A");  // Clear bits 1,3
    val = csr_read(CSR_MSTATUS);
    check("csrrci 0x0A (0x1F & ~0x0A)", 0x15, val);
    
    // CSRRWI - write immediate
    asm volatile ("csrrwi x0, mstatus, 0x07");
    val = csr_read(CSR_MSTATUS);
    check("csrrwi 0x07", 0x07, val);
}

// Test 8: All bits of 32-bit CSR
void test_full_width(void) {
    printf("\n=== Test: Full 32-bit width ===\n");
    
    csr_write(CSR_MEPC, 0xFFFFFFFF);
    uint32_t val = csr_read(CSR_MEPC);
    check("mepc all 1s", 0xFFFFFFFF, val);
    
    csr_write(CSR_MEPC, 0x00000000);
    val = csr_read(CSR_MEPC);
    check("mepc all 0s", 0x00000000, val);
    
    csr_write(CSR_MEPC, 0xAAAAAAAA);
    val = csr_read(CSR_MEPC);
    check("mepc 0xAAAAAAAA", 0xAAAAAAAA, val);
    
    csr_write(CSR_MEPC, 0x55555555);
    val = csr_read(CSR_MEPC);
    check("mepc 0x55555555", 0x55555555, val);
}

// Test 9: Timer read (mtime should be incrementing)
void test_timer_read(void) {
    printf("\n=== Test: Timer read ===\n");
    
    uint32_t t1 = *MTIME_LO;
    // Small delay
    for (volatile int i = 0; i < 100; i++);
    uint32_t t2 = *MTIME_LO;
    
    if (t2 > t1) {
        printf("  PASS: mtime incrementing (%u -> %u)\n", t1, t2);
    } else {
        printf("  FAIL: mtime not incrementing (%u -> %u)\n", t1, t2);
        errors++;
    }
}

// Test 10: Timer compare write
void test_timer_compare(void) {
    printf("\n=== Test: Timer compare write ===\n");
    
    // Write a value to mtimecmp
    *MTIMECMP_LO = 0x12345678;
    *MTIMECMP_HI = 0x9ABCDEF0;
    
    // Read it back
    uint32_t lo = *MTIMECMP_LO;
    uint32_t hi = *MTIMECMP_HI;
    
    check("mtimecmp_lo", 0x12345678, lo);
    check("mtimecmp_hi", 0x9ABCDEF0, hi);
}

// Test 11: Interleaved CSR operations on different registers
void test_interleaved_ops(void) {
    printf("\n=== Test: Interleaved CSR ops ===\n");
    
    csr_write(CSR_MTVEC, 0x1111);
    csr_write(CSR_MEPC, 0x2222);
    csr_write(CSR_MSTATUS, 0x33);
    csr_write(CSR_MIE, 0x44);
    
    // Read in different order
    uint32_t mie = csr_read(CSR_MIE);
    uint32_t mtvec = csr_read(CSR_MTVEC);
    uint32_t mstatus = csr_read(CSR_MSTATUS);
    uint32_t mepc = csr_read(CSR_MEPC);
    
    check("mtvec", 0x1111, mtvec);
    check("mepc", 0x2222, mepc);
    check("mstatus", 0x33, mstatus);
    check("mie", 0x44, mie);
}

// Test 12: Rapid alternating read/write
void test_rapid_alternating(void) {
    printf("\n=== Test: Rapid alternating R/W ===\n");
    
    for (int i = 0; i < 10; i++) {
        csr_write(CSR_MTVEC, i * 0x100);
        uint32_t val = csr_read(CSR_MTVEC);
        if (val != (uint32_t)(i * 0x100)) {
            printf("  FAIL: iteration %d, expected 0x%x, got 0x%x\n", i, i * 0x100, val);
            errors++;
            return;
        }
    }
    printf("  PASS: 10 rapid R/W cycles\n");
}

// Test 13: CSR forwarding chain (write-write-read)
void test_forwarding_chain(void) {
    printf("\n=== Test: Forwarding chain ===\n");
    
    // Write A, Write B, Read should get B
    csr_write(CSR_MTVEC, 0xAAAA);
    csr_write(CSR_MTVEC, 0xBBBB);
    uint32_t val = csr_read(CSR_MTVEC);
    check("write-write-read chain", 0xBBBB, val);
}

int main(void) {
    printf("\n========================================\n");
    printf("   Comprehensive CSR Test Suite\n");
    printf("========================================\n");
    
    test_back_to_back_writes();
    test_write_read_forwarding();
    test_csrrw_return_value();
    test_csrrs_return_value();
    test_csrrc_return_value();
    test_csr_readonly_ops();
    test_csr_immediate();
    test_full_width();
    test_timer_read();
    test_timer_compare();
    test_interleaved_ops();
    test_rapid_alternating();
    test_forwarding_chain();
    
    printf("\n========================================\n");
    printf("   Total errors: %d\n", errors);
    printf("========================================\n");
    
    if (errors == 0) {
        printf("   ALL TESTS PASSED!\n");
    } else {
        printf("   SOME TESTS FAILED!\n");
    }
    printf("========================================\n");
    
    return errors;
}
