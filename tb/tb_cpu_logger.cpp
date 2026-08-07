// ============================================================================
// CPU Logger Testbench
// ============================================================================
//
// Tests the cpu_logger debug module which captures CPU memory accesses:
// - Instruction fetches (IMEM reads)
// - Data loads (DMEM reads)
// - Data stores (DMEM writes)
//
// The logger maintains a circular buffer of recent transactions for debugging.
// Each entry contains: timestamp, address, data, and transaction type.
//
// Log Entry Format (96 bits):
//   [95:64] - data (instruction, load data, or store data)
//   [63:32] - address
//   [31:16] - timestamp (lower 16 bits of cycle counter)
//   [15:2]  - reserved
//   [1:0]   - type: 00=IMEM fetch, 01=DMEM read, 10=DMEM write
//
// Note: The NOP filter (imem_rdata != 0x00000013) creates a timing path from
// RAM output through comparison logic. This causes a minor timing violation
// at worst-case corners but doesn't affect functional operation.
//
// ============================================================================

#include <cstdio>
#include "Vcpu_logger.h"
#include "verilated.h"

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
    
    // Clear log first
    tb.clear_log();
    
    // Simulate CPU execution with some memory accesses
    printf("Simulating CPU execution...\n\n");
    
    // Instruction fetches (NOP = 0x00000013 will be filtered out)
    tb.imem_fetch(0x00000000, 0x00500093);  // ADDI x1, x0, 5
    tb.imem_fetch(0x00000004, 0x00300113);  // ADDI x2, x0, 3
    tb.imem_fetch(0x00000008, 0x002081B3);  // ADD x3, x1, x2
    tb.imem_fetch(0x0000000C, 0x00302023);  // SW x3, 0(x0)
    
    // Data store
    tb.dmem_write(0x00000000, 8);           // Store 8 to DMEM[0]
    
    // More instruction fetches
    tb.imem_fetch(0x00000010, 0x00002283);  // LW x5, 0(x0)
    
    // Data load
    tb.dmem_read(0x00000000, 8);            // Load 8 from DMEM[0]
    
    // NOP should be filtered
    tb.imem_fetch(0x00000014, 0x00000013);  // NOP (should NOT be logged)
    
    // Another instruction
    tb.imem_fetch(0x00000018, 0x00000063);  // BEQ x0, x0, 0
    
    printf("Log count: %d (expected 8 - NOP filtered)\n", tb.dut->log_count);
    printf("Current cycle: %d\n\n", tb.dut->log_cycle);
    
    // Should have logged 8 entries (NOP filtered out)
    if (tb.dut->log_count != 8) {
        printf("ERROR: Expected 8 logged transactions (NOP filtered)!\n");
        errors++;
    }
    
    printf("CPU Memory Access Log (newest first):\n");
    int entries_to_show = (tb.dut->log_count < 10) ? tb.dut->log_count : 10;
    for (int i = 0; i < entries_to_show; i++) {
        tb.print_log_entry(i);
    }
    
    // Test clear
    printf("\nClearing log...\n");
    tb.clear_log();
    printf("Log count after clear: %d (expected 0)\n", tb.dut->log_count);
    
    if (tb.dut->log_count != 0) {
        printf("ERROR: Log count should be 0 after clear!\n");
        errors++;
    }
    
    printf("\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    
    return errors;
}
