#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

class SocTb {
public:
    Vriscv_soc* soc;
    
    SocTb() {
        soc = new Vriscv_soc;
        soc->rst_n = 0;
        soc->clk = 0;
        soc->clk_en = 1;  // Always enable for simulation
        soc->bar_wen = 0;
        soc->bar_addr = 0;
        soc->bar_wdata = 0;
        tick();
        soc->rst_n = 1;
        tick();
    }
    
    ~SocTb() { delete soc; }
    
    void tick() {
        soc->clk = 0;
        soc->eval();
        soc->clk = 1;
        soc->eval();
    }
    
    void bar_write(uint16_t addr, uint64_t data) {
        soc->bar_addr = addr;
        soc->bar_wdata = data;
        soc->bar_wen = 1;
        tick();
        soc->bar_wen = 0;
    }
    
    uint64_t bar_read(uint16_t addr) {
        soc->bar_addr = addr;
        soc->bar_wen = 0;
        tick();  // Address registered
        tick();  // Data available
        return soc->bar_rdata;
    }
    
    // Write instruction to IMEM
    void write_imem(uint32_t word_idx, uint32_t instr) {
        bar_write(0x1000 + word_idx * 4, instr);
    }
    
    // Write data to DMEM
    void write_dmem(uint32_t word_idx, uint32_t data) {
        bar_write(0x2000 + word_idx * 4, data);
    }
    
    // Read data from DMEM
    uint32_t read_dmem(uint32_t word_idx) {
        return bar_read(0x2000 + word_idx * 4) & 0xFFFFFFFF;
    }
    
    // Control: reset CPU
    void reset_cpu() {
        bar_write(0x00, 0x02);  // RESET
        tick();
    }
    
    // Control: run CPU
    void run_cpu() {
        bar_write(0x00, 0x01);  // RUN
    }
    
    // Control: stop CPU
    void stop_cpu() {
        bar_write(0x00, 0x00);  // STOP
    }
    
    // Read PC
    uint32_t read_pc() {
        return bar_read(0x10) & 0xFFFFFFFF;
    }
    
    // Read result
    uint32_t read_result() {
        return bar_read(0x18) & 0xFFFFFFFF;
    }
    
    // Read status
    uint32_t read_status() {
        return bar_read(0x08) & 0xFFFFFFFF;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    SocTb tb;
    
    printf("RISC-V SoC Test\n");
    printf("===============\n\n");
    
    // Load simpler test program into IMEM
    printf("Loading program into IMEM...\n");
    
    // Program (same as cpu.sv test):
    //   ADDI x1, x0, 5      # x1 = 5
    //   ADDI x2, x0, 3      # x2 = 3
    //   ADD  x3, x1, x2     # x3 = 8
    //   SUB  x4, x1, x2     # x4 = 2
    //   SW   x3, 0(x0)      # dmem[0] = 8
    //   LW   x5, 0(x0)      # x5 = 8
    //   LW   x6, 4(x0)      # x6 = dmem[1] = 200 (pre-init)
    //   BEQ  x0, x0, 0      # loop forever
    
    tb.write_imem(0, 0x00500093);  // ADDI x1, x0, 5
    tb.write_imem(1, 0x00300113);  // ADDI x2, x0, 3
    tb.write_imem(2, 0x002081b3);  // ADD  x3, x1, x2
    tb.write_imem(3, 0x40208233);  // SUB  x4, x1, x2
    tb.write_imem(4, 0x00302023);  // SW   x3, 0(x0)
    tb.write_imem(5, 0x00002283);  // LW   x5, 0(x0)
    tb.write_imem(6, 0x00402303);  // LW   x6, 4(x0)
    tb.write_imem(7, 0x00000063);  // BEQ  x0, x0, 0 (loop)
    
    // Pre-init DMEM[1] = 200 for LW x6 test
    tb.write_dmem(1, 200);
    
    printf("Program loaded.\n\n");
    
    // Reset and run CPU
    printf("Resetting CPU...\n");
    tb.reset_cpu();
    
    printf("Starting CPU...\n");
    tb.run_cpu();
    
    // Run for several cycles with explicit check after each instruction completes
    printf("Running for 20 cycles...\n");
    for (int i = 0; i < 20; i++) {
        // Just tick, no reads
        tb.soc->clk = 0; tb.soc->eval();
        tb.soc->clk = 1; tb.soc->eval();
    }
    printf("Done.\n\n");
    
    // Read back with proper timing
    tb.soc->bar_wen = 0;
    
    tb.soc->bar_addr = 0x2000;  // DMEM[0]
    for (int i = 0; i < 3; i++) { tb.soc->clk = 0; tb.soc->eval(); tb.soc->clk = 1; tb.soc->eval(); }
    uint32_t d0 = tb.soc->bar_rdata;
    
    tb.soc->bar_addr = 0x2004;  // DMEM[1]  
    for (int i = 0; i < 3; i++) { tb.soc->clk = 0; tb.soc->eval(); tb.soc->clk = 1; tb.soc->eval(); }
    uint32_t d1 = tb.soc->bar_rdata;
    
    tb.soc->bar_addr = 0x2008;  // DMEM[2]
    for (int i = 0; i < 3; i++) { tb.soc->clk = 0; tb.soc->eval(); tb.soc->clk = 1; tb.soc->eval(); }
    uint32_t d2 = tb.soc->bar_rdata;
    
    tb.soc->bar_addr = 0x10;  // PC
    for (int i = 0; i < 3; i++) { tb.soc->clk = 0; tb.soc->eval(); tb.soc->clk = 1; tb.soc->eval(); }
    uint32_t pc = tb.soc->bar_rdata;
    
    // Stop CPU
    tb.stop_cpu();
    
    // Results with manually read values
    printf("Results:\n");
    printf("  DMEM[0] = %d (expected 8)\n", d0);
    printf("  DMEM[1] = %d (expected 200)\n", d1);
    
    printf("\nFinal PC: 0x%X (expected 0x1C)\n", pc);
    
    // Verify
    int errors = 0;
    if (d0 != 8) { printf("  ERROR: DMEM[0] wrong!\n"); errors++; }
    if (d1 != 200) { printf("  ERROR: DMEM[1] wrong!\n"); errors++; }
    if (pc != 0x1C) { printf("  ERROR: PC wrong!\n"); errors++; }
    
    printf("\n========================\n");
    if (errors == 0)
        printf("ALL TESTS PASSED!\n");
    else
        printf("FAILED: %d errors\n", errors);
    
    return errors;
}
