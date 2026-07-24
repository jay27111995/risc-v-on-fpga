// ============================================================================
// RISC-V SoC Testbench
// ============================================================================
//
// Tests the riscv_soc module directly (without AXI wrapper).
// Verifies basic instruction execution, memory operations, and pipeline behavior.
//
// Test Program:
//   0x00: ADDI x1, x0, 5      # x1 = 5
//   0x04: ADDI x2, x0, 3      # x2 = 3
//   0x08: ADD  x3, x1, x2     # x3 = 8 (tests data forwarding)
//   0x0C: SUB  x4, x1, x2     # x4 = 2
//   0x10: SW   x3, 0(x0)      # DMEM[0] = 8
//   0x14: LW   x5, 0(x0)      # x5 = 8 (load what we just stored)
//   0x18: LW   x6, 4(x0)      # x6 = 200 (pre-initialized value)
//   0x1C: BEQ  x0, x0, 0      # loop forever
//
// Expected Results:
//   - DMEM[0] = 8 (from SW x3)
//   - DMEM[1] = 200 (pre-initialized, read by LW x6)
//   - PC loops around 0x1C-0x20
//
// ============================================================================

#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

// ============================================================================
// Testbench Helper Class
// ============================================================================

class SocTestbench {
public:
    Vriscv_soc* soc;
    
    SocTestbench() {
        soc = new Vriscv_soc;
        
        // Initialize all inputs
        soc->rst_n = 0;
        soc->clk = 0;
        soc->bar_wen = 0;
        soc->bar_ren = 0;
        soc->bar_addr = 0;
        soc->bar_wdata = 0;
        
        // Reset sequence
        tick();
        soc->rst_n = 1;
        tick();
    }
    
    ~SocTestbench() {
        delete soc;
    }
    
    // Single clock cycle
    void tick() {
        soc->clk = 0;
        soc->eval();
        soc->clk = 1;
        soc->eval();
    }
    
    // BAR write (single cycle)
    void bar_write(uint16_t addr, uint64_t data) {
        soc->bar_addr = addr;
        soc->bar_wdata = data;
        soc->bar_wen = 1;
        soc->bar_ren = 0;
        tick();
        soc->bar_wen = 0;
    }
    
    // BAR read (requires 2 cycles for registered read)
    uint64_t bar_read(uint16_t addr) {
        soc->bar_addr = addr;
        soc->bar_wen = 0;
        soc->bar_ren = 1;   // Assert read enable
        tick();             // Address latched, data captured
        soc->bar_ren = 0;
        tick();             // Data available
        // Return the word requested (even=lower, odd=upper)
        if (addr & 0x4)
            return soc->bar_rdata >> 32;
        else
            return soc->bar_rdata;
    }
    
    // ---- IMEM Access (writes pairs) ----
    void write_imem_pair(uint32_t even_idx, uint32_t word0, uint32_t word1) {
        uint64_t pair = ((uint64_t)word1 << 32) | word0;
        bar_write(0x1000 + even_idx * 4, pair);
    }
    
    void write_imem(uint32_t word_idx, uint32_t instr) {
        // For single writes, pair with NOP
        uint32_t even_idx = word_idx & ~1;
        if (word_idx & 1) {
            write_imem_pair(even_idx, 0x00000013, instr);  // NOP, then instr
        } else {
            write_imem_pair(even_idx, instr, 0x00000013);  // instr, then NOP
        }
    }
    
    // ---- DMEM Access (writes pairs) ----
    void write_dmem_pair(uint32_t even_idx, uint32_t word0, uint32_t word1) {
        uint64_t pair = ((uint64_t)word1 << 32) | word0;
        bar_write(0x2000 + even_idx * 4, pair);
    }
    
    void write_dmem(uint32_t word_idx, uint32_t data) {
        // For single writes, pair with 0
        uint32_t even_idx = word_idx & ~1;
        if (word_idx & 1) {
            write_dmem_pair(even_idx, 0, data);
        } else {
            write_dmem_pair(even_idx, data, 0);
        }
    }
    
