// ============================================================================
// AXI Core + RISC-V SoC Testbench
// ============================================================================
//
// Verilator simulation testbench for the complete RV32I SoC with AXI-Lite
// interface. Tests all 37 base instructions plus pipeline hazards.
//
// BAR Memory Map (matches FPGA hardware):
//   0x00000 - 0x000FF : Control registers (CTRL, STATUS, PC, CYCLES, etc.)
//   0x20000 - 0x3FFFF : IMEM - 128KB instruction memory
//   0x80000 - 0x87FFF : DMEM - 32KB data memory
//
// Control Registers:
//   0x00 CTRL   - [0] RUN, [1] RESET
//   0x08 STATUS - [0] RUNNING
//   0x10 PC     - Current program counter
//   0x20 CYCLES - Cycle count (write to clear all perf counters)
//
// Test Coverage (30 tests):
//   1-6:   Branch instructions (BEQ, BNE, BLT, BLTU, BGE, BGEU)
//   7-10:  Shift operations (SLL, SRL, SRA, SLLI)
//   11-12: Set-less-than (SLT, SLTU)
//   13-14: Jump instructions (JAL, JALR)
//   15-16: Upper immediate (LUI, AUIPC)
//   17-18: Byte/halfword load/store (SB/LB/LBU, SH/LH/LHU)
//   19-21: ALU operations (SUB, AND/OR/XOR, ANDI/ORI/XORI)
//   22-23: Immediate shifts and compares (SRLI/SRAI, SLTI/SLTIU)
//   24:    Load word (LW)
//   25-30: Pipeline hazards (forwarding, load-use, branches, loops)
//
// Build:
//   verilator --cc --top-module axi_core_hw -I../rtl ../rtl/*.sv \
//             --exe tb_axi_core.cpp -CFLAGS "-std=c++17" -Wno-CASEINCOMPLETE
//   make -C obj_dir -f Vaxi_core_hw.mk
//
// Run:
//   ./obj_dir/Vaxi_core_hw
//
// ============================================================================

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
    tb.axi_write(0x40008, 0x03);  // Clear + enable sniffer
    for (int i = 0; i < 5; i++) tb.tick();

    // Load Program to IMEM
    printf("Loading program to IMEM...\n");
    for (int i = 0; i < program_size; i += 2) {
        uint32_t even = program[i];
        uint32_t odd = (i + 1 < program_size) ? program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        if (!tb.axi_write(0x20000 + i * 4, pair)) {
            printf("  ERROR: Write to IMEM timed out!\n");
            errors++;
        }
    }
    printf("  Loaded %d instructions\n\n", program_size);

    // Stop bus sniffer - we've captured the IMEM writes
    tb.axi_write(0x40008, 0x00);  // Disable sniffer
    for (int i = 0; i < 10; i++) tb.tick();

    // Verify IMEM
    printf("Verifying IMEM...\n");
    for (int i = 0; i < program_size; i++) {
        uint64_t readback = tb.axi_read(0x20000 + (i & ~1) * 4);
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
    tb.axi_write(0x50008, 0x03);  // Clear + enable CPU logger
    for (int i = 0; i < 2; i++) tb.tick();

    printf("Starting CPU...\n");
    tb.axi_write(0x00, 0x01);  // RUN

    printf("Running for 100 cycles...\n");
    for (int i = 0; i < 100; i++) tb.tick();

    // Stop logging immediately
    tb.axi_write(0x50008, 0x00);  // Disable CPU logger

    tb.axi_write(0x00, 0x00);  // STOP
    printf("\n");

    // Read Results
    printf("Results:\n");
    uint32_t status = (uint32_t)tb.axi_read(0x08);
    uint32_t pc = (uint32_t)tb.axi_read(0x10);
    uint64_t dmem_pair = tb.axi_read(0x80000);
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
    uint32_t sniff_count = (uint32_t)tb.axi_read(0x40000);
    uint32_t sniff_cycle = (uint32_t)tb.axi_read(0x40004);
    printf("  Total transactions: %u, Current cycle: %u\n", sniff_count, sniff_cycle);

    int sniff_entries = (sniff_count < 8) ? sniff_count : 8;  // Show up to 8
    for (int i = sniff_entries - 1; i >= 0; i--) {  // Show oldest first
        uint32_t base = 0x40010 + i * 0x10;
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
    uint32_t cpu_count = (uint32_t)tb.axi_read(0x50000);
    uint32_t cpu_cycle = (uint32_t)tb.axi_read(0x50004);
    printf("  Total accesses: %u, Current cycle: %u\n", cpu_count, cpu_cycle);

    int cpu_entries = (cpu_count < 32) ? cpu_count : 32;  // Show up to 32
    const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
    for (int i = cpu_entries - 1; i >= 0; i--) {  // Show oldest first
        uint32_t base = 0x50010 + i * 0x10;
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    // Start CPU
    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 200; i++) tb.tick();  // Run for 200 cycles
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();

    // Check result - dmem[4] should be 0 (counted down from 5)
    uint32_t bne_result = (uint32_t)tb.axi_read(0x80004);
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();

    // -5 < 3 is true (signed), so branch taken, x3 stays 1
    uint32_t blt_result = (uint32_t)tb.axi_read(0x80008);
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();

    // 0xFFFFFFFB < 3 is false (unsigned), so branch NOT taken, x3 = 2
    uint32_t bltu_result = (uint32_t)tb.axi_read(0x8000C);
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();

    // 5 >= -3 is true (signed), so branch taken, x3 = 1
    uint32_t bge_result = (uint32_t)tb.axi_read(0x80010);
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);  // RUN
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);  // STOP
    for (int i = 0; i < 10; i++) tb.tick();

    // 5 >= 0xFFFFFFFD is false (unsigned), so branch NOT taken, x3 = 2
    uint32_t bgeu_result = (uint32_t)tb.axi_read(0x80014);
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t sll_result = (uint32_t)tb.axi_read(0x80018);  // dmem[24]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t srl_result = (uint32_t)tb.axi_read(0x8001C);  // dmem[28]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t sra_result = (uint32_t)tb.axi_read(0x80020);  // dmem[32]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t slli_result = (uint32_t)tb.axi_read(0x80024);  // dmem[36]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t slt_result = (uint32_t)tb.axi_read(0x80028);  // dmem[40]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t sltu_result = (uint32_t)tb.axi_read(0x8002C);  // dmem[44]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t jal_result = (uint32_t)tb.axi_read(0x80030);  // dmem[48]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t jalr_result = (uint32_t)tb.axi_read(0x80034);  // dmem[52]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t lui_result = (uint32_t)tb.axi_read(0x80038);  // dmem[56]
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

    // At PC=0: AUIPC x1, 1 -> x1 = 0 + 0x20000 = 0x20000
    const uint32_t auipc_program[] = {
        0x00001097,  // AUIPC x1, 1       ; x1 = PC + 0x20000 = 0 + 0x20000 = 0x20000
        0x02102E23,  // SW x1, 60(x0)     ; dmem[60] = x1
        0x00000063,  // BEQ x0, x0, 0
    };
    const int auipc_program_size = sizeof(auipc_program) / sizeof(auipc_program[0]);

    for (int i = 0; i < auipc_program_size; i += 2) {
        uint32_t even = auipc_program[i];
        uint32_t odd = (i + 1 < auipc_program_size) ? auipc_program[i + 1] : 0x00000013;
        uint64_t pair = ((uint64_t)odd << 32) | even;
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t auipc_result = (uint32_t)tb.axi_read(0x8003C);  // dmem[60]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t lb_result = (uint32_t)tb.axi_read(0x80040);   // dmem[64]
    uint32_t lbu_result = (uint32_t)tb.axi_read(0x80044);  // dmem[68]
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
        tb.axi_write(0x20000 + i * 4, pair);
    }

    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 100; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t lh_result = (uint32_t)tb.axi_read(0x80048);   // dmem[72]
    uint32_t lhu_result = (uint32_t)tb.axi_read(0x8004C);  // dmem[76]
    printf("  LH result:  0x%08X (expected 0xFFFFFFFF, sign extended -1)\n", lh_result);
    printf("  LHU result: 0x%08X (expected 0x0000FFFF, zero extended 65535)\n", lhu_result);
    if (lh_result != 0xFFFFFFFF || lhu_result != 0x0000FFFF) {
        printf("  ERROR: SH/LH test failed!\n");
        errors++;
    } else {
        printf("  SH/LH test PASSED!\n");
    }

    // =========================================================================
    // Test 19: SUB instruction
    // =========================================================================
    printf("\n=== Test 19: SUB (subtract) ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t sub_program[] = {
        0x00A00093,  // ADDI x1, x0, 10    ; x1 = 10
        0x00300113,  // ADDI x2, x0, 3     ; x2 = 3
        0x402081B3,  // SUB x3, x1, x2     ; x3 = 10 - 3 = 7
        0x00302023,  // SW x3, 0(x0)       ; dmem[0] = 7
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 5; i += 2) {
        uint32_t even = sub_program[i];
        uint32_t odd = (i + 1 < 5) ? sub_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t sub_result = (uint32_t)tb.axi_read(0x80000);
    printf("  DMEM[0] = %u (expected 7, 10 - 3)\n", sub_result);
    if (sub_result != 7) { printf("  ERROR: SUB test failed!\n"); errors++; }
    else { printf("  SUB test PASSED!\n"); }

    // =========================================================================
    // Test 20: AND, OR, XOR instructions
    // =========================================================================
    printf("\n=== Test 20: AND, OR, XOR ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t logic_program[] = {
        0x0FF00093,  // ADDI x1, x0, 0xFF  ; x1 = 0xFF
        0x0F000113,  // ADDI x2, x0, 0xF0  ; x2 = 0xF0
        0x0020F1B3,  // AND x3, x1, x2     ; x3 = 0xFF & 0xF0 = 0xF0
        0x0020E233,  // OR x4, x1, x2      ; x4 = 0xFF | 0xF0 = 0xFF
        0x0020C2B3,  // XOR x5, x1, x2     ; x5 = 0xFF ^ 0xF0 = 0x0F
        0x00302023,  // SW x3, 0(x0)       ; dmem[0] = AND result
        0x00402223,  // SW x4, 4(x0)       ; dmem[4] = OR result
        0x00502423,  // SW x5, 8(x0)       ; dmem[8] = XOR result
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 9; i += 2) {
        uint32_t even = logic_program[i];
        uint32_t odd = (i + 1 < 9) ? logic_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t and_result = (uint32_t)tb.axi_read(0x80000);
    uint32_t or_result = (uint32_t)tb.axi_read(0x80004);
    uint32_t xor_result = (uint32_t)tb.axi_read(0x80008);
    printf("  AND: 0x%02X (expected 0xF0)\n", and_result);
    printf("  OR:  0x%02X (expected 0xFF)\n", or_result);
    printf("  XOR: 0x%02X (expected 0x0F)\n", xor_result);
    if (and_result != 0xF0 || or_result != 0xFF || xor_result != 0x0F) {
        printf("  ERROR: Logic test failed!\n"); errors++;
    } else { printf("  AND/OR/XOR test PASSED!\n"); }

    // =========================================================================
    // Test 21: ANDI, ORI, XORI (immediate versions)
    // =========================================================================
    printf("\n=== Test 21: ANDI, ORI, XORI ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t logic_imm_program[] = {
        0x0FF00093,  // ADDI x1, x0, 0xFF  ; x1 = 0xFF
        0x0F00F113,  // ANDI x2, x1, 0xF0  ; x2 = 0xFF & 0xF0 = 0xF0
        0x00F0E193,  // ORI x3, x1, 0x0F   ; x3 = 0xFF | 0x0F = 0xFF
        0x0F00C213,  // XORI x4, x1, 0xF0  ; x4 = 0xFF ^ 0xF0 = 0x0F
        0x00202023,  // SW x2, 0(x0)
        0x00302223,  // SW x3, 4(x0)
        0x00402423,  // SW x4, 8(x0)
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 8; i += 2) {
        uint32_t even = logic_imm_program[i];
        uint32_t odd = (i + 1 < 8) ? logic_imm_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t andi_result = (uint32_t)tb.axi_read(0x80000);
    uint32_t ori_result = (uint32_t)tb.axi_read(0x80004);
    uint32_t xori_result = (uint32_t)tb.axi_read(0x80008);
    printf("  ANDI: 0x%02X (expected 0xF0)\n", andi_result);
    printf("  ORI:  0x%02X (expected 0xFF)\n", ori_result);
    printf("  XORI: 0x%02X (expected 0x0F)\n", xori_result);
    if (andi_result != 0xF0 || ori_result != 0xFF || xori_result != 0x0F) {
        printf("  ERROR: Logic immediate test failed!\n"); errors++;
    } else { printf("  ANDI/ORI/XORI test PASSED!\n"); }

    // =========================================================================
    // Test 22: SRLI, SRAI (immediate shifts)
    // =========================================================================
    printf("\n=== Test 22: SRLI, SRAI ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t shift_imm_program[] = {
        0x08000093,  // ADDI x1, x0, 128   ; x1 = 128 = 0x80
        0x4010D113,  // SRAI x2, x1, 1     ; x2 = 128 >> 1 = 64 (or -64 if x1 was negative)
        0x0010D193,  // SRLI x3, x1, 1     ; x3 = 128 >> 1 = 64
        0x00202023,  // SW x2, 0(x0)       ; dmem[0] = SRAI result
        0x00302223,  // SW x3, 4(x0)       ; dmem[4] = SRLI result
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 6; i += 2) {
        uint32_t even = shift_imm_program[i];
        uint32_t odd = (i + 1 < 6) ? shift_imm_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t srai_result = (uint32_t)tb.axi_read(0x80000);
    uint32_t srli_result = (uint32_t)tb.axi_read(0x80004);
    printf("  SRAI(128, 1): %u (expected 64, same as SRLI since MSB=0)\n", srai_result);
    printf("  SRLI(128, 1): %u (expected 64)\n", srli_result);
    // For positive numbers, SRAI and SRLI should give same result
    if (srai_result != 64 || srli_result != 64) {
        printf("  ERROR: Shift immediate test failed!\n"); errors++;
    } else { printf("  SRLI/SRAI test PASSED!\n"); }

    // =========================================================================
    // Test 23: SLTI, SLTIU (set less than immediate)
    // =========================================================================
    printf("\n=== Test 23: SLTI, SLTIU ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t slti_program[] = {
        0xFFB00093,  // ADDI x1, x0, -5    ; x1 = -5
        0x00A0A113,  // SLTI x2, x1, 10    ; x2 = (-5 < 10) = 1 (signed)
        0x00A0B193,  // SLTIU x3, x1, 10   ; x3 = (0xFFFFFFFB < 10) = 0 (unsigned)
        0x00202023,  // SW x2, 0(x0)
        0x00302223,  // SW x3, 4(x0)
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 6; i += 2) {
        uint32_t even = slti_program[i];
        uint32_t odd = (i + 1 < 6) ? slti_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t slti_result = (uint32_t)tb.axi_read(0x80000);
    uint32_t sltiu_result = (uint32_t)tb.axi_read(0x80004);
    printf("  SLTI:  %u (expected 1, -5 < 10 signed)\n", slti_result);
    printf("  SLTIU: %u (expected 0, 0xFFFFFFFB > 10 unsigned)\n", sltiu_result);
    if (slti_result != 1 || sltiu_result != 0) {
        printf("  ERROR: SLTI/SLTIU test failed!\n"); errors++;
    } else { printf("  SLTI/SLTIU test PASSED!\n"); }

    // =========================================================================
    // Test 24: LW (load word)
    // =========================================================================
    printf("\n=== Test 24: LW (load word) ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // First write a known value, then load it back
    const uint32_t lw_program[] = {
        0x12345137,  // LUI x2, 0x12345    ; x2 = 0x12345000
        0x67810113,  // ADDI x2, x2, 0x678 ; x2 = 0x12345678
        0x00202023,  // SW x2, 0(x0)       ; dmem[0] = 0x12345678
        0x00002183,  // LW x3, 0(x0)       ; x3 = dmem[0]
        0x00302223,  // SW x3, 4(x0)       ; dmem[4] = x3 (should be same)
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 6; i += 2) {
        uint32_t even = lw_program[i];
        uint32_t odd = (i + 1 < 6) ? lw_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t lw_result = (uint32_t)tb.axi_read(0x80004);
    printf("  LW result: 0x%08X (expected 0x12345678)\n", lw_result);
    if (lw_result != 0x12345678) {
        printf("  ERROR: LW test failed!\n"); errors++;
    } else { printf("  LW test PASSED!\n"); }

    // =========================================================================
    // Test 25: Forwarding chain (EX2 -> EX1 -> EX1)
    // =========================================================================
    printf("\n=== Test 25: Forwarding chain (data dependencies) ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // Chain of dependent instructions - tests forwarding paths
    const uint32_t fwd_program[] = {
        0x00500093,  // ADDI x1, x0, 5     ; x1 = 5
        0x00108113,  // ADDI x2, x1, 1     ; x2 = x1 + 1 = 6 (forward from EX2)
        0x00210193,  // ADDI x3, x2, 2     ; x3 = x2 + 2 = 8 (forward from EX2)
        0x00318213,  // ADDI x4, x3, 3     ; x4 = x3 + 3 = 11 (forward from EX2)
        0x004202B3,  // ADD x5, x4, x4     ; x5 = x4 + x4 = 22 (forward x4 twice)
        0x00502023,  // SW x5, 0(x0)       ; dmem[0] = 22
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 7; i += 2) {
        uint32_t even = fwd_program[i];
        uint32_t odd = (i + 1 < 7) ? fwd_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t fwd_result = (uint32_t)tb.axi_read(0x80000);
    printf("  Forwarding result: %u (expected 22)\n", fwd_result);
    if (fwd_result != 22) {
        printf("  ERROR: Forwarding test failed!\n"); errors++;
    } else { printf("  Forwarding test PASSED!\n"); }

    // =========================================================================
    // Test 26: Load-use hazard (load followed by dependent instruction)
    // =========================================================================
    printf("\n=== Test 26: Load-use hazard ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // Store value, then load and immediately use it
    const uint32_t loaduse_program[] = {
        0x00A00093,  // ADDI x1, x0, 10    ; x1 = 10
        0x00102023,  // SW x1, 0(x0)       ; dmem[0] = 10
        0x00002103,  // LW x2, 0(x0)       ; x2 = dmem[0] = 10
        0x00210113,  // ADDI x2, x2, 2     ; x2 = x2 + 2 = 12 (use right after load)
        0x00202223,  // SW x2, 4(x0)       ; dmem[4] = 12
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 6; i += 2) {
        uint32_t even = loaduse_program[i];
        uint32_t odd = (i + 1 < 6) ? loaduse_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t loaduse_result = (uint32_t)tb.axi_read(0x80004);
    printf("  Load-use result: %u (expected 12)\n", loaduse_result);
    if (loaduse_result != 12) {
        printf("  ERROR: Load-use hazard test failed!\n"); errors++;
    } else { printf("  Load-use hazard test PASSED!\n"); }

    // =========================================================================
    // Test 27: Back-to-back branches
    // =========================================================================
    printf("\n=== Test 27: Back-to-back branches ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // Multiple branches in sequence
    const uint32_t branch_seq_program[] = {
        0x00100093,  // ADDI x1, x0, 1     ; x1 = 1
        0x00200113,  // ADDI x2, x0, 2     ; x2 = 2
        0x00208463,  // BEQ x1, x2, +8     ; not taken (1 != 2)
        0x00108093,  // ADDI x1, x1, 1     ; x1 = 2
        0x00208463,  // BEQ x1, x2, +8     ; taken (2 == 2)
        0x00108093,  // ADDI x1, x1, 1     ; SKIPPED
        0x00108093,  // ADDI x1, x1, 1     ; x1 = 3
        0x00102023,  // SW x1, 0(x0)       ; dmem[0] = 3
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 9; i += 2) {
        uint32_t even = branch_seq_program[i];
        uint32_t odd = (i + 1 < 9) ? branch_seq_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t branch_result = (uint32_t)tb.axi_read(0x80000);
    printf("  Branch sequence result: %u (expected 3)\n", branch_result);
    if (branch_result != 3) {
        printf("  ERROR: Back-to-back branches test failed!\n"); errors++;
    } else { printf("  Back-to-back branches test PASSED!\n"); }

    // =========================================================================
    // Test 28: Multiple consecutive loads
    // =========================================================================
    printf("\n=== Test 28: Multiple consecutive loads ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t multi_load_program[] = {
        0x00A00093,  // ADDI x1, x0, 10    ; x1 = 10
        0x01400113,  // ADDI x2, x0, 20    ; x2 = 20
        0x01E00193,  // ADDI x3, x0, 30    ; x3 = 30
        0x00102023,  // SW x1, 0(x0)       ; dmem[0] = 10
        0x00202223,  // SW x2, 4(x0)       ; dmem[4] = 20
        0x00302423,  // SW x3, 8(x0)       ; dmem[8] = 30
        0x00002203,  // LW x4, 0(x0)       ; x4 = 10
        0x00402283,  // LW x5, 4(x0)       ; x5 = 20
        0x00802303,  // LW x6, 8(x0)       ; x6 = 30
        0x005203B3,  // ADD x7, x4, x5     ; x7 = 10 + 20 = 30
        0x00638433,  // ADD x8, x7, x6     ; x8 = 30 + 30 = 60
        0x00802623,  // SW x8, 12(x0)      ; dmem[12] = 60
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 13; i += 2) {
        uint32_t even = multi_load_program[i];
        uint32_t odd = (i + 1 < 13) ? multi_load_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t multi_load_result = (uint32_t)tb.axi_read(0x8000C);
    printf("  Multi-load result: %u (expected 60)\n", multi_load_result);
    if (multi_load_result != 60) {
        printf("  ERROR: Multiple loads test failed!\n"); errors++;
    } else { printf("  Multiple loads test PASSED!\n"); }

    // =========================================================================
    // Test 29: Store followed by load to same address
    // =========================================================================
    printf("\n=== Test 29: Store-load forwarding (same address) ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    const uint32_t store_load_program[] = {
        0x0AB00093,  // ADDI x1, x0, 0xAB  ; x1 = 171
        0x00102023,  // SW x1, 0(x0)       ; dmem[0] = 171
        0x00002103,  // LW x2, 0(x0)       ; x2 = dmem[0] (should be 171)
        0x00202223,  // SW x2, 4(x0)       ; dmem[4] = x2
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 5; i += 2) {
        uint32_t even = store_load_program[i];
        uint32_t odd = (i + 1 < 5) ? store_load_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 200; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t store_load_result = (uint32_t)tb.axi_read(0x80004);
    printf("  Store-load result: %u (expected 171)\n", store_load_result);
    if (store_load_result != 171) {
        printf("  ERROR: Store-load test failed!\n"); errors++;
    } else { printf("  Store-load test PASSED!\n"); }

    // =========================================================================
    // Test 30: Complex loop with all hazard types
    // =========================================================================
    printf("\n=== Test 30: Complex loop (sum 1 to 5) ===\n");

    tb.axi_write(0x0000, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // Sum = 1 + 2 + 3 + 4 + 5 = 15
    const uint32_t loop_program[] = {
        0x00000093,  // ADDI x1, x0, 0     ; x1 = sum = 0
        0x00100113,  // ADDI x2, x0, 1     ; x2 = i = 1
        0x00600193,  // ADDI x3, x0, 6     ; x3 = limit = 6
        // loop:
        0x002080B3,  // ADD x1, x1, x2     ; sum += i
        0x00110113,  // ADDI x2, x2, 1     ; i++
        0xFE311CE3,  // BNE x2, x3, -8     ; if (i != 6) goto loop
        0x00102023,  // SW x1, 0(x0)       ; dmem[0] = sum
        0x00000063,  // BEQ x0, x0, 0
    };
    for (int i = 0; i < 8; i += 2) {
        uint32_t even = loop_program[i];
        uint32_t odd = (i + 1 < 8) ? loop_program[i + 1] : 0x00000013;
        tb.axi_write(0x20000 + i * 4, ((uint64_t)odd << 32) | even);
    }
    tb.axi_write(0x0000, 0x01);
    for (int i = 0; i < 300; i++) tb.tick();
    tb.axi_write(0x0000, 0x00);
    for (int i = 0; i < 10; i++) tb.tick();

    uint32_t loop_result = (uint32_t)tb.axi_read(0x80000);
    printf("  Loop sum result: %u (expected 15, sum of 1..5)\n", loop_result);
    if (loop_result != 15) {
        printf("  ERROR: Loop test failed!\n"); errors++;
    } else { printf("  Loop test PASSED!\n"); }

    // -------------------------------------------------------------------------
    // Test 31: EBREAK halts CPU
    // -------------------------------------------------------------------------
    printf("\n=== Test 31: EBREAK halts CPU ===\n");

    // Reset CPU
    tb.axi_write(0x00, 0x02);  // RESET
    for (int i = 0; i < 10; i++) tb.tick();

    // Clear IMEM
    for (int i = 0; i < 16; i++) {
        tb.axi_write(0x20000 + i * 4, 0x00000013);  // NOP
    }

    // Program: store a value, then EBREAK
    tb.axi_write(0x20000 + 0,  0x00A00093);  // ADDI x1, x0, 10     ; x1 = 10
    tb.axi_write(0x20000 + 4,  0x00102023);  // SW   x1, 0(x0)      ; dmem[0] = 10
    tb.axi_write(0x20000 + 8,  0x00100073);  // EBREAK              ; halt
    tb.axi_write(0x20000 + 12, 0x01400093);  // ADDI x1, x0, 20     ; x1 = 20 (should NOT execute)
    tb.axi_write(0x20000 + 16, 0x00102023);  // SW   x1, 0(x0)      ; dmem[0] = 20 (should NOT execute)

    // Clear DMEM
    tb.axi_write(0x80000, 0);

    // Start CPU
    tb.axi_write(0x00, 0x01);  // RUN

    // Run long enough for EBREAK to propagate through pipeline
    for (int i = 0; i < 50; i++) tb.tick();

    // Check STATUS register - should show HALTED
    uint32_t ebreak_status = (uint32_t)tb.axi_read(0x08);
    printf("  STATUS: 0x%02X (running=%d, halted=%d)\n", ebreak_status, ebreak_status & 1, (ebreak_status >> 1) & 1);

    // CPU should be halted (not running, halted flag set)
    if ((ebreak_status & 1) != 0) {
        printf("  ERROR: CPU should not be running after EBREAK!\n"); errors++;
    } else if ((ebreak_status & 2) == 0) {
        printf("  ERROR: HALTED flag should be set!\n"); errors++;
    } else {
        printf("  PASS: CPU halted correctly\n");
    }

    // DMEM[0] should be 10 (not 20, since instructions after EBREAK shouldn't execute)
    uint32_t ebreak_result = (uint32_t)tb.axi_read(0x80000);
    printf("  DMEM[0]: %u (expected 10)\n", ebreak_result);
    if (ebreak_result != 10) {
        printf("  ERROR: EBREAK test failed - instructions after EBREAK executed!\n"); errors++;
    } else {
        printf("  EBREAK test PASSED!\n");
    }

    printf("\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }

    return errors;
}
