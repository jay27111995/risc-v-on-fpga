// ============================================================================
// CPU Logger Testbench
// ============================================================================
//
// Tests the cpu_logger debug module which captures CPU memory accesses:
// - Instruction fetches (IMEM reads) - optional, controlled by log_imem
// - Data loads (DMEM reads)
// - Data stores (DMEM writes)
//
// Logging stops naturally when the CPU halts (e.g., on EBREAK) since no more
// memory transactions occur.
//
// Log Entry Format (128 bits):
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits)
//   [31:20]  - reserved  (12 bits)
//   [19:2]   - address   (18 bits - word-aligned)
//   [1:0]    - type      (00=IFETCH, 01=DLOAD, 10=DSTORE)
//
// Control Register (at 0x5008):
//   [0] = log_enable (default 1)
//   [1] = log_clear (write 1 to clear, auto-clears)
//   [2] = log_imem (default 0 = DMEM only)
//
// ============================================================================

#include <cstdio>
#include "Vcpu_logger.h"
#include "verilated.h"

// Log entry types
#define TYPE_IFETCH  0
#define TYPE_DLOAD   1
#define TYPE_DSTORE  2

class Testbench {
public:
    Vcpu_logger* dut;
    uint64_t cycle;

    Testbench() {
        dut = new Vcpu_logger;
        cycle = 0;

        dut->clk = 0;
        dut->rst_n = 0;
        dut->log_enable = 0;
        dut->log_clear = 0;
        dut->log_imem = 0;  // DMEM-only by default
        dut->imem_addr = 0;
        dut->imem_rdata = 0;
        dut->imem_valid = 0;
        dut->dmem_addr = 0;
        dut->dmem_wdata = 0;
        dut->dmem_rdata = 0;
        dut->dmem_wen = 0;
        dut->dmem_ren = 0;
        dut->log_idx = 0;

        // Reset sequence
        for (int i = 0; i < 5; i++) tick();
        dut->rst_n = 1;
        for (int i = 0; i < 2; i++) tick();

        // Enable logging
        dut->log_enable = 1;
    }

    ~Testbench() {
        dut->final();
        delete dut;
    }

    void tick() {
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        cycle++;
    }

    void imem_fetch(uint32_t addr, uint32_t data) {
        dut->imem_addr = addr;
        dut->imem_rdata = data;
        dut->imem_valid = 1;
        tick();
        dut->imem_valid = 0;
        // Extra tick for pipeline to memory
        tick();
    }

    void dmem_read(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_rdata = data;
        dut->dmem_ren = 1;
        tick();
        dut->dmem_ren = 0;
        // Extra tick for pipeline to memory
        tick();
    }

    void dmem_write(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_wdata = data;
        dut->dmem_wen = 1;
        tick();
        dut->dmem_wen = 0;
        // Extra tick for pipeline to memory
        tick();
    }

    // Get parsed log entry
    struct LogEntry {
        uint8_t type;
        uint32_t addr;
        uint64_t timestamp;
        uint32_t data;
    };

    LogEntry get_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();

        LogEntry e;
        uint32_t w0 = dut->log_entry[0];
        uint32_t w1 = dut->log_entry[1];
        uint32_t w2 = dut->log_entry[2];
        uint32_t w3 = dut->log_entry[3];

