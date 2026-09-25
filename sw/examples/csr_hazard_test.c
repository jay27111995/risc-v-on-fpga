// CSR Pipeline Hazard Tests
// Tests specifically for forwarding and hazard edge cases

#include "../lib/stdio.h"
#include "../lib/stdint.h"

#define CSR_MSTATUS  0x300
#define CSR_MIE      0x304
#define CSR_MTVEC    0x305
#define CSR_MEPC     0x341

static int errors = 0;

void check(const char* name, uint32_t expected, uint32_t actual) {
    if (expected == actual) {
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s - expected 0x%x, got 0x%x\n", name, expected, actual);
        errors++;
    }
}

// Test: CSR write followed immediately by CSR read of SAME register
void test_same_reg_hazard(void) {
    printf("\n=== Same register hazard ===\n");
    
    uint32_t result;
    
    // Pattern 1: csrw then csrr (1 instruction apart)
    asm volatile (
        "li t0, 0x1234\n"
        "csrw mtvec, t0\n"
        "csrr %0, mtvec\n"
        : "=r"(result)
        :
        : "t0"
    );
    check("csrw-csrr (0 gap)", 0x1234, result);
    
    // Pattern 2: csrw, nop, csrr (1 instruction gap)  
    asm volatile (
        "li t0, 0x5678\n"
        "csrw mtvec, t0\n"
        "nop\n"
        "csrr %0, mtvec\n"
        : "=r"(result)
        :
        : "t0"
    );
    check("csrw-nop-csrr (1 gap)", 0x5678, result);
    
    // Pattern 3: csrw, 2 nops, csrr
    asm volatile (
        "li t0, 0x9ABC\n"
        "csrw mtvec, t0\n"
        "nop\n"
        "nop\n"
        "csrr %0, mtvec\n"
        : "=r"(result)
        :
        : "t0"
    );
    check("csrw-nop-nop-csrr (2 gap)", 0x9ABC, result);
}

// Test: CSR write to reg A, then read reg B (no hazard expected)
void test_different_reg_no_hazard(void) {
    printf("\n=== Different register (no hazard) ===\n");
    
    uint32_t result;
    
    // Set up initial values
    asm volatile ("csrw mepc, %0" :: "r"(0xAAAA));
    
    // Write mtvec, read mepc - should not forward
    asm volatile (
        "li t0, 0x1111\n"
        "csrw mtvec, t0\n"
        "csrr %0, mepc\n"
        : "=r"(result)
        :
        : "t0"
    );
    check("write mtvec, read mepc", 0xAAAA, result);
}

// Test: Multiple CSR writes in sequence, then read
void test_write_sequence(void) {
    printf("\n=== Write sequence then read ===\n");
    
    uint32_t r1, r2, r3;
    
    asm volatile (
        "li t0, 0x1000\n"
        "li t1, 0x2000\n"
        "li t2, 0x3000\n"
        "csrw mtvec, t0\n"
        "csrw mepc, t1\n"
        "csrw mstatus, t2\n"
        "csrr %0, mtvec\n"
        "csrr %1, mepc\n"
        "csrr %2, mstatus\n"
        : "=r"(r1), "=r"(r2), "=r"(r3)
        :
        : "t0", "t1", "t2"
    );
    
    check("mtvec after sequence", 0x1000, r1);
    check("mepc after sequence", 0x2000, r2);
    check("mstatus after sequence", 0x3000, r3);
}

// Test: CSRRW chain - each should see previous value
void test_csrrw_chain(void) {
    printf("\n=== CSRRW chain ===\n");
    
    uint32_t v1, v2, v3;
    
    // Initialize
    asm volatile ("csrw mtvec, %0" :: "r"(0x0000));
    
    // Chain of CSRRWs - each returns old value
    asm volatile (
        "li t0, 0x1111\n"
        "li t1, 0x2222\n"
        "li t2, 0x3333\n"
        "csrrw %0, mtvec, t0\n"  // Returns 0x0000, writes 0x1111
        "csrrw %1, mtvec, t1\n"  // Returns 0x1111, writes 0x2222
        "csrrw %2, mtvec, t2\n"  // Returns 0x2222, writes 0x3333
        : "=r"(v1), "=r"(v2), "=r"(v3)
        :
        : "t0", "t1", "t2"
    );
    
    check("csrrw chain v1 (old=0x0000)", 0x0000, v1);
    check("csrrw chain v2 (old=0x1111)", 0x1111, v2);
    check("csrrw chain v3 (old=0x2222)", 0x2222, v3);
    
    // Final value should be 0x3333
    uint32_t final;
    asm volatile ("csrr %0, mtvec" : "=r"(final));
    check("csrrw chain final", 0x3333, final);
}