    uint32_t read_dmem(uint32_t word_idx) {
        return static_cast<uint32_t>(bar_read(0x2000 + word_idx * 4));
    }
    
    // ---- Control Registers ----
    void reset_cpu() {
        bar_write(0x00, 0x02);  // Set RESET bit
        tick();                 // Wait for self-clear
    }
    
    void start_cpu() {
        bar_write(0x00, 0x01);  // Set RUN bit
    }
    
    void stop_cpu() {
        bar_write(0x00, 0x00);  // Clear RUN bit
    }
    
    uint32_t read_pc() {
        return static_cast<uint32_t>(bar_read(0x10));
    }
    
    uint32_t read_status() {
        return static_cast<uint32_t>(bar_read(0x08));
    }
};

// ============================================================================
// Main Test
// ============================================================================

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    SocTestbench tb;
    int errors = 0;
    
    printf("RISC-V SoC Testbench\n");
    printf("====================\n\n");
    
    // ------------------------------------------------------------------------
    // Load Test Program
    // ------------------------------------------------------------------------
    printf("Loading test program...\n");
    
    // Write instruction pairs
    tb.write_imem_pair(0, 0x00500093, 0x00300113);  // ADDI x1, x0, 5 | ADDI x2, x0, 3
    tb.write_imem_pair(2, 0x002081b3, 0x40208233);  // ADD  x3, x1, x2 | SUB  x4, x1, x2
    tb.write_imem_pair(4, 0x00302023, 0x00002283);  // SW   x3, 0(x0) | LW   x5, 0(x0)
    tb.write_imem_pair(6, 0x00402303, 0x00000063);  // LW   x6, 4(x0) | BEQ  x0, x0, 0
    printf("  Loaded 8 instructions\n");
    
    // Pre-initialize DMEM[1] = 200 for the LW x6, 4(x0) test
    tb.write_dmem_pair(0, 0, 200);  // DMEM[0]=0, DMEM[1]=200
    printf("  Pre-initialized DMEM[1] = 200\n\n");
    
    // ------------------------------------------------------------------------
    // Execute Program
    // ------------------------------------------------------------------------
    printf("Executing program...\n");
    
    tb.reset_cpu();
    tb.start_cpu();
    
    // Run for 20 cycles (enough for 8 instructions + pipeline fill/drain)
    for (int i = 0; i < 20; i++) {
        tb.tick();
    }
    
    tb.stop_cpu();
    printf("  Ran for 20 cycles\n\n");
    
    // ------------------------------------------------------------------------
    // Verify Results
    // ------------------------------------------------------------------------
    printf("Results:\n");
    
    uint32_t dmem0 = tb.read_dmem(0);
    uint32_t dmem1 = tb.read_dmem(1);
    uint32_t pc = tb.read_pc();
    
    printf("  DMEM[0] = %u (expected 8)\n", dmem0);
    printf("  DMEM[1] = %u (expected 200)\n", dmem1);
    printf("  PC      = 0x%02X (expected 0x1C-0x20)\n", pc);
    
    // Check DMEM[0] = 8 (result of ADD x3, x1, x2 stored by SW)
    if (dmem0 != 8) {
        printf("  ERROR: DMEM[0] incorrect!\n");
        errors++;
    }
    
    // Check DMEM[1] = 200 (pre-initialized value, should be unchanged)
    if (dmem1 != 200) {
        printf("  ERROR: DMEM[1] incorrect!\n");
        errors++;
    }
    
    // Check PC is in expected range (looping on BEQ at 0x1C)
    // With 5-stage pipeline, PC can be 0x1C, 0x20, or nearby due to pipeline
    if (pc < 0x1C || pc > 0x24) {
        printf("  ERROR: PC out of expected range!\n");
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
