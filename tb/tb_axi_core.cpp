// ============================================================================
// AXI Core + RISC-V SoC Testbench
// ============================================================================
//
// Tests the complete system: AXI-Lite wrapper (axi_core_hw) + RISC-V SoC.
// This is closer to the actual FPGA configuration where PCIe accesses
// come through AXI-Lite.
//
// Test Program:
//   0x00: ADDI x1, x0, 5      # x1 = 5
//   0x04: ADDI x2, x0, 3      # x2 = 3
//   0x08: ADD  x3, x1, x2     # x3 = 8
//   0x0C: SW   x3, 0(x0)      # DMEM[0] = 8
//   0x10: BEQ  x0, x0, 0      # loop forever
//
// Expected Result:
//   - DMEM[0] = 8
//
// ============================================================================

#include "Vaxi_core_hw.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

// ============================================================================
// AXI-Lite Testbench Helper Class
// ============================================================================

class AxiTestbench {
public:
    Vaxi_core_hw* dut;
    int cycle_count;
    
    AxiTestbench() {
        dut = new Vaxi_core_hw;
        cycle_count = 0;
        
        // Initialize clocks and reset
        dut->clk = 0;
        dut->cpu_clk = 0;
        dut->rst = 1;  // Active high reset
        
        // Initialize AXI Master interface (unused in this test, directly tied off)
        dut->axm_m0_awready = 1;
        dut->axm_m0_wready = 1;
        dut->axm_m0_bid = 0;
        dut->axm_m0_bvalid = 0;
        dut->axm_m0_arready = 1;
        dut->axm_m0_rid = 0;
        dut->axm_m0_rlast = 0;
        dut->axm_m0_rvalid = 0;
        
        // Initialize AXI-Lite Slave interface
        dut->axi_lite_s_awaddr = 0;
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wdata = 0;
        dut->axi_lite_s_wstrb = 0;
        dut->axi_lite_s_wvalid = 0;
        dut->axi_lite_s_bready = 1;
        dut->axi_lite_s_araddr = 0;
        dut->axi_lite_s_arvalid = 0;
        dut->axi_lite_s_rready = 1;
        
        // Reset sequence
        for (int i = 0; i < 10; i++) tick();
        dut->rst = 0;
        for (int i = 0; i < 10; i++) tick();
    }
    
    ~AxiTestbench() {
        delete dut;
    }
    
    // Single clock cycle (both clocks together for simplicity in test)
    void tick() {
        dut->clk = 0;
        dut->cpu_clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->cpu_clk = 1;
        dut->eval();
        cycle_count++;
    }
    
    // AXI-Lite write transaction
    bool axi_write(uint32_t addr, uint64_t data, int timeout = 100) {
        // Address and data phase (can be simultaneous in AXI-Lite)
        dut->axi_lite_s_awaddr = addr;
        dut->axi_lite_s_awvalid = 1;
        dut->axi_lite_s_wdata = data;
        dut->axi_lite_s_wstrb = 0xFF;  // All bytes valid
        dut->axi_lite_s_wvalid = 1;
        
        // Wait for handshake
        int count = timeout;
        while ((!dut->axi_lite_s_awready || !dut->axi_lite_s_wready) && count-- > 0) {
            tick();
        }
        if (count <= 0) return false;
        
        tick();  // Complete handshake
        
        // Deassert valid signals
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        
        // Wait for write response
        count = timeout;
        while (!dut->axi_lite_s_bvalid && count-- > 0) {
            tick();
        }
        if (count <= 0) return false;
        
        tick();  // Acknowledge response
        
        // Extra cycles to ensure write propagates
        for (int i = 0; i < 5; i++) tick();
        
        return true;
    }
    
