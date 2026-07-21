#include "Vaxi_core_hw.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

class AxiTb {
public:
    Vaxi_core_hw* dut;
    int cycle;
    
    AxiTb() {
        dut = new Vaxi_core_hw;
        cycle = 0;
        
        // Initialize all inputs
        dut->clk = 0;
        dut->rst = 1;
        
        // AXI Master inputs (unused, but need valid values)
        dut->axm_m0_awready = 1;
        dut->axm_m0_wready = 1;
        dut->axm_m0_bid = 0;
        dut->axm_m0_bvalid = 0;
        dut->axm_m0_arready = 1;
        dut->axm_m0_rid = 0;
        // axm_m0_rdata is 1024 bits - leave as default
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
        
        // Reset
        tick();
        tick();
        dut->rst = 0;
        tick();
    }
    
    ~AxiTb() { delete dut; }
    
    void tick() {
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        cycle++;
    }
    
    // AXI-Lite write transaction
    void axi_write(uint32_t addr, uint64_t data) {
        // Present address and data
        dut->axi_lite_s_awaddr = addr;
        dut->axi_lite_s_awvalid = 1;
        dut->axi_lite_s_wdata = data;
        dut->axi_lite_s_wstrb = 0xFF;  // All bytes valid
        dut->axi_lite_s_wvalid = 1;
        
        // Wait for ready
        while (!dut->axi_lite_s_awready || !dut->axi_lite_s_wready) {
            tick();
        }
        tick();
        
        // Deassert valid
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        
        // Wait for response
        while (!dut->axi_lite_s_bvalid) {
            tick();
        }
        tick();
    }
    
    // AXI-Lite read transaction
    uint64_t axi_read(uint32_t addr) {
        // Present address
        dut->axi_lite_s_araddr = addr;
        dut->axi_lite_s_arvalid = 1;
        
        // Wait for address ready
        while (!dut->axi_lite_s_arready) {
            tick();
        }
        tick();
        
        // Deassert valid
        dut->axi_lite_s_arvalid = 0;
        
        // Wait for data
        while (!dut->axi_lite_s_rvalid) {
            tick();
        }
        uint64_t data = dut->axi_lite_s_rdata;
        tick();
        
        return data;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    AxiTb tb;
    
    printf("AXI Core + RISC-V SoC Test\n");
    printf("==========================\n\n");
    
    // Test program
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
    
    // Reset CPU
    printf("Resetting CPU...\n");
    tb.axi_write(0x00, 0x02);  // RESET
    for (int i = 0; i < 5; i++) tb.tick();
    
    // Run CPU
    printf("Starting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN
    
    // Let CPU run
    printf("Running for 20 cycles...\n");
    for (int i = 0; i < 20; i++) {
        tb.tick();
    }
    
    // Stop CPU
    tb.axi_write(0x00, 0x00);  // STOP
    
    // Read results
    printf("\nReading results...\n");
    
    uint64_t pc = tb.axi_read(0x10);
    uint64_t dmem0 = tb.axi_read(0x2000);
    
    printf("  PC:      0x%lX\n", pc);
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
