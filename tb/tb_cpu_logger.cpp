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
// Log Entry Format (96 bits):
//   [95:64] - data (instruction, load data, or store data)
//   [63:32] - address
//   [31:16] - timestamp (lower 16 bits of cycle counter)
//   [15:2]  - reserved
//   [1:0]   - type: 00=IMEM fetch, 01=DMEM read, 10=DMEM write
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
        // Need extra tick for registered IMEM path
        tick();
    }
    
    void dmem_read(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_rdata = data;
        dut->dmem_ren = 1;
        tick();
        dut->dmem_ren = 0;
    }
    
    void dmem_write(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_wdata = data;
        dut->dmem_wen = 1;
        tick();
        dut->dmem_wen = 0;
    }
    
    void print_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();
        
        // Parse 96-bit log entry (Verilator splits into 3x32-bit array)
        uint32_t data = dut->log_entry[2];              // [95:64]
        uint32_t addr = dut->log_entry[1];              // [63:32]
        uint16_t time = (dut->log_entry[0] >> 16) & 0xFFFF;  // [31:16]
        uint8_t type = dut->log_entry[0] & 0x3;         // [1:0]
        
        const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
        printf("  [%2d] cycle=%4d %s addr=0x%08X data=0x%08X\n",
               idx, time, type_names[type], addr, data);
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
    // Test 4: DMEM priority over IMEM
    // -------------------------------------------------------------------------
    printf("\nTest 4: DMEM priority over IMEM\n");
    printf("-------------------------------\n");
    
    tb.clear_log();
    tb.dut->log_imem = 1;  // Enable IMEM logging
    
    // Simulate simultaneous DMEM write and pending IMEM fetch
    tb.dut->imem_addr = 0x00001000;
    tb.dut->imem_rdata = 0xDEADBEEF;
    tb.dut->imem_valid = 1;
    tb.tick();  // IMEM data gets registered
    
    // Now do a DMEM write - should take priority
    tb.dut->imem_valid = 0;
    tb.dut->dmem_addr = 0x00002000;
    tb.dut->dmem_wdata = 0x12345678;
    tb.dut->dmem_wen = 1;
    tb.tick();
    tb.dut->dmem_wen = 0;
    
    // Let IMEM log
    tb.tick();
    tb.tick();
    
    printf("Log count: %d\n", tb.dut->log_count);
    printf("\nPriority Log:\n");
    for (uint32_t i = 0; i < tb.dut->log_count && i < 10; i++) {
        tb.print_log_entry(i);
    }
    
    // First entry should be DSTORE (DMEM has priority)
    tb.dut->log_idx = tb.dut->log_count - 1;  // Oldest entry
    tb.dut->eval();
    uint8_t first_type = tb.dut->log_entry[0] & 0x3;
    if (first_type == TYPE_IFETCH) {
        printf("PASS: IMEM logged first (before DMEM arrived)\n");
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
