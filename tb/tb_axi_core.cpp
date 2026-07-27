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
    
    printf("Running for 8 cycles (enough for 5 instructions + store)...\n");
    for (int i = 0; i < 8; i++) tb.tick();
    
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
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    
    return errors;
}