        e.type = w0 & 0x3;
        e.addr = ((w0 >> 2) & 0x3FFFF) << 2;
        e.timestamp = ((uint64_t)w2 << 32) | w1;
        e.data = w3;
        return e;
    }

    void print_log_entry(int idx) {
        LogEntry e = get_log_entry(idx);
        const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
        printf("  [%2d] cycle=%lu %s addr=0x%05X data=0x%08X\n",
               idx, e.timestamp, type_names[e.type], e.addr, e.data);
    }

    void clear_log() {
        dut->log_clear = 1;
        tick();
        dut->log_clear = 0;
        tick();
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Testbench tb;
    int errors = 0;

    printf("cpu_logger Testbench\n");
    printf("====================\n\n");

    // -------------------------------------------------------------------------
    // Test 1: DMEM-only mode (default, log_imem=0)
    // -------------------------------------------------------------------------
    printf("Test 1: DMEM-only mode (log_imem=0)\n");
    printf("-----------------------------------\n");

    tb.clear_log();
    tb.dut->log_imem = 0;  // DMEM only

    // Instruction fetches - should NOT be logged
    tb.imem_fetch(0x00000000, 0x00500093);  // ADDI x1, x0, 5
    tb.imem_fetch(0x00000004, 0x00300113);  // ADDI x2, x0, 3

    // Data store - should be logged
    tb.dmem_write(0x00000000, 8);           // Store 8 to DMEM[0]

    // More instruction fetches - should NOT be logged
    tb.imem_fetch(0x00000008, 0x002081B3);  // ADD x3, x1, x2

    // Data load - should be logged
    tb.dmem_read(0x00000000, 8);            // Load 8 from DMEM[0]

    printf("Log count: %d (expected 2 - DMEM only)\n", tb.dut->log_count);

    if (tb.dut->log_count != 2) {
        printf("ERROR: Expected 2 logged transactions (DMEM only), got %d!\n", tb.dut->log_count);
        errors++;
    } else {
        printf("PASS: Only DMEM accesses logged\n");
    }

    printf("\nDMEM-only Log:\n");
    for (uint32_t i = 0; i < tb.dut->log_count && i < 10; i++) {
        tb.print_log_entry(i);
    }

    // -------------------------------------------------------------------------
    // Test 2: Full logging mode (log_imem=1)
    // -------------------------------------------------------------------------
    printf("\nTest 2: Full logging mode (log_imem=1)\n");
    printf("--------------------------------------\n");

    tb.clear_log();
    tb.dut->log_imem = 1;  // Enable IMEM logging

    // Instruction fetches - should be logged
    tb.imem_fetch(0x00000000, 0x00500093);  // ADDI x1, x0, 5
    tb.imem_fetch(0x00000004, 0x00300113);  // ADDI x2, x0, 3

    // Data store - should be logged
    tb.dmem_write(0x00000000, 8);           // Store 8 to DMEM[0]

    // Another instruction
    tb.imem_fetch(0x00000008, 0x002081B3);  // ADD x3, x1, x2

    // Data load - should be logged
    tb.dmem_read(0x00000000, 8);            // Load 8 from DMEM[0]

    printf("Log count: %d (expected 5)\n", tb.dut->log_count);

    if (tb.dut->log_count != 5) {
        printf("ERROR: Expected 5 logged transactions, got %d!\n", tb.dut->log_count);
        errors++;
    } else {
        printf("PASS: All transactions logged\n");
    }

    printf("\nFull Log:\n");
    for (uint32_t i = 0; i < tb.dut->log_count && i < 10; i++) {
        tb.print_log_entry(i);
    }

    // Verify correct instruction data captured
    Testbench::LogEntry e4 = tb.get_log_entry(4);  // Oldest IFETCH
    Testbench::LogEntry e3 = tb.get_log_entry(3);  // Second IFETCH
    Testbench::LogEntry e1 = tb.get_log_entry(1);  // Third IFETCH

    if (e4.type != TYPE_IFETCH || e4.data != 0x00500093) {
        printf("ERROR: Entry 4 should be IFETCH with data 0x00500093, got type=%d data=0x%08X\n",
               e4.type, e4.data);
        errors++;
    }
    if (e3.type != TYPE_IFETCH || e3.data != 0x00300113) {
        printf("ERROR: Entry 3 should be IFETCH with data 0x00300113, got type=%d data=0x%08X\n",
               e3.type, e3.data);
        errors++;
    }
    if (e1.type != TYPE_IFETCH || e1.data != 0x002081B3) {
        printf("ERROR: Entry 1 should be IFETCH with data 0x002081B3, got type=%d data=0x%08X\n",
               e1.type, e1.data);
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 3: Clear functionality
    // -------------------------------------------------------------------------
    printf("\nTest 3: Clear functionality\n");
    printf("---------------------------\n");

    printf("Log count before clear: %d\n", tb.dut->log_count);
    tb.clear_log();
    printf("Log count after clear: %d (expected 0)\n", tb.dut->log_count);

    if (tb.dut->log_count != 0) {
        printf("ERROR: Log count should be 0 after clear!\n");
        errors++;
    } else {
        printf("PASS: Log cleared successfully\n");
    }

    // Verify logging works after clear
    tb.dmem_write(0x00000000, 0xDEADBEEF);

    if (tb.dut->log_count != 1) {
        printf("ERROR: Logging should work after clear!\n");
        errors++;
    } else {
        printf("PASS: Logging resumed after clear\n");
    }

    // -------------------------------------------------------------------------
    // Test 4: Verify 64-bit timestamp
    // -------------------------------------------------------------------------
    printf("\nTest 4: 64-bit timestamp\n");
    printf("------------------------\n");

    tb.clear_log();

    // Do a transaction
    tb.dmem_write(0x00001000, 0x12345678);

    // Check the timestamp is reasonable
    Testbench::LogEntry e = tb.get_log_entry(0);
    uint32_t current_cycle = tb.dut->log_cycle;

    printf("Timestamp in log: %lu\n", e.timestamp);
    printf("Current cycle:    %u\n", current_cycle);

    // Timestamp should be within a few cycles of current
    if (e.timestamp > 0 && e.timestamp <= current_cycle) {
        printf("PASS: 64-bit timestamp working\n");
    } else {
        printf("ERROR: Timestamp looks wrong!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 5: Verify 18-bit address
    // -------------------------------------------------------------------------
    printf("\nTest 5: 18-bit address\n");
    printf("----------------------\n");

    tb.clear_log();

    // Write to a high address (use upper bits of 18-bit range)
    uint32_t test_addr = 0x0003FFF0;  // Near top of 256KB range
    tb.dmem_write(test_addr, 0xCAFEBABE);

    e = tb.get_log_entry(0);

    printf("Test address:   0x%05X\n", test_addr);
    printf("Logged address: 0x%05X\n", e.addr);

    if (e.addr == test_addr) {
        printf("PASS: 18-bit address working\n");
    } else {
        printf("ERROR: Address mismatch!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 6: Log enable/disable
    // -------------------------------------------------------------------------
    printf("\nTest 6: Log enable/disable\n");
    printf("--------------------------\n");

    tb.clear_log();

    // Log some entries while enabled
    tb.dmem_write(0x0000, 0x11111111);
    tb.dmem_write(0x0004, 0x22222222);

    uint32_t count_before = tb.dut->log_count;
    printf("Count with logging enabled: %d\n", count_before);

    // Disable logging
    tb.dut->log_enable = 0;

    // These should NOT be logged
    tb.dmem_write(0x0008, 0x33333333);
    tb.dmem_write(0x000C, 0x44444444);

    uint32_t count_disabled = tb.dut->log_count;
    printf("Count after disabled writes: %d (should still be %d)\n", count_disabled, count_before);

    // Re-enable logging
    tb.dut->log_enable = 1;

    // This should be logged
    tb.dmem_write(0x0010, 0x55555555);

    uint32_t count_after = tb.dut->log_count;
    printf("Count after re-enable: %d (should be %d)\n", count_after, count_before + 1);

    if (count_disabled != count_before) {
        printf("ERROR: Logging occurred while disabled!\n");
        errors++;
    } else if (count_after != count_before + 1) {
        printf("ERROR: Logging didn't resume after re-enable!\n");
        errors++;
    } else {
        printf("PASS: Log enable/disable working\n");
    }

    // -------------------------------------------------------------------------
    // Test 7: Simultaneous DMEM read and write (priority test)
    // -------------------------------------------------------------------------
    printf("\nTest 7: DMEM priority (store > load)\n");
    printf("------------------------------------\n");

    tb.clear_log();

    // Assert both wen and ren simultaneously - store should win
    tb.dut->dmem_addr = 0x100;
    tb.dut->dmem_wdata = 0xAAAAAAAA;
    tb.dut->dmem_rdata = 0xBBBBBBBB;
    tb.dut->dmem_wen = 1;
    tb.dut->dmem_ren = 1;
    tb.tick();
    tb.dut->dmem_wen = 0;
    tb.dut->dmem_ren = 0;
    tb.tick();

    if (tb.dut->log_count != 1) {
        printf("ERROR: Expected 1 entry for simultaneous wen/ren\n");
        errors++;
    } else {
        e = tb.get_log_entry(0);
        if (e.type == TYPE_DSTORE && e.data == 0xAAAAAAAA) {
            printf("PASS: Store takes priority over load\n");
        } else {
            printf("ERROR: Expected DSTORE with 0xAAAAAAAA, got type=%d data=0x%08X\n", e.type, e.data);
            errors++;
        }
    }

    // -------------------------------------------------------------------------
    // Test 8: Log wrap-around (circular buffer)
    // -------------------------------------------------------------------------
    printf("\nTest 8: Circular buffer (write 260 entries, depth=256)\n");
    printf("------------------------------------------------------\n");

    tb.clear_log();

    // Write more than LOG_DEPTH entries
    for (int i = 0; i < 260; i++) {
        tb.dmem_write(i * 4, i);
    }

    printf("Log count: %d (trans_count keeps incrementing)\n", tb.dut->log_count);

    // Entry 0 should be the newest (259), not entry 256
    e = tb.get_log_entry(0);
    printf("Newest entry (idx=0): data=0x%08X (expected 259=0x103)\n", e.data);

    // Entry 3 should be 256
    e = tb.get_log_entry(3);
    printf("Entry idx=3: data=0x%08X (expected 256=0x100)\n", e.data);

    // Entry 255 should wrap to entry 4 (260-256=4)
    e = tb.get_log_entry(255);
    printf("Entry idx=255: data=0x%08X (expected 4=0x004)\n", e.data);

    if (tb.get_log_entry(0).data == 259 &&
        tb.get_log_entry(3).data == 256 &&
        tb.get_log_entry(255).data == 4) {
        printf("PASS: Circular buffer working correctly\n");
    } else {
        printf("ERROR: Circular buffer wrap issue\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 9: IMEM and DMEM interleaved
    // -------------------------------------------------------------------------
    printf("\nTest 9: IMEM and DMEM interleaved\n");
    printf("---------------------------------\n");

    tb.clear_log();
    tb.dut->log_imem = 1;

    // Simulate a typical CPU sequence
    tb.imem_fetch(0x00, 0x00500093);  // ADDI x1, x0, 5
    tb.imem_fetch(0x04, 0x00302023);  // SW x3, 0(x0)
    tb.dmem_write(0x00, 5);           // Store from SW
    tb.imem_fetch(0x08, 0x00002183);  // LW x3, 0(x0)
    tb.dmem_read(0x00, 5);            // Load from LW
    tb.imem_fetch(0x0C, 0x00100073);  // EBREAK

    printf("Log count: %d (expected 6: 4 IFETCH + 1 DSTORE + 1 DLOAD)\n", tb.dut->log_count);

    if (tb.dut->log_count != 6) {
        printf("ERROR: Expected 6 entries\n");
        errors++;
    } else {
        printf("PASS: Interleaved IMEM/DMEM logging works\n");
    }

    printf("\nInterleaved log:\n");
    for (uint32_t i = 0; i < tb.dut->log_count; i++) {
        tb.print_log_entry(i);
    }

    // -------------------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------------------
    printf("\n");
    printf("========================================\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    printf("========================================\n");

    return errors;
}
