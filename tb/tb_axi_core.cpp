// AXI Core + RISC-V SoC Testbench
// Tests the complete AXI-Lite wrapper with embedded RISC-V SoC

#include <cstdio>
#include <cstdlib>
#include "Vaxi_core_hw.h"
#include "verilated.h"

// Uncomment for VCD tracing:
// #define TRACE_VCD
#ifdef TRACE_VCD
#include "verilated_vcd_c.h"
#endif

class AxiTestbench {
public:
    Vaxi_core_hw* dut;
    uint64_t cycle;
#ifdef TRACE_VCD
    VerilatedVcdC* tfp;
#endif
    
    AxiTestbench() {
        dut = new Vaxi_core_hw;
        cycle = 0;
        
#ifdef TRACE_VCD
        // Enable tracing
        Verilated::traceEverOn(true);
        tfp = new VerilatedVcdC;
        dut->trace(tfp, 99);
        tfp->open("trace.vcd");
#endif
        
        // Initialize signals
        dut->clk = 0;
        dut->rst = 1;
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        dut->axi_lite_s_bready = 1;
        dut->axi_lite_s_arvalid = 0;
        dut->axi_lite_s_rready = 1;
        dut->axi_lite_s_wstrb = 0xFF;
        
        // Reset sequence
        for (int i = 0; i < 10; i++) tick();
        dut->rst = 0;
        for (int i = 0; i < 5; i++) tick();
    }
    
    ~AxiTestbench() {
#ifdef TRACE_VCD
        tfp->close();
#endif
        dut->final();
        delete dut;
    }
    
    void tick() {
        dut->clk = 0;
        dut->eval();
#ifdef TRACE_VCD
        tfp->dump(cycle * 10);
#endif
        dut->clk = 1;
        dut->eval();
#ifdef TRACE_VCD
        tfp->dump(cycle * 10 + 5);
#endif
        cycle++;
    }
    
    bool axi_write(uint32_t addr, uint64_t data, int timeout = 100) {
        dut->axi_lite_s_awaddr = addr;
        dut->axi_lite_s_wdata = data;
        dut->axi_lite_s_awvalid = 1;
        dut->axi_lite_s_wvalid = 1;
        
        int count = timeout;
        while (!(dut->axi_lite_s_awready && dut->axi_lite_s_wready) && count-- > 0) {
            tick();
        }
        if (count <= 0) return false;
        
        tick();
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        
        count = timeout;
        while (!dut->axi_lite_s_bvalid && count-- > 0) {
            tick();
        }
        if (count <= 0) return false;
        
        tick();
        for (int i = 0; i < 5; i++) tick();
        
        return true;
    }
    