// Test: CSRRS chain - set bits progressively  
void test_csrrs_chain(void) {
    printf("\n=== CSRRS chain ===\n");
    
    uint32_t v1, v2, v3;
    
    asm volatile ("csrw mstatus, %0" :: "r"(0x00));
    
    asm volatile (
        "li t0, 0x01\n"
        "li t1, 0x02\n"
        "li t2, 0x04\n"
        "csrrs %0, mstatus, t0\n"  // 0x00 | 0x01 = 0x01, returns 0x00
        "csrrs %1, mstatus, t1\n"  // 0x01 | 0x02 = 0x03, returns 0x01
        "csrrs %2, mstatus, t2\n"  // 0x03 | 0x04 = 0x07, returns 0x03
        : "=r"(v1), "=r"(v2), "=r"(v3)
        :
        : "t0", "t1", "t2"
    );
    
    check("csrrs chain v1 (old=0x00)", 0x00, v1);
    check("csrrs chain v2 (old=0x01)", 0x01, v2);
    check("csrrs chain v3 (old=0x03)", 0x03, v3);
    
    uint32_t final;
    asm volatile ("csrr %0, mstatus" : "=r"(final));
    check("csrrs chain final (0x07)", 0x07, final);
}

// Test: CSRRC chain - clear bits progressively
void test_csrrc_chain(void) {
    printf("\n=== CSRRC chain ===\n");
    
    uint32_t v1, v2, v3;
    
    asm volatile ("csrw mstatus, %0" :: "r"(0xFF));
    
    asm volatile (
        "li t0, 0x01\n"
        "li t1, 0x02\n"
        "li t2, 0x04\n"
        "csrrc %0, mstatus, t0\n"  // 0xFF & ~0x01 = 0xFE, returns 0xFF
        "csrrc %1, mstatus, t1\n"  // 0xFE & ~0x02 = 0xFC, returns 0xFE
        "csrrc %2, mstatus, t2\n"  // 0xFC & ~0x04 = 0xF8, returns 0xFC
        : "=r"(v1), "=r"(v2), "=r"(v3)
        :
        : "t0", "t1", "t2"
    );
    
    check("csrrc chain v1 (old=0xFF)", 0xFF, v1);
    check("csrrc chain v2 (old=0xFE)", 0xFE, v2);
    check("csrrc chain v3 (old=0xFC)", 0xFC, v3);
    
    uint32_t final;
    asm volatile ("csrr %0, mstatus" : "=r"(final));
    check("csrrc chain final (0xF8)", 0xF8, final);
}

// Test: Mixed operations on same register
void test_mixed_ops(void) {
    printf("\n=== Mixed ops on same register ===\n");
    
    uint32_t v1, v2, v3;
    
    asm volatile ("csrw mstatus, %0" :: "r"(0x0F));
    
    asm volatile (
        "li t0, 0xF0\n"
        "li t1, 0x05\n"
        "li t2, 0xAA\n"
        "csrrs %0, mstatus, t0\n"  // 0x0F | 0xF0 = 0xFF, returns 0x0F
        "csrrc %1, mstatus, t1\n"  // 0xFF & ~0x05 = 0xFA, returns 0xFF
        "csrrw %2, mstatus, t2\n"  // writes 0xAA, returns 0xFA
        : "=r"(v1), "=r"(v2), "=r"(v3)
        :
        : "t0", "t1", "t2"
    );
    
    check("mixed v1 (csrrs old=0x0F)", 0x0F, v1);
    check("mixed v2 (csrrc old=0xFF)", 0xFF, v2);
    check("mixed v3 (csrrw old=0xFA)", 0xFA, v3);
    
    uint32_t final;
    asm volatile ("csrr %0, mstatus" : "=r"(final));
    check("mixed final (0xAA)", 0xAA, final);
}

// Test: Interleaved ops on different registers
void test_interleaved_different_regs(void) {
    printf("\n=== Interleaved on different regs ===\n");
    
    uint32_t r1, r2, r3, r4;
    
    asm volatile (
        "li t0, 0x11\n"
        "li t1, 0x22\n"
        "li t2, 0x33\n"
        "li t3, 0x44\n"
        "csrw mtvec, t0\n"
        "csrw mepc, t1\n"
        "csrr %0, mtvec\n"   // Should get 0x11
        "csrw mtvec, t2\n"
        "csrr %1, mepc\n"    // Should get 0x22
        "csrr %2, mtvec\n"   // Should get 0x33
        "csrw mepc, t3\n"
        "csrr %3, mepc\n"    // Should get 0x44
        : "=r"(r1), "=r"(r2), "=r"(r3), "=r"(r4)
        :
        : "t0", "t1", "t2", "t3"
    );
    
    check("interleaved r1 (mtvec=0x11)", 0x11, r1);
    check("interleaved r2 (mepc=0x22)", 0x22, r2);
    check("interleaved r3 (mtvec=0x33)", 0x33, r3);
    check("interleaved r4 (mepc=0x44)", 0x44, r4);
}

int main(void) {
    printf("\n========================================\n");
    printf("   CSR Pipeline Hazard Tests\n");
    printf("========================================\n");
    
    test_same_reg_hazard();
    test_different_reg_no_hazard();
    test_write_sequence();
    test_csrrw_chain();
    test_csrrs_chain();
    test_csrrc_chain();
    test_mixed_ops();
    test_interleaved_different_regs();
    
    printf("\n========================================\n");
    printf("   Total errors: %d\n", errors);
    printf("========================================\n");
    
    if (errors == 0) {
        printf("   ALL TESTS PASSED!\n");
    }
    printf("========================================\n");
    
    return errors;
}
