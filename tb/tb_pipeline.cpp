// ============================================================================
// Pipeline Corner Case Testbench
// ============================================================================
//
// Focused tests for pipeline hazards and corner cases:
// 1. Store forwarding - SW using value just computed by ADD
// 2. Load-use hazard - LW followed by instruction using loaded value  
// 3. Back-to-back stores
// 4. Store after branch
// 5. Forwarding from MEM stage
// 6. Forwarding from WB stage
// 7. x0 handling (writes to x0 should be ignored)
//
// ============================================================================

#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

// ============================================================================
// Testbench Helper Class
// ============================================================================

class PipelineTestbench {
public:
    Vriscv_soc* soc;
    int cycle_count;
    
    PipelineTestbench() {
        soc = new Vriscv_soc;
        cycle_count = 0;
        
        soc->rst_n = 0;
        soc->clk = 0;
        soc->bar_wen = 0;
        soc->bar_ren = 0;
        soc->bar_addr = 0;
        soc->bar_wdata = 0;
        
        tick();
        tick();
        soc->rst_n = 1;
        tick();
    }
    
    ~PipelineTestbench() {
        delete soc;
    }
    
    void tick() {
        soc->clk = 0;
        soc->eval();
        soc->clk = 1;
        soc->eval();
        cycle_count++;
    }
    
    void bar_write(uint32_t addr, uint32_t data) {
        soc->bar_wen = 1;
        soc->bar_addr = addr;
        soc->bar_wdata = data;
        tick();
        soc->bar_wen = 0;
        tick();
    }
    
    uint32_t bar_read(uint32_t addr) {
        soc->bar_ren = 1;
        soc->bar_addr = addr;
        tick();
        soc->bar_ren = 0;
        tick();
        return static_cast<uint32_t>(soc->bar_rdata);
    }
    
    void write_imem(uint32_t idx, uint32_t instr) {
        bar_write(0x1000 + idx * 4, instr);
    }
    
    void write_dmem(uint32_t idx, uint32_t data) {
        bar_write(0x2000 + idx * 4, data);
    }
    
    uint32_t read_dmem(uint32_t idx) {
        return bar_read(0x2000 + idx * 4);
    }
    
    uint32_t read_pc() {
        return static_cast<uint32_t>(bar_read(0x10));
    }
    
    void reset_cpu() {
        bar_write(0x00, 0x02);  // Reset bit
        for (int i = 0; i < 5; i++) tick();
        bar_write(0x00, 0x00);  // Clear reset
        for (int i = 0; i < 5; i++) tick();
    }
    
    void start_cpu() {
        bar_write(0x00, 0x01);  // Run bit
    }
    
    void stop_cpu() {
        bar_write(0x00, 0x00);  // Clear run bit
    }
    
    void run_cycles(int n) {
        for (int i = 0; i < n; i++) tick();
    }
    
    void clear_imem() {
        for (int i = 0; i < 32; i++) {
            write_imem(i, 0x00000013);  // NOP (addi x0, x0, 0)
        }
    }
    
    void clear_dmem() {
        for (int i = 0; i < 8; i++) {
            write_dmem(i, 0);
        }
    }
};

// ============================================================================
// Test Functions
// ============================================================================

