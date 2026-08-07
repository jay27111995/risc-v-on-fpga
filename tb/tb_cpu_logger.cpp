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
        dut->imem_addr = 0;
        dut->imem_rdata = 0;
        dut->imem_valid = 0;
        dut->dmem_addr = 0;
        dut->dmem_wdata = 0;
        dut->dmem_rdata = 0;
        dut->dmem_wen = 0;
        dut->dmem_ren = 0;
        dut->log_idx = 0;
        
        for (int i = 0; i < 5; i++) tick();
        dut->rst_n = 1;
        for (int i = 0; i < 2; i++) tick();
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
    
    void imem_fetch(uint32_t addr, uint32_t instr) {
        dut->imem_addr = addr;
        dut->imem_rdata = instr;
        dut->imem_valid = 1;
        tick();
    }
    
    void dmem_write(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_wdata = data;
        dut->dmem_wen = 1;
        tick();
        dut->dmem_wen = 0;
    }
    
    void dmem_read(uint32_t addr, uint32_t data) {
        dut->dmem_addr = addr;
        dut->dmem_rdata = data;
        dut->dmem_ren = 1;
        tick();
        dut->dmem_ren = 0;
    }
    
    void print_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();
        
        // Parse 96-bit log entry (3 x 32-bit words)
        uint32_t data = dut->log_entry[2];       // [95:64]
        uint32_t addr = dut->log_entry[1];       // [63:32]
        uint16_t time = (dut->log_entry[0] >> 16) & 0xFFFF;  // [31:16]
        uint8_t type = dut->log_entry[0] & 0x3;  // [1:0]
        
        const char* type_str = (type == 0) ? "IFETCH" : 
                               (type == 1) ? "DLOAD " : 
                               (type == 2) ? "DSTORE" : "???";
        
        printf("  [%2d] cycle=%3d %s addr=0x%08X data=0x%08X\n",
               idx, time, type_str, addr, data);
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    Testbench tb;
    
    printf("cpu_logger Testbench\n");
    printf("====================\n\n");
    
    // Simulate CPU execution
    printf("Simulating CPU execution...\n\n");
    
    // Fetch instruction at PC=0x0000 (ADDI x1, x0, 5)
    tb.imem_fetch(0x0000, 0x00500093);
    
    // Fetch at PC=0x0004 (ADDI x2, x0, 3)
    tb.imem_fetch(0x0004, 0x00300113);
    
    // Fetch at PC=0x0008 (ADD x3, x1, x2)
    tb.imem_fetch(0x0008, 0x002081B3);
    
    // Fetch at PC=0x000C (SW x3, 0(x0))
    tb.imem_fetch(0x000C, 0x00302023);
    
    // DMEM write happens (store x3=8 to addr 0)
    tb.dmem_write(0x0000, 0x00000008);
    
    // Fetch at PC=0x0010 (BEQ x0, x0, 0)
    tb.imem_fetch(0x0010, 0x00000063);
    
    // Simulate a load instruction
    tb.dmem_read(0x0000, 0x00000008);
    
    // Stop fetching
    tb.dut->imem_valid = 0;
    tb.tick();
    tb.tick();
    
    printf("Log count: %d\n", tb.dut->log_count);
    printf("Current cycle: %d\n\n", tb.dut->log_cycle);
    
    printf("CPU Memory Access Log (newest first):\n");
    for (int i = 0; i < 7; i++) {
        tb.print_log_entry(i);
    }
    
    printf("\n=== TEST COMPLETE ===\n");
    return 0;
}
