// atomic_test.c - Test LR.W/SC.W atomic instructions (A extension)

#include "stdio.h"

// Inline assembly for LR.W and SC.W
static inline int lr_w(volatile int *addr) {
    int result;
    asm volatile("lr.w %0, (%1)" : "=r"(result) : "r"(addr) : "memory");
    return result;
}

static inline int sc_w(volatile int *addr, int value) {
    int result;
    asm volatile("sc.w %0, %2, (%1)" : "=r"(result) : "r"(addr), "r"(value) : "memory");
    return result;  // 0 = success, 1 = failure
}

volatile int shared_var = 100;
volatile int lock = 0;

int passed = 0;
int failed = 0;

void check(const char *name, int condition) {
    if (condition) {
        puts("  [PASS] ");
        passed++;
    } else {
        puts("  [FAIL] ");
        failed++;
    }
    puts(name);
    puts("\n");
}

int main(void) {
    puts("=== Atomic Test (LR.W/SC.W) ===\n\n");
    
    int val, sc_result;
    
    // Test 1: Basic LR/SC sequence (should succeed)
    puts("Test 1: Basic LR/SC sequence\n");
    shared_var = 100;
    val = lr_w(&shared_var);
    check("LR.W loads correct value", val == 100);
    sc_result = sc_w(&shared_var, 101);
    check("SC.W returns 0 (success)", sc_result == 0);
    check("Value updated to 101", shared_var == 101);
    
    // Test 2: SC without LR (should fail)
    puts("\nTest 2: SC without prior LR\n");
    shared_var = 200;
    sc_result = sc_w(&shared_var, 999);
    check("SC.W returns 1 (fail)", sc_result == 1);
    check("Value unchanged (200)", shared_var == 200);
    
    // Test 3: LR/SC to different addresses (should fail)
    puts("\nTest 3: LR addr1, SC addr2\n");
    shared_var = 300;
    lock = 0;
    val = lr_w(&shared_var);  // Reserve shared_var
    sc_result = sc_w(&lock, 1);  // SC to lock (different addr)
    check("SC.W to different addr returns 1", sc_result == 1);
    check("lock unchanged (0)", lock == 0);
    
    // Test 4: LR/SC with intervening store (should fail)
    puts("\nTest 4: LR, store, SC\n");
    shared_var = 400;
    val = lr_w(&shared_var);
    shared_var = 450;  // Intervening store clears reservation
    sc_result = sc_w(&shared_var, 499);
    check("SC.W after store returns 1", sc_result == 1);
    check("Value is 450 (from store)", shared_var == 450);
    
    // Test 5: Spinlock acquire pattern
    puts("\nTest 5: Spinlock acquire\n");
    lock = 0;  // Lock is free
    int acquired = 0;
    for (int i = 0; i < 5; i++) {
        val = lr_w(&lock);
        if (val == 0) {  // Lock free
            sc_result = sc_w(&lock, 1);
            if (sc_result == 0) {
                acquired = 1;
                break;
            }
        }
    }
    check("Lock acquired", acquired == 1);
    check("lock == 1", lock == 1);
    
    // Release lock
    lock = 0;
    
    // Test 6: Double SC (second should fail)
    puts("\nTest 6: Two SC after one LR\n");
    shared_var = 600;
    val = lr_w(&shared_var);
    sc_result = sc_w(&shared_var, 601);
    check("First SC returns 0", sc_result == 0);
    sc_result = sc_w(&shared_var, 602);
    check("Second SC returns 1", sc_result == 1);
    check("Value is 601 (first SC)", shared_var == 601);
    
    // Summary
    puts("\n=== Results: ");
    print_int(passed);
    puts(" passed, ");
    print_int(failed);
    puts(" failed ===\n");
    
    puts("\n===END===\n");
    return failed;
}
