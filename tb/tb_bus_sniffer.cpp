// ============================================================================
// Bus Sniffer Testbench
// ============================================================================
//
// Tests the bus_sniffer debug module which captures host AXI transactions:
// - Write transactions (address + data)
// - Read transactions (address + returned data)
//
// The sniffer maintains a circular buffer of recent host transactions for
// debugging PCIe BAR accesses to the SoC.
//
// Log Entry Format (128 bits):
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits)
//   [31:20]  - reserved  (12 bits)
//   [19:1]   - address   (19 bits)
//   [0]      - type      (0=read, 1=write)
//
// ============================================================================

#include <cstdio>
#include "Vbus_sniffer.h"
#include "verilated.h"

class Testbench {
public:
    Vbus_sniffer* dut;
    uint64_t cycle;
    uint32_t mem[64];
    
    Testbench() {
        dut = new Vbus_sniffer;
        cycle = 0;
        
        dut->clk = 0;
        dut->rst_n = 0;
        dut->log_enable = 0;
        dut->log_clear = 0;
        dut->in_addr = 0;
        dut->in_wdata = 0;
        dut->in_wen = 0;
        dut->in_ren = 0;
        dut->in_rdata = 0;
        dut->log_idx = 0;
        
        for (int i = 0; i < 64; i++) mem[i] = 0;
        
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
        // Simulate downstream memory
        if (dut->out_wen) {
            uint32_t idx = (dut->out_addr >> 2) & 0x3F;
            mem[idx] = dut->out_wdata;
        }
        if (dut->out_ren) {
            uint32_t idx = (dut->out_addr >> 2) & 0x3F;
            dut->in_rdata = mem[idx];
        }
        
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        cycle++;
    }
    
    void write(uint32_t addr, uint32_t data) {
        dut->in_addr = addr;
        dut->in_wdata = data;
        dut->in_wen = 1;
        tick();
        dut->in_wen = 0;
        tick();
    }
    
    uint32_t read(uint32_t addr) {
        dut->in_addr = addr;
        dut->in_ren = 1;
        tick();
        dut->in_ren = 0;
        tick();
        return dut->out_rdata;
    }
    
    void print_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();
        
        // Parse 128-bit log entry (Verilator splits into 4x32-bit array)
        // [127:96] = w3 = data
        // [95:64]  = w2 = timestamp[63:32]
        // [63:32]  = w1 = timestamp[31:0]
        // [31:0]   = w0 = reserved[31:20], address[19:1], type[0]
        uint32_t w0 = dut->log_entry[0];
        uint32_t w1 = dut->log_entry[1];
        uint32_t w2 = dut->log_entry[2];
        uint32_t w3 = dut->log_entry[3];
        
        bool is_write = w0 & 1;
        uint32_t addr = (w0 >> 1) & 0x7FFFF;
        uint64_t timestamp = ((uint64_t)w2 << 32) | w1;
        uint32_t data = w3;
        
        printf("  [%d] cycle=%lu %s addr=0x%05X data=0x%08X\n",
               idx, timestamp, is_write ? "WR" : "RD", addr, data);
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
    
    printf("bus_sniffer Testbench\n");
    printf("=====================\n\n");
    
    // Clear log first
    tb.clear_log();
    
    // Do some transactions
    printf("Performing transactions...\n");
    tb.write(0x0000, 0xAAAAAAAA);
    tb.write(0x0004, 0xBBBBBBBB);
    tb.read(0x0000);
    tb.write(0x0008, 0xCCCCCCCC);
    tb.read(0x0004);
    tb.read(0x0008);
    
    // Extra tick for pipeline flush
    tb.tick();
    
    printf("\nLog count: %d (expected 6)\n", tb.dut->log_count);
    printf("Current cycle: %d\n\n", tb.dut->log_cycle);
    
    if (tb.dut->log_count != 6) {
        printf("ERROR: Expected 6 logged transactions!\n");
        errors++;
    }
    
    printf("Transaction log (newest first):\n");
    for (int i = 0; i < 6; i++) {
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