int test_store_forwarding_mem(PipelineTestbench& tb) {
    // Test: Store uses value from instruction in MEM stage (1 cycle ahead)
    // This is the FPGA failure case!
    //
    // Cycle: IF      ID       EX       MEM      WB
    //   1    ADDI x1
    //   2    ADDI x2  ADDI x1
    //   3    ADD x3   ADDI x2  ADDI x1
    //   4    SW x3    ADD x3   ADDI x2  ADDI x1          <- SW in ID needs x3
    //   5    BEQ      SW x3    ADD x3   ADDI x2  ADDI x1 <- SW in EX, ADD in MEM (forward!)
    //   6    -        BEQ      SW x3    ADD x3   ADDI x2 <- SW in MEM, writes DMEM[0]=8
    
    printf("Test 1: Store forwarding from MEM stage\n");
    printf("  Program: x1=5, x2=3, x3=x1+x2=8, SW x3 to DMEM[0]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5     # x1 = 5
    tb.write_imem(1, 0x00300113);  // ADDI x2, x0, 3     # x2 = 3  
    tb.write_imem(2, 0x002081B3);  // ADD  x3, x1, x2    # x3 = 8
    tb.write_imem(3, 0x00302023);  // SW   x3, 0(x0)     # DMEM[0] = 8 (forward from MEM)
    tb.write_imem(4, 0x00000063);  // BEQ  x0, x0, 0     # loop
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(20);
    tb.stop_cpu();
    
    uint32_t dmem0 = tb.read_dmem(0);
    printf("  DMEM[0] = %u (expected 8)\n", dmem0);
    
    if (dmem0 != 8) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_store_forwarding_wb(PipelineTestbench& tb) {
    // Test: Store uses value from instruction in WB stage (2 cycles ahead)
    //
    // ADD x3, NOOP, SW x3 - SW needs x3 from WB stage
    
    printf("Test 2: Store forwarding from WB stage\n");
    printf("  Program: x3=5+3=8, NOP, SW x3 to DMEM[1]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5
    tb.write_imem(1, 0x00300113);  // ADDI x2, x0, 3
    tb.write_imem(2, 0x002081B3);  // ADD  x3, x1, x2    # x3 = 8
    tb.write_imem(3, 0x00000013);  // NOP
    tb.write_imem(4, 0x00302223);  // SW   x3, 4(x0)     # DMEM[1] = 8 (forward from WB)
    tb.write_imem(5, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(25);
    tb.stop_cpu();
    
    uint32_t dmem1 = tb.read_dmem(1);
    printf("  DMEM[1] = %u (expected 8)\n", dmem1);
    
    if (dmem1 != 8) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_store_no_forwarding(PipelineTestbench& tb) {
    // Test: Store uses value that's already in register file (no forwarding needed)
    //
    // x3 computed, multiple NOPs, then SW x3
    
    printf("Test 3: Store without forwarding (value in regfile)\n");
    printf("  Program: x3=8, NOPs, SW x3 to DMEM[2]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5
    tb.write_imem(1, 0x00300113);  // ADDI x2, x0, 3
    tb.write_imem(2, 0x002081B3);  // ADD  x3, x1, x2    # x3 = 8
    tb.write_imem(3, 0x00000013);  // NOP
    tb.write_imem(4, 0x00000013);  // NOP  
    tb.write_imem(5, 0x00000013);  // NOP
    tb.write_imem(6, 0x00302423);  // SW   x3, 8(x0)     # DMEM[2] = 8 (from regfile)
    tb.write_imem(7, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(30);
    tb.stop_cpu();
    
    uint32_t dmem2 = tb.read_dmem(2);
    printf("  DMEM[2] = %u (expected 8)\n", dmem2);
    
    if (dmem2 != 8) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_load_use_hazard(PipelineTestbench& tb) {
    // Test: Load followed immediately by instruction using loaded value
    // This requires a stall cycle
    //
    // Pre-init DMEM[3] = 42, then:
    //   LW x4, 12(x0)    # x4 = 42
    //   ADD x5, x4, x1   # x5 = 42 + 5 = 47 (needs stall)
    //   SW x5, 16(x0)    # DMEM[4] = 47
    
    printf("Test 4: Load-use hazard (requires stall)\n");
    printf("  Program: load DMEM[3]=42 into x4, add x4+x1, store to DMEM[4]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    tb.write_dmem(3, 42);  // Pre-initialize DMEM[3]
    
    // Verify DMEM[3] was written
    uint32_t check = tb.read_dmem(3);
    printf("  DMEM[3] pre-init check = %u (expected 42)\n", check);
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5
    tb.write_imem(1, 0x00C02203);  // LW   x4, 12(x0)    # x4 = DMEM[3] = 42
    tb.write_imem(2, 0x001202B3);  // ADD  x5, x4, x1    # x5 = 42 + 5 = 47 (load-use!)
    tb.write_imem(3, 0x00502823);  // SW   x5, 16(x0)    # DMEM[4] = 47
    tb.write_imem(4, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(25);
    tb.stop_cpu();
    
    uint32_t dmem4 = tb.read_dmem(4);
    printf("  DMEM[4] = %u (expected 47)\n", dmem4);
    
    if (dmem4 != 47) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_back_to_back_stores(PipelineTestbench& tb) {
    // Test: Multiple consecutive store instructions
    //
    // SW x1, 20(x0)   # DMEM[5] = 5
    // SW x2, 24(x0)   # DMEM[6] = 3
    // SW x3, 28(x0)   # DMEM[7] = 8
    
    printf("Test 5: Back-to-back stores\n");
    printf("  Program: SW x1,x2,x3 to DMEM[5,6,7]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5
    tb.write_imem(1, 0x00300113);  // ADDI x2, x0, 3
    tb.write_imem(2, 0x00800193);  // ADDI x3, x0, 8
    tb.write_imem(3, 0x00102A23);  // SW   x1, 20(x0)   # DMEM[5] = 5
    tb.write_imem(4, 0x00202C23);  // SW   x2, 24(x0)   # DMEM[6] = 3
    tb.write_imem(5, 0x00302E23);  // SW   x3, 28(x0)   # DMEM[7] = 8
    tb.write_imem(6, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(25);
    tb.stop_cpu();
    
    uint32_t dmem5 = tb.read_dmem(5);
    uint32_t dmem6 = tb.read_dmem(6);
    uint32_t dmem7 = tb.read_dmem(7);
    printf("  DMEM[5] = %u (expected 5)\n", dmem5);
    printf("  DMEM[6] = %u (expected 3)\n", dmem6);
    printf("  DMEM[7] = %u (expected 8)\n", dmem7);
    
    int errors = 0;
    if (dmem5 != 5) errors++;
    if (dmem6 != 3) errors++;
    if (dmem7 != 8) errors++;
    
    if (errors) {
        printf("  FAILED!\n\n");
        return errors;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_store_address_forwarding(PipelineTestbench& tb) {
    // Test: Store where BASE ADDRESS comes from forwarding
    // 
    // ADDI x6, x0, 4    # x6 = 4 (byte address for DMEM[1])
    // ADDI x7, x0, 99
    // SW x7, 0(x6)      # DMEM[1] = 99 (forward x6 for address)
    
    printf("Test 6: Store with forwarded base address\n");
    printf("  Program: x6=4, x7=99, SW x7,0(x6) -> DMEM[1]=99\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    
    tb.write_imem(0, 0x00400313);  // ADDI x6, x0, 4     # x6 = 4
    tb.write_imem(1, 0x06300393);  // ADDI x7, x0, 99    # x7 = 99
    tb.write_imem(2, 0x00732023);  // SW   x7, 0(x6)     # DMEM[1] = 99
    tb.write_imem(3, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(20);
    tb.stop_cpu();
    
    uint32_t dmem1 = tb.read_dmem(1);
    printf("  DMEM[1] = %u (expected 99)\n", dmem1);
    
    if (dmem1 != 99) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

int test_x0_writes_ignored(PipelineTestbench& tb) {
    // Test: Writes to x0 should be ignored, x0 always reads 0
    //
    // ADDI x0, x0, 100   # Should be ignored, x0 stays 0
    // SW x0, 0(x0)       # DMEM[0] = 0 (x0 is still 0)
    
    printf("Test 7: x0 writes ignored\n");
    printf("  Program: ADDI x0,100 (ignored), SW x0 to DMEM[0]\n");
    
    tb.clear_imem();
    tb.clear_dmem();
    tb.write_dmem(0, 0xDEADBEEF);  // Pre-fill with garbage
    
    tb.write_imem(0, 0x06400013);  // ADDI x0, x0, 100   # x0 should stay 0
    tb.write_imem(1, 0x00002023);  // SW   x0, 0(x0)     # DMEM[0] = 0
    tb.write_imem(2, 0x00000063);  // BEQ  x0, x0, 0
    
    tb.reset_cpu();
    tb.start_cpu();
    tb.run_cycles(20);
    tb.stop_cpu();
    
    uint32_t dmem0 = tb.read_dmem(0);
    printf("  DMEM[0] = %u (expected 0)\n", dmem0);
    
    if (dmem0 != 0) {
        printf("  FAILED!\n\n");
        return 1;
    }
    printf("  PASSED\n\n");
    return 0;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    PipelineTestbench tb;
    int total_errors = 0;
    
    printf("================================================================\n");
    printf("Pipeline Corner Case Testbench\n");
    printf("================================================================\n\n");
    
    total_errors += test_store_forwarding_mem(tb);
    total_errors += test_store_forwarding_wb(tb);
    total_errors += test_store_no_forwarding(tb);
    total_errors += test_load_use_hazard(tb);
    total_errors += test_back_to_back_stores(tb);
    total_errors += test_store_address_forwarding(tb);
    total_errors += test_x0_writes_ignored(tb);
    
    printf("================================================================\n");
    if (total_errors == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("FAILED: %d error(s)\n", total_errors);
    }
    printf("================================================================\n");
    
    return total_errors ? 1 : 0;
}
