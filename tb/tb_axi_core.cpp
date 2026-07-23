#include "Vaxi_core_hw.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

class AxiTb {
public:
    Vaxi_core_hw* dut;
    int cycle;
    
    int fast_cycle;  // Track fast clock cycles (unused in simple mode)
    
    AxiTb() {
        dut = new Vaxi_core_hw;
        cycle = 0;
        fast_cycle = 0;
        
        // Initialize all inputs
        dut->clk = 0;
        dut->cpu_clk = 0;
        dut->rst = 1;
        
        // AXI Master inputs (unused, but need valid values)
        dut->axm_m0_awready = 1;
        dut->axm_m0_wready = 1;
        dut->axm_m0_bid = 0;
        dut->axm_m0_bvalid = 0;
        dut->axm_m0_arready = 1;
        dut->axm_m0_rid = 0;
        dut->axm_m0_rlast = 0;
        dut->axm_m0_rvalid = 0;
        
        // AXI Lite Slave inputs
        dut->axi_lite_s_awaddr = 0;
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wdata = 0;
        dut->axi_lite_s_wstrb = 0;
        dut->axi_lite_s_wvalid = 0;
        dut->axi_lite_s_bready = 1;
        dut->axi_lite_s_araddr = 0;
        dut->axi_lite_s_arvalid = 0;
        dut->axi_lite_s_rready = 1;
        
        // Reset for a few cycles
        for (int i = 0; i < 10; i++) tick();
        dut->rst = 0;
        for (int i = 0; i < 10; i++) tick();
    }
    
    ~AxiTb() { delete dut; }
    
    void tick() {
        // Run both clocks together for simplicity
        // Real hardware has 4:1 ratio but with PCIe latency between writes
        dut->clk = 0;
        dut->cpu_clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->cpu_clk = 1;
        dut->eval();
        cycle++;
    }
    
    // Fast tick - same as tick for now (simplifies testing)
    void fast_tick() {
        tick();
    }
    
    // AXI-Lite write transaction (uses fast_tick for AXI timing)
    void axi_write(uint32_t addr, uint64_t data) {
        dut->axi_lite_s_awaddr = addr;
        dut->axi_lite_s_awvalid = 1;
        dut->axi_lite_s_wdata = data;
        dut->axi_lite_s_wstrb = 0xFF;
        dut->axi_lite_s_wvalid = 1;
        
        int timeout = 100;
        while ((!dut->axi_lite_s_awready || !dut->axi_lite_s_wready) && timeout-- > 0) {
            fast_tick();
        }
        fast_tick();
        
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        
        timeout = 100;
        while (!dut->axi_lite_s_bvalid && timeout-- > 0) {
            fast_tick();
        }
        fast_tick();
        
        // Wait for write to propagate to CPU domain (stretch completes)
        // Need at least 10 fast clocks for the stretch, plus margin
        for (int i = 0; i < 16; i++) fast_tick();
    }
    
    // AXI-Lite read transaction (uses fast_tick for AXI timing)
    uint64_t axi_read(uint32_t addr) {
        dut->axi_lite_s_araddr = addr;
        dut->axi_lite_s_arvalid = 1;
        
        int timeout = 100;
        while (!dut->axi_lite_s_arready && timeout-- > 0) {
            fast_tick();
        }
        fast_tick();
        
        dut->axi_lite_s_arvalid = 0;
        
        timeout = 100;
        while (!dut->axi_lite_s_rvalid && timeout-- > 0) {
            fast_tick();
        }
        uint64_t data = dut->axi_lite_s_rdata;
        fast_tick();
        
        return data;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    AxiTb tb;
    
    printf("AXI Core + RISC-V SoC Test\n");
    printf("==========================\n\n");

    uint32_t program[] = {
        0x00500093,  // ADDI x1, x0, 5
        0x00300113,  // ADDI x2, x0, 3
        0x002081b3,  // ADD  x3, x1, x2
        0x00302023,  // SW   x3, 0(x0)
        0x00000063,  // BEQ  x0, x0, 0 (loop)
    };
    
    // Load program to IMEM (BAR offset 0x1000)
    printf("Loading program to IMEM...\n");
    for (int i = 0; i < 5; i++) {
        tb.axi_write(0x1000 + i * 4, program[i]);
    }
    
    // Verify IMEM content by reading back
    printf("\nVerifying IMEM content:\n");
    for (int i = 0; i < 5; i++) {
        uint64_t val = tb.axi_read(0x1000 + i * 4);
        printf("  IMEM[%d] = 0x%08lX (expected 0x%08X) %s\n", 
               i, val, program[i], val == program[i] ? "OK" : "FAIL");
    }
    
    // Reset CPU
    printf("\nResetting CPU...\n");
    tb.axi_write(0x00, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Read status
    uint64_t status = tb.axi_read(0x08);
    printf("  Status after reset: 0x%lX\n", status);
    
    // Run CPU
    printf("\nStarting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN
    
    // Let CPU run
    printf("Running for 30 cycles...\n");
    for (int i = 0; i < 30; i++) {
        tb.tick();
    }
    
    // Stop CPU
    tb.axi_write(0x00, 0x00);  // STOP
    
    // Read results
    printf("\nResults:\n");
    
    uint64_t ctrl = tb.axi_read(0x00);
    status = tb.axi_read(0x08);
    uint64_t pc = tb.axi_read(0x10);
    uint64_t result = tb.axi_read(0x18);
    uint64_t dmem0 = tb.axi_read(0x2000);
    
    printf("  CTRL:    0x%lX\n", ctrl);
    printf("  STATUS:  0x%lX\n", status);
    printf("  PC:      0x%lX\n", pc);
    printf("  RESULT:  0x%lX\n", result);
    printf("  DMEM[0]: %lu (expected 8)\n", dmem0);
    
    // Verify
    printf("\n========================\n");
    if (dmem0 == 8) {
        printf("ALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("TEST FAILED!\n");
        return 1;
    }
}
