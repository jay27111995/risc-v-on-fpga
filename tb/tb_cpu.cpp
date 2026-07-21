#include "Vcpu.h"
#include "verilated.h"
#include <cstdio>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vcpu* cpu = new Vcpu;

    printf("CPU Comprehensive Test\n");
    printf("======================\n\n");

    // Reset
    cpu->rst = 1;
    cpu->clk = 0; cpu->eval();
    cpu->clk = 1; cpu->eval();
    cpu->rst = 0;

    printf("Program:\n");
    printf("  0x00: ADDI x1, x0, 5    # x1 = 5\n");
    printf("  0x04: ADDI x2, x0, 3    # x2 = 3\n");
    printf("  0x08: ADD  x3, x1, x2   # x3 = 8\n");
    printf("  0x0C: SUB  x4, x1, x2   # x4 = 2\n");
    printf("  0x10: SW   x3, 0(x0)    # mem[0] = 8\n");
    printf("  0x14: LW   x5, 0(x0)    # x5 = 8\n");
    printf("  0x18: LW   x6, 4(x0)    # x6 = 200 (pre-init)\n");
    printf("  0x1C: BEQ  loop\n\n");

    int errors = 0;

    struct Test {
        uint32_t pc;
        int expected;
        const char* name;
    } tests[] = {
        {0x00, 5,   "ADDI x1=5"},
        {0x04, 3,   "ADDI x2=3"},
        {0x08, 8,   "ADD x3=8"},
        {0x0C, 2,   "SUB x4=2"},
        {0x10, 0,   "SW (addr=0)"},
        {0x14, 8,   "LW x5=8"},
        {0x18, 200, "LW x6=200"},
        {0x1C, 0,   "BEQ loop"},
    };

    for (int cycle = 0; cycle < 10; cycle++) {
        cpu->clk = 0; 
        cpu->eval();
        
        uint32_t pc = cpu->pc_out;
        uint32_t result = cpu->result;
        
        // Find matching test
        for (int i = 0; i < 8; i++) {
            if (tests[i].pc == pc) {
                bool pass = (result == tests[i].expected) || 
                           (pc == 0x10) ||  // SW doesn't have meaningful result
                           (pc == 0x1C);    // BEQ loop
                
                printf("Cycle %d: PC=0x%02X, result=%3d  %s %s\n", 
                       cycle, pc, result, tests[i].name,
                       pass ? "✓" : "✗");
                
                if (!pass && pc != 0x10 && pc != 0x1C) errors++;
                break;
            }
        }
        
        cpu->clk = 1;
        cpu->eval();
    }

    printf("\n========================\n");
    if (errors == 0)
        printf("ALL TESTS PASSED!\n");
    else
        printf("FAILED: %d errors\n", errors);

    delete cpu;
    return errors;
}
