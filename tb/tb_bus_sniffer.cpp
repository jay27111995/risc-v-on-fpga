// Testbench for bus_sniffer
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
        dut->in_addr = 0;
        dut->in_wdata = 0;
        dut->in_wen = 0;
        dut->in_ren = 0;
        dut->in_rdata = 0;
        dut->log_idx = 0;
        
        for (int i = 0; i < 64; i++) mem[i] = 0;
        
        for (int i = 0; i < 5; i++) tick();
        dut->rst_n = 1;
        for (int i = 0; i < 2; i++) tick();
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
    
    void write(uint16_t addr, uint32_t data) {
        dut->in_addr = addr;
        dut->in_wdata = data;
        dut->in_wen = 1;
        tick();
        dut->in_wen = 0;
        tick();
    }
    
    uint32_t read(uint16_t addr) {
        dut->in_addr = addr;
        dut->in_ren = 1;
        tick();
        dut->in_ren = 0;
        tick();  // Read data available now
        return dut->out_rdata;
    }
    
    void print_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();
        
        // Parse 128-bit log entry
        // Verilator splits wide signals - need to reconstruct
        uint32_t data = (dut->log_entry[3]);  // [127:96]
        uint16_t addr = (dut->log_entry[1] >> 16) & 0xFFFF;  // [63:48]
        uint16_t time = dut->log_entry[1] & 0xFFFF;  // [47:32]
        bool is_write = dut->log_entry[0] & 1;  // [0]
        
        printf("  [%d] cycle=%3d addr=0x%04X %s data=0x%08X\n",
               idx, time, addr, is_write ? "WR" : "RD", data);
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    Testbench tb;
    
    printf("bus_sniffer Testbench\n");
    printf("=====================\n\n");
    
    // Do some transactions
    printf("Performing transactions...\n");
    tb.write(0x0000, 0xAAAAAAAA);
    tb.write(0x0004, 0xBBBBBBBB);
    tb.read(0x0000);
    tb.write(0x0008, 0xCCCCCCCC);
    tb.read(0x0004);
    tb.read(0x0008);
    
    printf("\nLog count: %d\n", tb.dut->log_count);
    printf("Current cycle: %d\n\n", tb.dut->log_cycle);
    
    printf("Transaction log (newest first):\n");
    for (int i = 0; i < 6; i++) {
        tb.print_log_entry(i);
    }
    
    printf("\n=== TEST COMPLETE ===\n");
    return 0;
}
