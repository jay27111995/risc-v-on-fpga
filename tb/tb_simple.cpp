#include "Vcpu.h"
#include "verilated.h"
#include <cstdio>

int main() {
    Vcpu* cpu = new Vcpu;
    
    cpu->rst = 1;
    cpu->clk = 0; cpu->eval();
    cpu->clk = 1; cpu->eval();
    cpu->rst = 0;
    
    printf("Testing standalone CPU (from src/cpu.sv)\n\n");
    
    for (int i = 0; i < 15; i++) {
        cpu->clk = 0; cpu->eval();
        printf("Cycle %2d: PC=0x%02X, result=%d\n", i, cpu->pc_out, cpu->result);
        cpu->clk = 1; cpu->eval();
    }
    
    delete cpu;
    return 0;
}
