// ============================================================================
// 64-to-32 Bus Adapter Testbench
// ============================================================================
//
// Tests the bus64to32 module which converts 64-bit AXI transactions to
// 32-bit SoC bus transactions.
//
// The PCIe AXI-Lite interface uses 64-bit data width, but the internal SoC
// uses 32-bit. This adapter:
// - Splits 64-bit writes into two 32-bit writes (low then high)
// - Combines two 32-bit reads into one 64-bit read response
// - Handles byte enables for partial writes
//
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include "Vbus64to32.h"
#include "verilated.h"

class Testbench {
public:
    Vbus64to32* dut;
    uint64_t cycle;
    
    // Simulated 32-bit memory
    uint32_t mem[256];
    
    Testbench() {
        dut = new Vbus64to32;
        cycle = 0;
        
        dut->clk = 0;
        dut->rst_n = 0;
        dut->in_addr = 0;
        dut->in_wdata = 0;
        dut->in_wen = 0;
        dut->in_ren = 0;
        dut->in_rdata = 0;
        
        // Clear memory
        for (int i = 0; i < 256; i++) mem[i] = 0;
        
        // Reset
        for (int i = 0; i < 5; i++) tick();
        dut->rst_n = 1;
        for (int i = 0; i < 2; i++) tick();
    }
    
    ~Testbench() {
        dut->final();
        delete dut;
    }
    
    void tick() {
        // Simulate memory on 32-bit side
        if (dut->out_wen) {
            uint32_t idx = (dut->out_addr >> 2) & 0xFF;
            mem[idx] = dut->out_wdata;
            printf("  [cycle %lu] MEM WRITE: addr=0x%04X idx=%d data=0x%08X\n", 
                   cycle, dut->out_addr, idx, dut->out_wdata);
        }
        if (dut->out_ren) {
            uint32_t idx = (dut->out_addr >> 2) & 0xFF;
            dut->in_rdata = mem[idx];
            printf("  [cycle %lu] MEM READ:  addr=0x%04X idx=%d data=0x%08X\n",
                   cycle, dut->out_addr, idx, dut->in_rdata);
        }
        
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        cycle++;
    }
    
    // Initiate 64-bit write, run until done
    void write64(uint16_t addr, uint64_t data) {
        printf("WRITE64: addr=0x%04X data=0x%016lX\n", addr, data);
        dut->in_addr = addr;
        dut->in_wdata = data;
        dut->in_wen = 1;
        tick();
        dut->in_wen = 0;
        
        int timeout = 20;
        while (!dut->out_done && timeout-- > 0) {
            tick();
        }
        tick(); // One more after done
        printf("  Write complete after %lu cycles\n\n", cycle);
    }
    
    // Initiate 64-bit read, run until done
    uint64_t read64(uint16_t addr) {
        printf("READ64: addr=0x%04X\n", addr);
        dut->in_addr = addr;
        dut->in_ren = 1;
        tick();
        dut->in_ren = 0;
        
        int timeout = 20;
        while (!dut->out_done && timeout-- > 0) {
            tick();
        }
        uint64_t result = dut->out_rdata;
        tick(); // One more after done
        printf("  Read complete: 0x%016lX\n\n", result);
        return result;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    Testbench tb;
    int errors = 0;
    
    printf("bus64to32 Adapter Testbench\n");
    printf("===========================\n\n");
    
    // Test 1: Write 64-bit, read back
    printf("=== Test 1: Write and read 64-bit ===\n");
    tb.write64(0x0000, 0xDEADBEEF12345678ULL);
    uint64_t rb = tb.read64(0x0000);
    if (rb == 0xDEADBEEF12345678ULL) {
        printf("PASS: Got expected 0xDEADBEEF12345678\n\n");
    } else {
        printf("FAIL: Expected 0xDEADBEEF12345678, got 0x%016lX\n\n", rb);
        errors++;
    }
    
    // Test 2: Write to different address
    printf("=== Test 2: Write to addr 0x100 ===\n");
    tb.write64(0x0100, 0xCAFEBABE00112233ULL);
    rb = tb.read64(0x0100);
    if (rb == 0xCAFEBABE00112233ULL) {
        printf("PASS: Got expected 0xCAFEBABE00112233\n\n");
    } else {
        printf("FAIL: Expected 0xCAFEBABE00112233, got 0x%016lX\n\n", rb);
        errors++;
    }
    
    // Test 3: Verify first write still intact
    printf("=== Test 3: Verify first write intact ===\n");
    rb = tb.read64(0x0000);
    if (rb == 0xDEADBEEF12345678ULL) {
        printf("PASS: First write still intact\n\n");
    } else {
        printf("FAIL: First write corrupted, got 0x%016lX\n\n", rb);
        errors++;
    }
    
    // Test 4: Check memory contents directly
    printf("=== Test 4: Check 32-bit memory contents ===\n");
    printf("mem[0] = 0x%08X (expected 0x12345678)\n", tb.mem[0]);
    printf("mem[1] = 0x%08X (expected 0xDEADBEEF)\n", tb.mem[1]);
    printf("mem[64] = 0x%08X (expected 0x00112233)\n", tb.mem[64]);
    printf("mem[65] = 0x%08X (expected 0xCAFEBABE)\n", tb.mem[65]);
    
    if (tb.mem[0] == 0x12345678 && tb.mem[1] == 0xDEADBEEF &&
        tb.mem[64] == 0x00112233 && tb.mem[65] == 0xCAFEBABE) {
        printf("PASS: All memory contents correct\n\n");
    } else {
        printf("FAIL: Memory contents incorrect\n\n");
        errors++;
    }
    
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== %d TESTS FAILED ===\n", errors);
    }
    
    return errors;
}
