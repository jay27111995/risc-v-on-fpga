// ============================================================================
// PLIC Stub Testbench
// ============================================================================
//
// Tests the minimal PLIC implementation:
//   - Address decoding (hit signal)
//   - Read returns 0 for all registers
//   - Writes are accepted (no errors)
//
// PLIC Memory Map (relative to BASE_ADDR = 0x0C000000):
//   0x000000 - 0x000FFF: Priority registers
//   0x001000 - 0x00107F: Pending bits
//   0x002000 - 0x1F1FFF: Enable bits
//   0x200000: Priority threshold (context 0)
//   0x200004: Claim/complete (context 0)
//
// ============================================================================

#include "Vplic_stub.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

#define BASE_ADDR 0x0C000000

class PlicTestbench {
public:
    Vplic_stub* dut;
    uint64_t sim_time;

    PlicTestbench() {
        dut = new Vplic_stub;
        sim_time = 0;

        // Initialize inputs
        dut->clk = 0;
        dut->rst_n = 0;
        dut->addr = 0;
        dut->wdata = 0;
        dut->we = 0;
        dut->re = 0;

        // Reset sequence
        tick();
        tick();
        dut->rst_n = 1;
        tick();
    }

    ~PlicTestbench() {
        delete dut;
    }

    void tick() {
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        sim_time++;
    }

    // Write to PLIC address
    void write(uint32_t addr, uint32_t data) {
        dut->addr = addr;
        dut->wdata = data;
        dut->we = 1;
        dut->re = 0;
        tick();
        dut->we = 0;
    }

    // Read from PLIC address (returns data after 1 cycle)
    uint32_t read(uint32_t addr) {
        dut->addr = addr;
        dut->we = 0;
        dut->re = 1;
        tick();
        dut->re = 0;
        return dut->rdata;
    }

    bool get_hit() {
        return dut->hit;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    PlicTestbench tb;
    int errors = 0;

    printf("========================================\n");
    printf("   PLIC Stub Testbench\n");
    printf("========================================\n\n");

    // ========================================================================
    // Test 1: Address hit detection
    // ========================================================================
    printf("=== Test 1: Address hit detection ===\n");

    // PLIC range: 0x0C000000 - 0x0FFFFFFF (64MB based on addr[31:22])
    // addr[31:22] = 0x30 (48 decimal) for 0x0C000000
    // This covers 0x0C000000 - 0x0FFFFFFF
    struct {
        uint32_t addr;
        bool expect_hit;
        const char* desc;
    } hit_tests[] = {
        {0x0C000000, true,  "PLIC base (priority[0])"},
        {0x0C000004, true,  "PLIC priority[1]"},
        {0x0C001000, true,  "PLIC pending[0]"},
        {0x0C002000, true,  "PLIC enable[0]"},
        {0x0C200000, true,  "PLIC threshold (ctx 0)"},
        {0x0C200004, true,  "PLIC claim (ctx 0)"},
        {0x0C3FFFFF, true,  "PLIC upper addr (within 4MB range)"},
        {0x0BFFFFFF, false, "Below PLIC range"},
        {0x10000000, false, "Above PLIC range"},
        {0x00000000, false, "Address 0"},
        {0x10001000, false, "Timer address"},
    };

    for (auto& test : hit_tests) {
        tb.dut->addr = test.addr;
        tb.dut->re = 1;
        tb.dut->we = 0;
        tb.tick();
        bool hit = tb.get_hit();
        tb.dut->re = 0;

        if (hit != test.expect_hit) {
            printf("  FAIL: %s (0x%08X) - expected hit=%d, got %d\n",
                   test.desc, test.addr, test.expect_hit, hit);
            errors++;
        } else {
            printf("  PASS: %s (0x%08X) hit=%d\n",
                   test.desc, test.addr, hit);
        }
    }

    // ========================================================================
    // Test 2: All reads return 0
    // ========================================================================
    printf("\n=== Test 2: Read returns 0 ===\n");

    uint32_t read_addrs[] = {
        BASE_ADDR + 0x000000,  // Priority[0]
        BASE_ADDR + 0x000004,  // Priority[1]
        BASE_ADDR + 0x001000,  // Pending[0]
        BASE_ADDR + 0x002000,  // Enable[0]
        BASE_ADDR + 0x200000,  // Threshold
        BASE_ADDR + 0x200004,  // Claim
    };

    for (uint32_t addr : read_addrs) {
        uint32_t data = tb.read(addr);
        if (data != 0) {
            printf("  FAIL: Read 0x%08X returned 0x%08X, expected 0\n",
                   addr, data);
            errors++;
        } else {
            printf("  PASS: Read 0x%08X = 0x%08X\n", addr, data);
        }
    }

    // ========================================================================
    // Test 3: Writes are accepted (no crash)
    // ========================================================================
    printf("\n=== Test 3: Writes accepted ===\n");

    struct {
        uint32_t addr;
        uint32_t data;
        const char* desc;
    } write_tests[] = {
        {BASE_ADDR + 0x000004, 0x00000007, "Set priority[1] = 7"},
        {BASE_ADDR + 0x002000, 0xFFFFFFFF, "Enable all IRQs"},
        {BASE_ADDR + 0x200000, 0x00000000, "Set threshold = 0"},
        {BASE_ADDR + 0x200004, 0x00000001, "Complete IRQ 1"},
    };

    for (auto& test : write_tests) {
        tb.write(test.addr, test.data);
        printf("  PASS: %s\n", test.desc);
    }

    // Verify reads still return 0 after writes (stub ignores writes)
    printf("\n=== Test 4: Reads still return 0 after writes ===\n");
    for (auto& test : write_tests) {
        uint32_t data = tb.read(test.addr);
        if (data != 0) {
            printf("  FAIL: Read 0x%08X returned 0x%08X after write\n",
                   test.addr, data);
            errors++;
        } else {
            printf("  PASS: Read 0x%08X = 0 (write ignored as expected)\n",
                   test.addr);
        }
    }

    // ========================================================================
    // Summary
    // ========================================================================
    printf("\n========================================\n");
    if (errors == 0) {
        printf("   ALL TESTS PASSED!\n");
    } else {
        printf("   %d ERRORS\n", errors);
    }
    printf("========================================\n");

    return errors;
}
