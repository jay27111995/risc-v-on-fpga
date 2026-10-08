/*
 * Zephyr-style test program
 * Tests the features Zephyr will need:
 * - Timer interrupts (mtime/mtimecmp)
 * - Atomic operations (spinlocks)
 * - Basic UART output
 */

#include "uart.h"
#include <stdint.h>

// CSR access macros
#define csr_read(csr) ({ unsigned long __v; asm volatile ("csrr %0, " #csr : "=r"(__v)); __v; })
#define csr_write(csr, val) ({ asm volatile ("csrw " #csr ", %0" :: "r"(val)); })
#define csr_set(csr, val) ({ asm volatile ("csrs " #csr ", %0" :: "r"(val)); })
#define csr_clear(csr, val) ({ asm volatile ("csrc " #csr ", %0" :: "r"(val)); })

// Timer registers
#define MTIME_LO     (*(volatile uint32_t*)0x10001000)
#define MTIME_HI     (*(volatile uint32_t*)0x10001004)
#define MTIMECMP_LO  (*(volatile uint32_t*)0x10001008)
#define MTIMECMP_HI  (*(volatile uint32_t*)0x1000100C)

// Spinlock type (like Zephyr's k_spinlock_key_t)
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

static volatile int timer_fired = 0;
static volatile int timer_count = 0;

// Trap handler
void __attribute__((interrupt("machine"))) trap_handler(void) {
    uint32_t mcause = csr_read(mcause);
    
    if (mcause == 0x80000007) {  // Machine timer interrupt
        timer_fired = 1;
        timer_count++;
        // Schedule next interrupt far in future to stop repeated firing
        MTIMECMP_LO = 0xFFFFFFFF;
        MTIMECMP_HI = 0xFFFFFFFF;
    }
}

// Spinlock acquire (like Zephyr k_spin_lock)
static inline uint32_t spin_lock(spinlock_t *lock) {
    uint32_t key = csr_read(mstatus) & 0x8;  // Save MIE bit
    csr_clear(mstatus, 0x8);  // Disable interrupts
    
    uint32_t tmp;
    do {
        asm volatile (
            "lr.w %0, (%1)\n"
            : "=r"(tmp)
            : "r"(&lock->lock)
        );
        if (tmp == 0) {
            asm volatile (
                "sc.w %0, %1, (%2)\n"
                : "=r"(tmp)
                : "r"(1), "r"(&lock->lock)
            );
        }
    } while (tmp != 0);
    
    return key;
}

// Spinlock release (like Zephyr k_spin_unlock)
static inline void spin_unlock(spinlock_t *lock, uint32_t key) {
    lock->lock = 0;
    if (key) {
        csr_set(mstatus, 0x8);  // Restore MIE if it was set
    }
}

static spinlock_t test_lock = {0};

int main(void) {
    puts("=== Zephyr Compatibility Test ===\n");
    
    // Test 1: Timer read
    puts("Test 1: Timer read... ");
    uint32_t t1 = MTIME_LO;
    for (volatile int i = 0; i < 1000; i++);
    uint32_t t2 = MTIME_LO;
    if (t2 > t1) {
        puts("PASS\n");
    } else {
        puts("FAIL\n");
    }
    
    // Test 2: Timer interrupt
    puts("Test 2: Timer interrupt... ");
    csr_write(mtvec, (uint32_t)trap_handler);
    timer_fired = 0;
    
    // Set mtimecmp to trigger soon
    uint32_t now = MTIME_LO;
    MTIMECMP_HI = 0;
    MTIMECMP_LO = now + 10000;  // ~40us at 250MHz
    
    // Enable timer interrupt
    csr_set(mie, 0x80);      // MTIE
    csr_set(mstatus, 0x8);   // MIE
    
    // Wait for interrupt
    for (volatile int i = 0; i < 100000 && !timer_fired; i++);
    
    if (timer_fired) {
        puts("PASS\n");
    } else {
        puts("FAIL\n");
    }
    
    // Disable interrupts for spinlock test
    csr_clear(mstatus, 0x8);
    
    // Test 3: Spinlock
    puts("Test 3: Spinlock acquire/release... ");
    uint32_t key = spin_lock(&test_lock);
    if (test_lock.lock == 1) {
        spin_unlock(&test_lock, key);
        if (test_lock.lock == 0) {
            puts("PASS\n");
        } else {
            puts("FAIL (unlock)\n");
        }
    } else {
        puts("FAIL (lock)\n");
    }
    
    // Test 4: Spinlock contention simulation
    puts("Test 4: Spinlock contention... ");
    test_lock.lock = 0;
    
    // Acquire lock
    key = spin_lock(&test_lock);
    
    // Try second acquire (should see lock=1 on LR, SC should fail or not execute)
    uint32_t tmp;
    asm volatile (
        "lr.w %0, (%1)\n"
        : "=r"(tmp)
        : "r"(&test_lock.lock)
    );
    
    if (tmp == 1) {  // Lock is held
        puts("PASS\n");
    } else {
        puts("FAIL\n");
    }
    
    spin_unlock(&test_lock, key);
    
    // Test 5: Multiple lock/unlock cycles
    puts("Test 5: 100 lock/unlock cycles... ");
    int pass = 1;
    for (int i = 0; i < 100; i++) {
        key = spin_lock(&test_lock);
        if (test_lock.lock != 1) pass = 0;
        spin_unlock(&test_lock, key);
        if (test_lock.lock != 0) pass = 0;
    }
    puts(pass ? "PASS\n" : "FAIL\n");
    
    puts("\n=== All Zephyr prerequisites tested ===\n");
    puts("===END===\n");
    
    while(1);
    return 0;
}
