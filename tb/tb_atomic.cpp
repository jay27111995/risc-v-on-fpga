// ============================================================================
// LR.W/SC.W Testbench
// ============================================================================
//
// Tests the A extension atomic instructions (LR.W and SC.W)
//
// Test Program:
//   0x00: LUI   x1, 0x10000    # x1 = 0x10000000 (DMEM base)
//   0x04: ADDI  x1, x1, 0x400  # x1 = 0x10000400 (test address)
//   0x08: LR.W  x2, (x1)       # x2 = mem[x1], set reservation
//   0x0C: ADDI  x3, x2, 1      # x3 = x2 + 1
//   0x10: SC.W  x4, x3, (x1)   # mem[x1] = x3 if reserved, x4 = success(0)/fail(1)
//   0x14: SW    x4, 4(x1)      # Store SC result to DMEM[0x404]
//   0x18: SC.W  x5, x3, (x1)   # Second SC.W should FAIL (reservation cleared)
//   0x1C: SW    x5, 8(x1)      # Store second SC result to DMEM[0x408]
//   0x20: EBREAK               # halt
//
// Expected Results:
//   - DMEM[0x400] = 43 (stored by first SC.W)
//   - DMEM[0x404] = 0 (first SC succeeded)
//   - DMEM[0x408] = 1 (second SC failed)
//
// ============================================================================

#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

// Memory map constants
#define ADDR_CTRL     0x00000
#define ADDR_STATUS   0x00008
#define ADDR_PC       0x00010
#define ADDR_IMEM     0x20000
#define ADDR_DMEM     0x80000

class SocTestbench {
public:
    Vriscv_soc* soc;

    SocTestbench() {
        soc = new Vriscv_soc;
        soc->rst_n = 0;
        soc->clk = 0;
        soc->wen = 0;
        soc->ren = 0;
        soc->addr = 0;
        soc->wdata = 0;
        tick();
        soc->rst_n = 1;
        tick();
    }

    ~SocTestbench() {
        delete soc;
    }

    void tick() {
        soc->clk = 0;
        soc->eval();
        soc->clk = 1;
        soc->eval();
    }

    void bar_write(uint32_t addr, uint32_t data) {
        soc->addr = addr;
        soc->wdata = data;
        soc->wen = 1;
        tick();
        soc->wen = 0;
    }

    uint32_t bar_read(uint32_t addr) {
        soc->addr = addr;
        soc->ren = 1;
        tick();
        tick();  // 1 cycle latency for reads
        soc->ren = 0;
        return soc->rdata;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    SocTestbench tb;

    printf("LR.W/SC.W Testbench\n");
    printf("===================\n\n");

    // Instruction encodings:
    // LR.W rd, (rs1):         funct5=00010, aq=0, rl=0, rs2=00000, rs1, funct3=010, rd, opcode=0101111
    // SC.W rd, rs2, (rs1):    funct5=00011, aq=0, rl=0, rs2, rs1, funct3=010, rd, opcode=0101111
    // 
    // LR.W x2, (x1):  00010 00 00000 00001 010 00010 0101111 = 0x1000A12F
    // SC.W x4, x3, (x1): 00011 00 00011 00001 010 00100 0101111 = 0x1830A22F
    // SC.W x5, x3, (x1): 00011 00 00011 00001 010 00101 0101111 = 0x1830A2AF
    
    printf("Loading test program...\n");
    
    tb.bar_write(ADDR_IMEM + 0x00, 0x10000037);  // LUI x1, 0x10000
    tb.bar_write(ADDR_IMEM + 0x04, 0x40008093);  // ADDI x1, x1, 0x400
    tb.bar_write(ADDR_IMEM + 0x08, 0x1000A12F);  // LR.W x2, (x1)
    tb.bar_write(ADDR_IMEM + 0x0C, 0x00110193);  // ADDI x3, x2, 1
    tb.bar_write(ADDR_IMEM + 0x10, 0x1830A22F);  // SC.W x4, x3, (x1)
    tb.bar_write(ADDR_IMEM + 0x14, 0x0040A223);  // SW x4, 4(x1) - store SC result
    tb.bar_write(ADDR_IMEM + 0x18, 0x1830A2AF);  // SC.W x5, x3, (x1)
    tb.bar_write(ADDR_IMEM + 0x1C, 0x0050A423);  // SW x5, 8(x1) - store SC result
    tb.bar_write(ADDR_IMEM + 0x20, 0x00100073);  // EBREAK
    
    // Pre-initialize DMEM[0x400] with test value 42
    tb.bar_write(ADDR_DMEM + 0x400, 42);
    tb.bar_write(ADDR_DMEM + 0x404, 0xDEADBEEF);  // Sentinel
    tb.bar_write(ADDR_DMEM + 0x408, 0xDEADBEEF);  // Sentinel
    printf("  Pre-initialized DMEM[0x400] = 42\n");
    
    // Start CPU
    printf("\nStarting CPU...\n");
    tb.bar_write(ADDR_CTRL, 1);
    
    // Run for enough cycles
    for (int i = 0; i < 150; i++) {
        tb.tick();
    }
    
    // Stop CPU
    tb.bar_write(ADDR_CTRL, 0);
    tb.tick();
    tb.tick();
    
    // Read results
    printf("\nResults:\n");
    
    uint32_t pc = tb.bar_read(ADDR_PC);
    printf("  PC      = 0x%X\n", pc);
    
    uint32_t dmem_val = tb.bar_read(ADDR_DMEM + 0x400);
    printf("  DMEM[0x400] = %d (expected 43)\n", dmem_val);
    
    uint32_t sc1_result = tb.bar_read(ADDR_DMEM + 0x404);
    printf("  DMEM[0x404] = %d (expected 0 = first SC success)\n", sc1_result);
    
    uint32_t sc2_result = tb.bar_read(ADDR_DMEM + 0x408);
    printf("  DMEM[0x408] = %d (expected 1 = second SC fail)\n", sc2_result);
    
    bool pass = true;
    
    if (dmem_val != 43) {
        printf("\nFAIL: SC.W store didn't work (expected 43)\n");
        pass = false;
    }
    
    if (sc1_result != 0) {
        printf("\nFAIL: First SC.W should return 0 (success)\n");
        pass = false;
    }
    
    if (sc2_result != 1) {
        printf("\nFAIL: Second SC.W should return 1 (fail)\n");
        pass = false;
    }
    
    printf("\n%s\n", pass ? "=== ALL TESTS PASSED ===" : "=== TESTS FAILED ===");
    
    return pass ? 0 : 1;
}
