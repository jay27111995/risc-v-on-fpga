// atomic_test.c - Test LR.W/SC.W atomic instructions (A extension)

#include "stdio.h"

// Inline assembly for LR.W and SC.W
// LR.W rd, (rs1) - Load-Reserved Word
// SC.W rd, rs2, (rs1) - Store-Conditional Word (returns 0 on success, 1 on fail)

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

int main(void) {
    puts("Atomic test (LR.W/SC.W)\n");
    
    // Print address of shared_var
    puts("shared_var addr: ");
    print_int((int)&shared_var);
    puts("\n");
    
    // First, verify basic LW works
    puts("Verifying LW...\n");
    int lw_val = shared_var;  // Regular load
    puts("LW result: ");
    print_int(lw_val);
    puts(" (should be 100)\n");
    
    // Now try LR.W
    puts("Trying LR.W...\n");
    
    // Store the address in a variable to ensure s0 is set correctly
    volatile int *addr = &shared_var;
    puts("addr ptr: ");
    print_int((int)addr);
    puts("\n");
    
    int val = lr_w(addr);
    puts("LR.W result: ");
    print_int(val);
    puts("\n");
    
    int sc_result = sc_w(&shared_var, val + 1);
    if (sc_result == 0) {
        puts("SC succeeded (0)\n");
    } else {
        puts("SC FAILED unexpectedly\n");
    }
    
    // Verify the store happened
    int new_val = shared_var;
    if (new_val == 101) {
        puts("Value updated to 101: PASS\n");
    } else {
        puts("Value wrong: ");
        print_int(new_val);
        puts(" FAIL\n");
    }
    
    // Test 2: SC without LR (should fail since reservation cleared)
    puts("\nTest 2: SC without prior LR...\n");
    sc_result = sc_w(&shared_var, 999);
    if (sc_result == 1) {
        puts("SC failed as expected (1): PASS\n");
    } else {
        puts("SC unexpectedly succeeded: FAIL\n");
    }
    
    // Verify store did NOT happen
    if (shared_var == 101) {
        puts("Value unchanged (101): PASS\n");
    } else {
        puts("Value changed unexpectedly: FAIL\n");
    }
    
    // Test 3: LR/SC to different address (should fail)
    puts("\nTest 3: LR addr1, SC addr2...\n");
    val = lr_w(&shared_var);  // Reserve shared_var
    sc_result = sc_w(&lock, 1);  // Try SC to different address
    if (sc_result == 1) {
        puts("SC to different addr failed (1): PASS\n");
    } else {
        puts("SC to different addr succeeded: FAIL\n");
    }
    
    // Test 4: LR/SC with intervening store (should fail)
    puts("\nTest 4: LR, store, SC...\n");
    val = lr_w(&shared_var);
    shared_var = 200;  // Intervening store invalidates reservation
    sc_result = sc_w(&shared_var, 300);
    if (sc_result == 1) {
        puts("SC after store failed (1): PASS\n");
    } else {
        puts("SC after store succeeded: FAIL\n");
    }
    
    // Test 5: Spinlock acquire/release pattern
    puts("\nTest 5: Spinlock pattern...\n");
    lock = 0;  // Initialize lock
    
    // Try to acquire lock using LR/SC
    int acquired = 0;
    for (int i = 0; i < 5; i++) {
        val = lr_w(&lock);
        if (val == 0) {
            // Lock is free, try to acquire
            sc_result = sc_w(&lock, 1);
            if (sc_result == 0) {
                acquired = 1;
                puts("Lock acquired!\n");
                break;
            }
        }
    }
    
    if (acquired) {
        puts("Spinlock acquire: PASS\n");
        // Release lock
        lock = 0;
        puts("Lock released\n");
    } else {
        puts("Spinlock acquire: FAIL\n");
    }
    
    puts("\n===END===\n");
    return 0;
}