    uint64_t axi_read(uint32_t addr, int timeout = 100) {
        dut->axi_lite_s_araddr = addr;
        dut->axi_lite_s_arvalid = 1;
        
        int count = timeout;
        while (!dut->axi_lite_s_arready && count-- > 0) {
            tick();
        }
        
        tick();
        dut->axi_lite_s_arvalid = 0;
        
        count = timeout;
        while (!dut->axi_lite_s_rvalid && count-- > 0) {
            tick();
        }
        
        uint64_t data = dut->axi_lite_s_rdata;
        tick();
        
        return data;
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    AxiTestbench tb;
    int errors = 0;
    
    printf("AXI Core + RISC-V SoC Testbench\n");
    printf("===============================\n\n");
    
    // Test Program: x1=5, x2=3, x3=x1+x2, store x3 to dmem[0], loop
    const uint32_t program[] = {
        0x00500093,  // ADDI x1, x0, 5
        0x00300113,  // ADDI x2, x0, 3
        0x002081b3,  // ADD  x3, x1, x2
        0x00302023,  // SW   x3, 0(x0)
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int program_size = sizeof(program) / sizeof(program[0]);
    
    // Clear bus sniffer BEFORE loading program
    printf("Clearing bus sniffer...\n");
    tb.axi_write(0x4008, 0x03);  // Clear + enable sniffer
    for (int i = 0; i < 5; i++) tb.tick();
    
    // Load Program to IMEM
    printf("Loading program to IMEM...\n");
    for (int i = 0; i < program_size; i += 2) {
        uint32_t even = program[i];
        uint32_t odd = (i + 1 < program_size) ? program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        if (!tb.axi_write(0x1000 + i * 4, pair)) {
            printf("  ERROR: Write to IMEM timed out!\n");
            errors++;
        }
    }
    printf("  Loaded %d instructions\n\n", program_size);
    
    // Stop bus sniffer - we've captured the IMEM writes
    tb.axi_write(0x4008, 0x00);  // Disable sniffer
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Verify IMEM
    printf("Verifying IMEM...\n");
    for (int i = 0; i < program_size; i++) {
        uint64_t readback = tb.axi_read(0x1000 + (i & ~1) * 4);
        uint32_t word = (i & 1) ? (uint32_t)(readback >> 32) : (uint32_t)readback;
        bool match = (word == program[i]);
        printf("  IMEM[%d] = 0x%08X %s\n", i, word, match ? "OK" : "MISMATCH");
        if (!match) errors++;
    }
    printf("\n");
    
    // Reset and Run CPU
    printf("Resetting CPU...\n");
    tb.axi_write(0x00, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Clear CPU logger and run CPU for FEWER cycles to see the DSTORE
    printf("Clearing CPU logger...\n");
    tb.axi_write(0x5008, 0x03);  // Clear + enable CPU logger
    for (int i = 0; i < 2; i++) tb.tick();
    
    printf("Starting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN
    
    printf("Running for 100 cycles...\n");
    for (int i = 0; i < 100; i++) tb.tick();
    
    // Stop logging immediately
    tb.axi_write(0x5008, 0x00);  // Disable CPU logger
    
    tb.axi_write(0x00, 0x00);  // STOP
    printf("\n");
    
    // Read Results
    printf("Results:\n");
    uint32_t status = (uint32_t)tb.axi_read(0x08);
    uint32_t pc = (uint32_t)tb.axi_read(0x10);
    uint64_t dmem_pair = tb.axi_read(0x2000);
    uint32_t dmem0 = (uint32_t)dmem_pair;
    
    printf("  STATUS  = 0x%X\n", status);
    printf("  PC      = 0x%X\n", pc);
    printf("  DMEM[0] = %u (expected 8)\n", dmem0);
    
    if (dmem0 != 8) {
        printf("  ERROR: DMEM[0] incorrect!\n");
        errors++;
    }
    
    // =========================================================================
    // Read Performance Counters
    // =========================================================================
    printf("\n=== Performance Counters ===\n");
    uint32_t cycles   = (uint32_t)tb.axi_read(0x20);
    uint32_t instrs   = (uint32_t)tb.axi_read(0x24);
    uint32_t stalls   = (uint32_t)tb.axi_read(0x28);
    uint32_t branches = (uint32_t)tb.axi_read(0x2C);
    uint32_t br_taken = (uint32_t)tb.axi_read(0x30);
    uint32_t loads    = (uint32_t)tb.axi_read(0x34);
    uint32_t stores   = (uint32_t)tb.axi_read(0x38);
    
    printf("  Cycles:         %u\n", cycles);
    printf("  Instructions:   %u\n", instrs);
    printf("  Stalls:         %u\n", stalls);
    printf("  Branches:       %u\n", branches);
    printf("  Branches taken: %u\n", br_taken);
    printf("  Loads:          %u\n", loads);
    printf("  Stores:         %u\n", stores);
    
    if (instrs > 0) {
        float cpi = (float)cycles / instrs;
        float ipc = (float)instrs / cycles;
        printf("\n  CPI: %.2f (cycles per instruction)\n", cpi);
        printf("  IPC: %.2f (instructions per cycle)\n", ipc);
        if (cycles > 0)
            printf("  Stall rate: %.1f%%\n", 100.0f * stalls / cycles);
    }
    
    printf("\n");
    
    // =========================================================================
    // Read Bus Sniffer Logs (0x4xxx)
    // =========================================================================
    printf("=== Bus Sniffer Log (host transactions) ===\n");
    uint32_t sniff_count = (uint32_t)tb.axi_read(0x4000);
    uint32_t sniff_cycle = (uint32_t)tb.axi_read(0x4004);
    printf("  Total transactions: %u, Current cycle: %u\n", sniff_count, sniff_cycle);
    
    int sniff_entries = (sniff_count < 8) ? sniff_count : 8;  // Show up to 8
    for (int i = sniff_entries - 1; i >= 0; i--) {  // Show oldest first
        uint32_t base = 0x4010 + i * 0x10;
        uint32_t w0 = (uint32_t)tb.axi_read(base + 0x00);  // [31:0]: type at bit 0
        uint32_t w1 = (uint32_t)tb.axi_read(base + 0x04);  // [63:32]: addr[15:0]<<16 | timestamp[15:0]
        (void)tb.axi_read(base + 0x08);                    // [95:64]: padding (unused)
        uint32_t w3 = (uint32_t)tb.axi_read(base + 0x0C);  // [127:96]: data
        
        // Parse entry
        uint32_t type = w0 & 1;
        uint32_t timestamp = w1 & 0xFFFF;
        uint32_t addr = w1 >> 16;
        uint32_t data = w3;  // Data is in the high word
        
        printf("  [%d] cycle=%5u %s addr=0x%04X data=0x%08X\n",
               i, timestamp, type ? "WR" : "RD", addr, data);
    }
    
    printf("\n");
    
    // =========================================================================
    // Read CPU Logger Logs (0x5xxx)
    // =========================================================================
    printf("=== CPU Logger (CPU memory accesses) ===\n");
    uint32_t cpu_count = (uint32_t)tb.axi_read(0x5000);
    uint32_t cpu_cycle = (uint32_t)tb.axi_read(0x5004);
    printf("  Total accesses: %u, Current cycle: %u\n", cpu_count, cpu_cycle);
    
    int cpu_entries = (cpu_count < 32) ? cpu_count : 32;  // Show up to 32
    const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
    for (int i = cpu_entries - 1; i >= 0; i--) {  // Show oldest first
        uint32_t base = 0x5010 + i * 0x10;
        uint32_t w0 = (uint32_t)tb.axi_read(base + 0x00);  // entry[31:0]
        uint32_t w1 = (uint32_t)tb.axi_read(base + 0x04);  // entry[63:32]
        uint32_t w2 = (uint32_t)tb.axi_read(base + 0x08);  // entry[95:64]
        
        // Parse entry: [95:64]=data, [63:32]=addr, [31:16]=timestamp, [1:0]=type
        uint32_t type = w0 & 3;
        uint32_t timestamp = (w0 >> 16) & 0xFFFF;
        uint32_t addr = w1;
        uint32_t data = w2;
        
        printf("  [%2d] cycle=%4u %s addr=0x%08X data=0x%08X\n",
               i, timestamp, type_names[type], addr, data);
    }
    
    printf("\n");
    
    // =========================================================================
    // Test 2: BNE instruction
    // =========================================================================
    printf("=== Test 2: BNE Instruction ===\n");
    
    // Stop CPU and reset
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Program: count down from 5 to 0 using BNE
    // x1 = 5
    // loop: x1 = x1 - 1
    //       if (x1 != 0) goto loop
    //       store x1 to dmem[4]
    //       infinite loop
    const uint32_t bne_program[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFF08093,  // ADDI x1, x1, -1     ; x1 = x1 - 1
        0xFE1010E3,  // BNE  x1, x0, -4     ; if (x1 != 0) goto -4 (back to ADDI)
        0x00102223,  // SW   x1, 4(x0)      ; store x1 to dmem[4] (should be 0)
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    const int bne_program_size = sizeof(bne_program) / sizeof(bne_program[0]);
    
    // Load Program to IMEM
    printf("Loading BNE test program...\n");
    for (int i = 0; i < bne_program_size; i += 2) {
        uint32_t even = bne_program[i];
        uint32_t odd = (i + 1 < bne_program_size) ? bne_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    // Start CPU
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 200; i++) tb.tick();  // Run for 200 cycles
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Check result - dmem[4] should be 0 (counted down from 5)
    uint32_t bne_result = (uint32_t)tb.axi_read(0x2004);
    printf("  DMEM[4] = %u (expected 0)\n", bne_result);
    if (bne_result != 0) {
        printf("  ERROR: BNE test failed!\n");
        errors++;
    } else {
        printf("  BNE test PASSED!\n");
    }
    
    // =========================================================================
    // Test 3: BLT instruction (branch if less than, signed)
    // =========================================================================
    printf("\n=== Test 3: BLT Instruction (signed) ===\n");
    
    // Stop CPU and reset
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Program: Test signed comparison
    // x1 = -5 (0xFFFFFFFB), x2 = 3
    // if (x1 < x2) store 1 to dmem[8], else store 0
    // -5 < 3 is true (signed), so should store 1
    const uint32_t blt_program[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x0020C463,  // BLT  x1, x2, 8      ; if (x1 < x2) skip next
        0x00000193,  // ADDI x3, x0, 0      ; x3 = 0 (not taken path)
        0x0000006F,  // JAL  x0, 0          ; jump to end (we don't have JAL yet, use BEQ)
        // Actually let's restructure to not need JAL
    };
    // Simpler version:
    // if x1 < x2 (signed), x3 = 1, else x3 = 2
    // store x3 to dmem[8]
    const uint32_t blt_program2[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020C463,  // BLT  x1, x2, 8      ; if (x1 < x2) skip next instr
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302423,  // SW   x3, 8(x0)      ; store x3 to dmem[8]
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    const int blt_program_size = sizeof(blt_program2) / sizeof(blt_program2[0]);
    
    printf("Loading BLT test program...\n");
    for (int i = 0; i < blt_program_size; i += 2) {
        uint32_t even = blt_program2[i];
        uint32_t odd = (i + 1 < blt_program_size) ? blt_program2[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();
    
    // -5 < 3 is true (signed), so branch taken, x3 stays 1
    uint32_t blt_result = (uint32_t)tb.axi_read(0x2008);
    printf("  DMEM[8] = %u (expected 1, BLT taken)\n", blt_result);
    if (blt_result != 1) {
        printf("  ERROR: BLT test failed!\n");
        errors++;
    } else {
        printf("  BLT test PASSED!\n");
    }
    
    // =========================================================================
    // Test 4: BLTU instruction (branch if less than, unsigned)
    // =========================================================================
    printf("\n=== Test 4: BLTU Instruction (unsigned) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Same values: x1 = -5 (0xFFFFFFFB), x2 = 3
    // Unsigned: 0xFFFFFFFB > 3, so BLTU should NOT be taken
    const uint32_t bltu_program[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5 (0xFFFFFFFB)
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020E463,  // BLTU x1, x2, 8      ; if (x1 < x2 unsigned) skip next
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302623,  // SW   x3, 12(x0)     ; store x3 to dmem[12]
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    const int bltu_program_size = sizeof(bltu_program) / sizeof(bltu_program[0]);
    
    printf("Loading BLTU test program...\n");
    for (int i = 0; i < bltu_program_size; i += 2) {
        uint32_t even = bltu_program[i];
        uint32_t odd = (i + 1 < bltu_program_size) ? bltu_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();
    
    // 0xFFFFFFFB < 3 is false (unsigned), so branch NOT taken, x3 = 2
    uint32_t bltu_result = (uint32_t)tb.axi_read(0x200C);
    printf("  DMEM[12] = %u (expected 2, BLTU not taken)\n", bltu_result);
    if (bltu_result != 2) {
        printf("  ERROR: BLTU test failed!\n");
        errors++;
    } else {
        printf("  BLTU test PASSED!\n");
    }
    
    // =========================================================================
    // Test 5: BGE instruction (branch if greater or equal, signed)
    // =========================================================================
    printf("\n=== Test 5: BGE Instruction (signed) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = 5, x2 = -3
    // 5 >= -3 is true (signed), so BGE should be taken
    const uint32_t bge_program[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFD00113,  // ADDI x2, x0, -3     ; x2 = -3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020D463,  // BGE  x1, x2, 8      ; if (x1 >= x2 signed) skip next
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302823,  // SW   x3, 16(x0)     ; store x3 to dmem[16]
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    const int bge_program_size = sizeof(bge_program) / sizeof(bge_program[0]);
    
    printf("Loading BGE test program...\n");
    for (int i = 0; i < bge_program_size; i += 2) {
        uint32_t even = bge_program[i];
        uint32_t odd = (i + 1 < bge_program_size) ? bge_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();
    
    // 5 >= -3 is true (signed), so branch taken, x3 = 1
    uint32_t bge_result = (uint32_t)tb.axi_read(0x2010);
    printf("  DMEM[16] = %u (expected 1, BGE taken)\n", bge_result);
    if (bge_result != 1) {
        printf("  ERROR: BGE test failed!\n");
        errors++;
    } else {
        printf("  BGE test PASSED!\n");
    }
    
    // =========================================================================
    // Test 6: BGEU instruction (branch if greater or equal, unsigned)
    // =========================================================================
    printf("\n=== Test 6: BGEU Instruction (unsigned) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = 5, x2 = -3 (0xFFFFFFFD)
    // 5 >= 0xFFFFFFFD is false (unsigned), so BGEU should NOT be taken
    const uint32_t bgeu_program[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFD00113,  // ADDI x2, x0, -3     ; x2 = -3 (0xFFFFFFFD)
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020F463,  // BGEU x1, x2, 8      ; if (x1 >= x2 unsigned) skip next
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302A23,  // SW   x3, 20(x0)     ; store x3 to dmem[20]
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    const int bgeu_program_size = sizeof(bgeu_program) / sizeof(bgeu_program[0]);
    
    printf("Loading BGEU test program...\n");
    for (int i = 0; i < bgeu_program_size; i += 2) {
        uint32_t even = bgeu_program[i];
        uint32_t odd = (i + 1 < bgeu_program_size) ? bgeu_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();
    
    // 5 >= 0xFFFFFFFD is false (unsigned), so branch NOT taken, x3 = 2
    uint32_t bgeu_result = (uint32_t)tb.axi_read(0x2014);
    printf("  DMEM[20] = %u (expected 2, BGEU not taken)\n", bgeu_result);
    if (bgeu_result != 2) {
        printf("  ERROR: BGEU test failed!\n");
        errors++;
    } else {
        printf("  BGEU test PASSED!\n");
    }
    
    // =========================================================================
    // Test 7: SLL (shift left logical)
    // =========================================================================
    printf("\n=== Test 7: SLL (shift left) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = 5, x2 = 2, x3 = x1 << x2 = 5 << 2 = 20
    const uint32_t sll_program[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0x00200113,  // ADDI x2, x0, 2      ; x2 = 2
        0x002091B3,  // SLL  x3, x1, x2     ; x3 = x1 << x2 = 20
        0x00302C23,  // SW   x3, 24(x0)     ; dmem[24] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int sll_program_size = sizeof(sll_program) / sizeof(sll_program[0]);
    
    for (int i = 0; i < sll_program_size; i += 2) {
        uint32_t even = sll_program[i];
        uint32_t odd = (i + 1 < sll_program_size) ? sll_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t sll_result = (uint32_t)tb.axi_read(0x2018);  // dmem[24]
    printf("  DMEM[24] = %u (expected 20, 5 << 2)\n", sll_result);
    if (sll_result != 20) {
        printf("  ERROR: SLL test failed!\n");
        errors++;
    } else {
        printf("  SLL test PASSED!\n");
    }
    
    // =========================================================================
    // Test 8: SRL (shift right logical)
    // =========================================================================
    printf("\n=== Test 8: SRL (shift right logical) ===\n");
    
    tb.axi_write(0x0000, 0x02);
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = 80, x2 = 3, x3 = x1 >> x2 = 80 >> 3 = 10
    const uint32_t srl_program[] = {
        0x05000093,  // ADDI x1, x0, 80     ; x1 = 80
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x0020D1B3,  // SRL  x3, x1, x2     ; x3 = x1 >> x2 = 10
        0x00302E23,  // SW   x3, 28(x0)     ; dmem[28] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int srl_program_size = sizeof(srl_program) / sizeof(srl_program[0]);
    
    for (int i = 0; i < srl_program_size; i += 2) {
        uint32_t even = srl_program[i];
        uint32_t odd = (i + 1 < srl_program_size) ? srl_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t srl_result = (uint32_t)tb.axi_read(0x201C);  // dmem[28]
    printf("  DMEM[28] = %u (expected 10, 80 >> 3)\n", srl_result);
    if (srl_result != 10) {
        printf("  ERROR: SRL test failed!\n");
        errors++;
    } else {
        printf("  SRL test PASSED!\n");
    }
    
    // =========================================================================
    // Test 9: SRA (shift right arithmetic)
    // =========================================================================
    printf("\n=== Test 9: SRA (shift right arithmetic) ===\n");
    
    tb.axi_write(0x0000, 0x02);
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = -16, x2 = 2, x3 = x1 >>> x2 = -16 >> 2 = -4 (0xFFFFFFFC)
    const uint32_t sra_program[] = {
        0xFF000093,  // ADDI x1, x0, -16    ; x1 = -16 (0xFFFFFFF0)
        0x00200113,  // ADDI x2, x0, 2      ; x2 = 2
        0x4020D1B3,  // SRA  x3, x1, x2     ; x3 = x1 >>> x2 = -4
        0x02302023,  // SW   x3, 32(x0)     ; dmem[32] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int sra_program_size = sizeof(sra_program) / sizeof(sra_program[0]);
    
    for (int i = 0; i < sra_program_size; i += 2) {
        uint32_t even = sra_program[i];
        uint32_t odd = (i + 1 < sra_program_size) ? sra_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t sra_result = (uint32_t)tb.axi_read(0x2020);  // dmem[32]
    printf("  DMEM[32] = 0x%08X (expected 0xFFFFFFFC, -16 >> 2 = -4)\n", sra_result);
    if (sra_result != 0xFFFFFFFC) {
        printf("  ERROR: SRA test failed!\n");
        errors++;
    } else {
        printf("  SRA test PASSED!\n");
    }
    
    // =========================================================================
    // Test 10: SLLI (shift left immediate)
    // =========================================================================
    printf("\n=== Test 10: SLLI (shift left immediate) ===\n");
    
    tb.axi_write(0x0000, 0x02);
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = 3, x2 = x1 << 4 = 3 << 4 = 48
    const uint32_t slli_program[] = {
        0x00300093,  // ADDI x1, x0, 3      ; x1 = 3
        0x00409113,  // SLLI x2, x1, 4      ; x2 = x1 << 4 = 48
        0x02202223,  // SW   x2, 36(x0)     ; dmem[36] = x2
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int slli_program_size = sizeof(slli_program) / sizeof(slli_program[0]);
    
    for (int i = 0; i < slli_program_size; i += 2) {
        uint32_t even = slli_program[i];
        uint32_t odd = (i + 1 < slli_program_size) ? slli_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t slli_result = (uint32_t)tb.axi_read(0x2024);  // dmem[36]
    printf("  DMEM[36] = %u (expected 48, 3 << 4)\n", slli_result);
    if (slli_result != 48) {
        printf("  ERROR: SLLI test failed!\n");
        errors++;
    } else {
        printf("  SLLI test PASSED!\n");
    }
    
    // =========================================================================
    // Test 11: SLT (set less than, signed)
    // =========================================================================
    printf("\n=== Test 11: SLT (set less than, signed) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = -5, x2 = 3, x3 = (x1 < x2) ? 1 : 0 = 1 (signed: -5 < 3)
    const uint32_t slt_program[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x0020A1B3,  // SLT  x3, x1, x2     ; x3 = (x1 < x2) ? 1 : 0
        0x02302423,  // SW   x3, 40(x0)     ; dmem[40] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int slt_program_size = sizeof(slt_program) / sizeof(slt_program[0]);
    
    for (int i = 0; i < slt_program_size; i += 2) {
        uint32_t even = slt_program[i];
        uint32_t odd = (i + 1 < slt_program_size) ? slt_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t slt_result = (uint32_t)tb.axi_read(0x2028);  // dmem[40]
    printf("  DMEM[40] = %u (expected 1, -5 < 3 signed)\n", slt_result);
    if (slt_result != 1) {
        printf("  ERROR: SLT test failed!\n");
        errors++;
    } else {
        printf("  SLT test PASSED!\n");
    }
    
    // =========================================================================
    // Test 12: SLTU (set less than, unsigned)
    // =========================================================================
    printf("\n=== Test 12: SLTU (set less than, unsigned) ===\n");
    
    tb.axi_write(0x0000, 0x02);
    for (int i = 0; i < 10; i++) tb.tick();
    
    // x1 = -5 (0xFFFFFFFB), x2 = 3, x3 = (x1 < x2) ? 1 : 0 = 0 (unsigned: big > 3)
    const uint32_t sltu_program[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5 (0xFFFFFFFB)
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x0020B1B3,  // SLTU x3, x1, x2     ; x3 = (x1 < x2) ? 1 : 0
        0x02302623,  // SW   x3, 44(x0)     ; dmem[44] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    const int sltu_program_size = sizeof(sltu_program) / sizeof(sltu_program[0]);
    
    for (int i = 0; i < sltu_program_size; i += 2) {
        uint32_t even = sltu_program[i];
        uint32_t odd = (i + 1 < sltu_program_size) ? sltu_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t sltu_result = (uint32_t)tb.axi_read(0x202C);  // dmem[44]
    printf("  DMEM[44] = %u (expected 0, 0xFFFFFFFB > 3 unsigned)\n", sltu_result);
    if (sltu_result != 0) {
        printf("  ERROR: SLTU test failed!\n");
        errors++;
    } else {
        printf("  SLTU test PASSED!\n");
    }
    
    // =========================================================================
    // Test 13: JAL (jump and link)
    // =========================================================================
    printf("\n=== Test 13: JAL (jump and link) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // JAL jumps forward, saves return address to x1
    // Then we store x1 to check it has PC+4
    const uint32_t jal_program[] = {
        // 0x00: JAL x1, 8      # x1 = PC+4 = 4, jump to 0x08
        // 0x04: ADDI x2, x0, 99  # SKIPPED
        // 0x08: SW x1, 48(x0)  # store x1 (should be 4)
        // 0x0C: BEQ x0, x0, 0  # loop
        0x008000EF,  // JAL x1, 8       ; x1 = 4, jump to PC+8 = 0x08
        0x06300113,  // ADDI x2, x0, 99 ; SKIPPED
        0x02102823,  // SW x1, 48(x0)   ; dmem[48] = x1 = 4
        0x00000063,  // BEQ x0, x0, 0
    };
    const int jal_program_size = sizeof(jal_program) / sizeof(jal_program[0]);
    
    for (int i = 0; i < jal_program_size; i += 2) {
        uint32_t even = jal_program[i];
        uint32_t odd = (i + 1 < jal_program_size) ? jal_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t jal_result = (uint32_t)tb.axi_read(0x2030);  // dmem[48]
    printf("  DMEM[48] = %u (expected 4, return address from JAL)\n", jal_result);
    if (jal_result != 4) {
        printf("  ERROR: JAL test failed!\n");
        errors++;
    } else {
        printf("  JAL test PASSED!\n");
    }
    
    // =========================================================================
    // Test 14: JALR (jump and link register)
    // =========================================================================
    printf("\n=== Test 14: JALR (jump and link register) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Simple function call and return
    // main: call add_five, store result
    // add_five: add 5 to x10, return
    const uint32_t jalr_program[] = {
        // 0x00: ADDI x10, x0, 3   # x10 = 3
        // 0x04: JAL x1, 12        # call add_five at 0x10, x1 = 8
        // 0x08: SW x10, 52(x0)    # store result (should be 8)
        // 0x0C: BEQ x0, x0, 0     # loop
        // 0x10: ADDI x10, x10, 5  # add_five: x10 += 5
        // 0x14: JALR x0, x1, 0    # return
        0x00300513,  // ADDI x10, x0, 3
        0x00C000EF,  // JAL x1, 12       ; call 0x10, x1 = 8
        0x02A02A23,  // SW x10, 52(x0)   ; dmem[52] = x10
        0x00000063,  // BEQ x0, x0, 0    ; loop forever
        0x00550513,  // ADDI x10, x10, 5 ; x10 = x10 + 5 = 8
        0x00008067,  // JALR x0, x1, 0   ; return to x1 (0x08)
    };
    const int jalr_program_size = sizeof(jalr_program) / sizeof(jalr_program[0]);
    
    for (int i = 0; i < jalr_program_size; i += 2) {
        uint32_t even = jalr_program[i];
        uint32_t odd = (i + 1 < jalr_program_size) ? jalr_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t jalr_result = (uint32_t)tb.axi_read(0x2034);  // dmem[52]
    printf("  DMEM[52] = %u (expected 8, 3 + 5 after function call)\n", jalr_result);
    if (jalr_result != 8) {
        printf("  ERROR: JALR test failed!\n");
        errors++;
    } else {
        printf("  JALR test PASSED!\n");
    }
    
    // =========================================================================
    // Test 15: LUI (load upper immediate)
    // =========================================================================
    printf("\n=== Test 15: LUI (load upper immediate) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // LUI x1, 0x12345 -> x1 = 0x12345000
    const uint32_t lui_program[] = {
        0x123450B7,  // LUI x1, 0x12345   ; x1 = 0x12345000
        0x02102C23,  // SW x1, 56(x0)     ; dmem[56] = x1
        0x00000063,  // BEQ x0, x0, 0
    };
    const int lui_program_size = sizeof(lui_program) / sizeof(lui_program[0]);
    
    for (int i = 0; i < lui_program_size; i += 2) {
        uint32_t even = lui_program[i];
        uint32_t odd = (i + 1 < lui_program_size) ? lui_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t lui_result = (uint32_t)tb.axi_read(0x2038);  // dmem[56]
    printf("  DMEM[56] = 0x%08X (expected 0x12345000)\n", lui_result);
    if (lui_result != 0x12345000) {
        printf("  ERROR: LUI test failed!\n");
        errors++;
    } else {
        printf("  LUI test PASSED!\n");
    }
    
    // =========================================================================
    // Test 16: AUIPC (add upper immediate to PC)
    // =========================================================================
    printf("\n=== Test 16: AUIPC (add upper immediate to PC) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // At PC=0: AUIPC x1, 1 -> x1 = 0 + 0x1000 = 0x1000
    const uint32_t auipc_program[] = {
        0x00001097,  // AUIPC x1, 1       ; x1 = PC + 0x1000 = 0 + 0x1000 = 0x1000
        0x02102E23,  // SW x1, 60(x0)     ; dmem[60] = x1
        0x00000063,  // BEQ x0, x0, 0
    };
    const int auipc_program_size = sizeof(auipc_program) / sizeof(auipc_program[0]);
    
    for (int i = 0; i < auipc_program_size; i += 2) {
        uint32_t even = auipc_program[i];
        uint32_t odd = (i + 1 < auipc_program_size) ? auipc_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t auipc_result = (uint32_t)tb.axi_read(0x203C);  // dmem[60]
    printf("  DMEM[60] = 0x%08X (expected 0x1000, PC=0 + 0x1000)\n", auipc_result);
    if (auipc_result != 0x1000) {
        printf("  ERROR: AUIPC test failed!\n");
        errors++;
    } else {
        printf("  AUIPC test PASSED!\n");
    }
    
    // =========================================================================
    // Test 17: SB/LB (store byte, load byte signed)
    // =========================================================================
    printf("\n=== Test 17: SB/LB (store/load byte) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Store 0xFF to byte 0, then load it back (should sign extend to -1)
    const uint32_t sb_lb_program[] = {
        0x0FF00093,  // ADDI x1, x0, 0xFF   ; x1 = 255 (0x000000FF)
        0x04100113,  // ADDI x2, x0, 65     ; x2 = 65 (base address)
        0x00110023,  // SB x1, 0(x2)        ; mem[65] = 0xFF (byte)
        0x00010183,  // LB x3, 0(x2)        ; x3 = sign_ext(0xFF) = -1
        0x00014203,  // LBU x4, 0(x2)       ; x4 = zero_ext(0xFF) = 255  (funct3=100)
        0x04302023,  // SW x3, 64(x0)       ; dmem[64] = x3
        0x04402223,  // SW x4, 68(x0)       ; dmem[68] = x4
        0x00000063,  // BEQ x0, x0, 0
    };
    const int sb_lb_program_size = sizeof(sb_lb_program) / sizeof(sb_lb_program[0]);
    
    for (int i = 0; i < sb_lb_program_size; i += 2) {
        uint32_t even = sb_lb_program[i];
        uint32_t odd = (i + 1 < sb_lb_program_size) ? sb_lb_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t lb_result = (uint32_t)tb.axi_read(0x2040);   // dmem[64]
    uint32_t lbu_result = (uint32_t)tb.axi_read(0x2044);  // dmem[68]
    printf("  LB result:  0x%08X (expected 0xFFFFFFFF, sign extended -1)\n", lb_result);
    printf("  LBU result: 0x%08X (expected 0x000000FF, zero extended 255)\n", lbu_result);
    if (lb_result != 0xFFFFFFFF || lbu_result != 0x000000FF) {
        printf("  ERROR: SB/LB test failed!\n");
        errors++;
    } else {
        printf("  SB/LB test PASSED!\n");
    }
    
    // =========================================================================
    // Test 18: SH/LH (store halfword, load halfword signed)
    // =========================================================================
    printf("\n=== Test 18: SH/LH (store/load halfword) ===\n");
    
    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();
    
    // Store 0x8765 to halfword, then load it back
    const uint32_t sh_lh_program[] = {
        0x87600093,  // LUI x1, 0x87600  -- wait, we need a different approach
        // Let's build 0xFFFF8765 = -30875
    };
    // Simpler: use ADDI with negative value
    const uint32_t sh_lh_program2[] = {
        0x80000137,  // LUI x2, 0x80000    ; x2 = 0x80000000 (just for base addr)
        0x04800113,  // ADDI x2, x0, 72    ; x2 = 72 (base address)
        0xFFF00093,  // ADDI x1, x0, -1    ; x1 = 0xFFFFFFFF
        0x00111023,  // SH x1, 0(x2)       ; mem[72] = 0xFFFF (halfword)
        0x00011183,  // LH x3, 0(x2)       ; x3 = sign_ext(0xFFFF) = -1
        0x00015203,  // LHU x4, 0(x2)      ; x4 = zero_ext(0xFFFF) = 65535
        0x04302423,  // SW x3, 72(x0)      ; dmem[72] = x3 (overwrite)
        0x04402623,  // SW x4, 76(x0)      ; dmem[76] = x4
        0x00000063,  // BEQ x0, x0, 0
    };
    const int sh_lh_program_size = sizeof(sh_lh_program2) / sizeof(sh_lh_program2[0]);
    
    for (int i = 0; i < sh_lh_program_size; i += 2) {
        uint32_t even = sh_lh_program2[i];
        uint32_t odd = (i + 1 < sh_lh_program_size) ? sh_lh_program2[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x1000 + i * 4, pair);
    }
    
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();
    
    uint32_t lh_result = (uint32_t)tb.axi_read(0x2048);   // dmem[72]
    uint32_t lhu_result = (uint32_t)tb.axi_read(0x204C);  // dmem[76]
    printf("  LH result:  0x%08X (expected 0xFFFFFFFF, sign extended -1)\n", lh_result);
    printf("  LHU result: 0x%08X (expected 0x0000FFFF, zero extended 65535)\n", lhu_result);
    if (lh_result != 0xFFFFFFFF || lhu_result != 0x0000FFFF) {
        printf("  ERROR: SH/LH test failed!\n");
        errors++;
    } else {
        printf("  SH/LH test PASSED!\n");
    }
    
    printf("\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    
    return errors;
}