    // AXI-Lite read transaction
    uint64_t axi_read(uint32_t addr, int timeout = 100) {
        // Address phase
        dut->axi_lite_s_araddr = addr;
        dut->axi_lite_s_arvalid = 1;
        
        // Wait for address handshake
        int count = timeout;
        while (!dut->axi_lite_s_arready && count-- > 0) {
            tick();
        }
        
        tick();  // Complete handshake
        dut->axi_lite_s_arvalid = 0;
        
        // Wait for read data
        count = timeout;
        while (!dut->axi_lite_s_rvalid && count-- > 0) {
            tick();
        }
        
        uint64_t data = dut->axi_lite_s_rdata;
        tick();  // Acknowledge data
        
        return data;
    }
};

// ============================================================================
// Main Test
// ============================================================================

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    AxiTestbench tb;
    int errors = 0;
    
    printf("AXI Core + RISC-V SoC Testbench\n");
    printf("===============================\n\n");
    
    // ------------------------------------------------------------------------
    // Test Program
    // ------------------------------------------------------------------------
    const uint32_t program[] = {
        0x00500093,  // ADDI x1, x0, 5
        0x00300113,  // ADDI x2, x0, 3
        0x002081b3,  // ADD  x3, x1, x2
        0x00302023,  // SW   x3, 0(x0)
        0x00000063,  // BEQ  x0, x0, 0
        0x00000013,  // NOP (padding for pair)
    };
    const int program_size = 5;  // Actual instructions (not padding)
    
    // ------------------------------------------------------------------------
    // Load Program to IMEM (write pairs)
    // ------------------------------------------------------------------------
    printf("Loading program to IMEM (via AXI-Lite)...\n");
    
    for (int i = 0; i < 6; i += 2) {
        uint64_t pair = ((uint64_t)program[i + 1] << 32) | program[i];
        if (!tb.axi_write(0x1000 + i * 4, pair)) {
            printf("  ERROR: Write to IMEM[%d:%d] timed out!\n", i, i+1);
            errors++;
        }
    }
    printf("  Loaded %d instructions\n\n", program_size);
    
    // Small delay to ensure last write completes
    for (int i = 0; i < 10; i++) tb.tick();
    
    // ------------------------------------------------------------------------
    // Verify IMEM Content
    // ------------------------------------------------------------------------
    printf("Verifying IMEM content...\n");
    
    for (int i = 0; i < program_size; i++) {
        uint64_t raw = tb.axi_read(0x1000 + (i & ~1) * 4);  // Read pair
        uint32_t readback = (i & 1) ? (raw >> 32) : raw;    // Extract word
        bool match = (readback == program[i]);
        printf("  IMEM[%d] = 0x%08X %s\n", i, readback, match ? "OK" : "MISMATCH");
        if (!match) errors++;
    }
    printf("\n");
    
    // ------------------------------------------------------------------------
    // Reset and Run CPU
    // ------------------------------------------------------------------------
    printf("Resetting CPU...\n");
    tb.axi_write(0x00, 0x02);  // RESET bit
    for (int i = 0; i < 5; i++) tb.tick();
    
    printf("Starting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN bit
    
    // Let CPU execute
    printf("Running for 30 cycles...\n");
    for (int i = 0; i < 30; i++) {
        tb.tick();
    }
    
    // Stop CPU
    tb.axi_write(0x00, 0x00);
    printf("\n");
    
    // ------------------------------------------------------------------------
    // Read Results
    // ------------------------------------------------------------------------
    printf("Results:\n");
    
    uint64_t status = tb.axi_read(0x08);
    uint64_t pc = tb.axi_read(0x10);
    uint64_t dmem_pair = tb.axi_read(0x2000);
    uint32_t dmem0 = (uint32_t)dmem_pair;  // Even word in lower 32 bits
    
    printf("  STATUS  = 0x%lX\n", status);
    printf("  PC      = 0x%lX\n", pc);
    printf("  DMEM[0] = %u (expected 8)\n", dmem0);
    
    // Verify DMEM[0] = 8
    if (dmem0 != 8) {
        printf("  ERROR: DMEM[0] incorrect!\n");
        errors++;
    }
    
    // ------------------------------------------------------------------------
    // Summary
    // ------------------------------------------------------------------------
    printf("\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    
    return errors;
}
