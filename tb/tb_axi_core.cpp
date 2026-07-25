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
    };
    const int program_size = sizeof(program) / sizeof(program[0]);
    
    // ------------------------------------------------------------------------
    // Load Program to IMEM
    // ------------------------------------------------------------------------
    printf("Loading program to IMEM (via AXI-Lite)...\n");
    
    // Write instruction pairs (AXI wrapper splits 64-bit into two 32-bit writes)
    for (int i = 0; i < program_size; i += 2) {
        uint32_t even = program[i];
        uint32_t odd = (i + 1 < program_size) ? program[i + 1] : 0x00000013;  // NOP if odd
        uint64_t pair = ((uint64_t)odd << 32) | even;
        if (!tb.axi_write(0x1000 + i * 4, pair)) {
            printf("  ERROR: Write to IMEM[%d,%d] timed out!\n", i, i+1);
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
        uint64_t readback = tb.axi_read(0x1000 + (i & ~1) * 4);  // Align to pair
        uint32_t word = (i & 1) ? (uint32_t)(readback >> 32) : (uint32_t)readback;
        bool match = (word == program[i]);
        printf("  IMEM[%d] = 0x%08X %s\n", i, word, match ? "OK" : "MISMATCH");
        if (!match) errors++;
    }
    printf("\n");
    
    // ------------------------------------------------------------------------
    // Reset and Run CPU
    // ------------------------------------------------------------------------
    printf("Resetting CPU...\n");
    tb.axi_write(0x00, 0x02);  // RESET bit
    for (int i = 0; i < 10; i++) tb.tick();
    
    printf("Starting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN bit
    for (int i = 0; i < 10; i++) tb.tick();  // Let write complete
    
    // Let CPU execute
    printf("Running for 50 cycles...\n");
    for (int i = 0; i < 50; i++) {
        tb.tick();
    }
    
    // Stop CPU
    tb.axi_write(0x00, 0x00);
    printf("\n");
    
    // ------------------------------------------------------------------------
    // Read Results
    // ------------------------------------------------------------------------
    printf("Results:\n");
    
    uint32_t status = (uint32_t)tb.axi_read(0x08);  // Lower 32 bits only
    uint32_t pc = (uint32_t)tb.axi_read(0x10);      // Lower 32 bits only
    uint64_t dmem_pair = tb.axi_read(0x2000);
    uint32_t dmem0 = (uint32_t)dmem_pair;  // Even word in lower 32 bits
    
    printf("  STATUS  = 0x%X\n", status);
    printf("  PC      = 0x%X\n", pc);
    printf("  DMEM[0] = %u (expected 8)\n", dmem0);
    
    // Verify DMEM[0] = 8
    if (dmem0 != 8) {
        printf("  ERROR: DMEM[0] incorrect!\n");
        errors++;
    }
    
    // ------------------------------------------------------------------------
    // Read Debug Registers
    // ------------------------------------------------------------------------
    printf("\nSoC DMEM write debug:\n");
    printf("  Write count:   %u\n", (uint32_t)tb.axi_read(0x30));
    printf("  CPU addr:      0x%X\n", (uint32_t)tb.axi_read(0x20));
    printf("  CPU data:      %u (0x%X)\n", (uint32_t)tb.axi_read(0x28), (uint32_t)tb.axi_read(0x28));
    printf("  Mux addr:      0x%X\n", (uint32_t)tb.axi_read(0x38));
    printf("  Mux data:      %u (0x%X)\n", (uint32_t)tb.axi_read(0x40), (uint32_t)tb.axi_read(0x40));
    uint32_t flags = (uint32_t)tb.axi_read(0x48);
    printf("  host_dmem_wen: %u\n", flags & 1);
    printf("  dmem_wen:      %u\n", (flags >> 1) & 1);
    
    printf("\nDirect DMEM read (via debug regs):\n");
    printf("  dmem[0]:       %u (0x%X)\n", (uint32_t)tb.axi_read(0x50), (uint32_t)tb.axi_read(0x50));
    printf("  dmem[1]:       %u (0x%X)\n", (uint32_t)tb.axi_read(0x58), (uint32_t)tb.axi_read(0x58));
    
    printf("\nAXI read path debug (after DMEM[0] read):\n");
    // Re-read DMEM to capture debug
    tb.axi_read(0x2000);
    printf("  SoC rdata:     0x%X\n", (uint32_t)tb.axi_read(0x118));
    printf("  SoC raddr:     0x%X\n", (uint32_t)tb.axi_read(0x11C));
    printf("  read_mux:      0x%X\n", (uint32_t)tb.axi_read(0x120));
    
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
