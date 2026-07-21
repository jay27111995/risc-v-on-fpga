#include "Vdmem.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <cstdio>

void tick(Vdmem* dmem, VerilatedVcdC* tfp, int& t) {
    dmem->clk = 0;
    dmem->eval();
    tfp->dump(t++);
    dmem->clk = 1;
    dmem->eval();
    tfp->dump(t++);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    Vdmem* dmem = new Vdmem;
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dmem->trace(tfp, 99);
    tfp->open("dmem_wave.vcd");

    int t = 0;

    printf("Data Memory Test\n\n");

    // Initialize
    dmem->clk = 0;
    dmem->we = 0;
    dmem->re = 0;
    dmem->addr = 0;
    dmem->wdata = 0;
    dmem->eval();
    tfp->dump(t++);

    // Read pre-initialized values
    printf("Read test (pre-initialized):\n");
    
    dmem->re = 1;
    dmem->addr = 0x00;  // word 0
    dmem->eval();
    tfp->dump(t++);
    printf("  addr=0x%02X: rdata=%d (expect 100)\n", dmem->addr, dmem->rdata);

    dmem->addr = 0x04;  // word 1
    dmem->eval();
    tfp->dump(t++);
    printf("  addr=0x%02X: rdata=%d (expect 200)\n", dmem->addr, dmem->rdata);

    dmem->addr = 0x08;  // word 2
    dmem->eval();
    tfp->dump(t++);
    printf("  addr=0x%02X: rdata=%d (expect 300)\n", dmem->addr, dmem->rdata);

    // Write test
    printf("\nWrite test:\n");
    
    dmem->re = 0;
    dmem->we = 1;
    dmem->addr = 0x10;  // word 4
    dmem->wdata = 42;
    printf("  Writing 42 to addr=0x%02X\n", dmem->addr);
    tick(dmem, tfp, t);  // clock edge - write happens

    dmem->we = 0;
    dmem->re = 1;
    dmem->eval();
    tfp->dump(t++);
    printf("  Read back: rdata=%d (expect 42)\n", dmem->rdata);

    // Write another value
    dmem->re = 0;
    dmem->we = 1;
    dmem->addr = 0x14;  // word 5
    dmem->wdata = 999;
    printf("  Writing 999 to addr=0x%02X\n", dmem->addr);
    tick(dmem, tfp, t);

    dmem->we = 0;
    dmem->re = 1;
    dmem->eval();
    tfp->dump(t++);
    printf("  Read back: rdata=%d (expect 999)\n", dmem->rdata);

    tfp->close();
    delete tfp;
    delete dmem;

    printf("\nWaveform saved to dmem_wave.vcd\n");
    return 0;
}
