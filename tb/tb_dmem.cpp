#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vdmem.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    Vdmem* dut = new Vdmem;
    
    VerilatedVcdC* tfp = new VerilatedVcdC;
    Verilated::traceEverOn(true);
    dut->trace(tfp, 99);
    tfp->open("dmem_trace.vcd");
    
    vluint64_t sim_time = 0;
    int errors = 0;
    
    auto tick = [&]() {
        dut->clk = 0;
        dut->eval();
        tfp->dump(sim_time++);
        dut->clk = 1;
        dut->eval();
        tfp->dump(sim_time++);
    };
    
    // Initialize
    dut->cpu_en = 0;
    dut->cpu_we = 0;
    dut->cpu_be = 0xF;
    dut->host_en = 0;
    dut->host_we = 0;
    tick();
    
    printf("=== DMEM Module Test ===\n\n");
    
    // =========================================================================
    // Test 1: Basic CPU write and read
    // =========================================================================
    printf("Test 1: Basic CPU write/read\n");
    
    // Write 0xDEADBEEF to address 0
    dut->cpu_en = 1;
    dut->cpu_we = 1;
    dut->cpu_addr = 0;
    dut->cpu_wdata = 0xDEADBEEF;
    dut->cpu_be = 0xF;
    tick();
    
    // Disable write
    dut->cpu_we = 0;
    tick();
    
    // Read back - initiate read
    dut->cpu_addr = 0;
    tick();  // Address registered
    tick();  // Data available
    
    if (dut->cpu_rdata == 0xDEADBEEF) {
        printf("  PASS: Read 0x%08X (expected 0xDEADBEEF)\n", dut->cpu_rdata);
    } else {
        printf("  FAIL: Read 0x%08X (expected 0xDEADBEEF)\n", dut->cpu_rdata);
        errors++;
    }
    
    // =========================================================================
    // Test 2: Address changes during read - this is the key test!
    // =========================================================================
    printf("\nTest 2: Address change during read\n");
    
    // Write different values to addresses 10 and 20
    dut->cpu_we = 1;
    dut->cpu_addr = 10;
    dut->cpu_wdata = 0x11111111;
    tick();
    
    dut->cpu_addr = 20;
    dut->cpu_wdata = 0x22222222;
    tick();
    
    dut->cpu_we = 0;
    tick();
    
    // Initiate read from address 10
    dut->cpu_addr = 10;
    tick();  // Address 10 registered
    
    // Change address to 20 BEFORE data arrives
    dut->cpu_addr = 20;
    tick();  // Data for address 10 should be available now
    
    if (dut->cpu_rdata == 0x11111111) {
        printf("  PASS: Read 0x%08X from addr 10 (address changed to 20 mid-read)\n", dut->cpu_rdata);
    } else {
        printf("  FAIL: Read 0x%08X (expected 0x11111111 from addr 10)\n", dut->cpu_rdata);
        errors++;
    }
    
    // Now the read from address 20 should complete
    tick();
    tick();
    
    if (dut->cpu_rdata == 0x22222222) {
        printf("  PASS: Read 0x%08X from addr 20\n", dut->cpu_rdata);
    } else {
        printf("  FAIL: Read 0x%08X (expected 0x22222222 from addr 20)\n", dut->cpu_rdata);
        errors++;
    }
    
    // =========================================================================
    // Test 3: Host write while CPU reading
    // =========================================================================
    printf("\nTest 3: Host write priority\n");
    
    // Setup: write initial value via CPU
    dut->cpu_we = 1;
    dut->cpu_addr = 30;
    dut->cpu_wdata = 0xAAAAAAAA;
    tick();
    dut->cpu_we = 0;
    tick();
    
    // Host writes different value to same address
    dut->host_en = 1;
    dut->host_we = 1;
    dut->host_addr = 30;
    dut->host_wdata = 0xBBBBBBBB;
    tick();
    dut->host_we = 0;
    dut->host_en = 0;
    tick();
    
    // CPU reads back
    dut->cpu_addr = 30;
    tick();
    tick();
    
    if (dut->cpu_rdata == 0xBBBBBBBB) {
        printf("  PASS: CPU reads 0x%08X (host's write took effect)\n", dut->cpu_rdata);
    } else {
        printf("  FAIL: CPU reads 0x%08X (expected 0xBBBBBBBB)\n", dut->cpu_rdata);
        errors++;
    }
    
    // =========================================================================
    // Test 4: Consecutive reads (like uart echo loop)
    // =========================================================================
    printf("\nTest 4: Consecutive reads (echo pattern)\n");
    
    // Write test values
    dut->cpu_we = 1;
    for (int i = 0; i < 4; i++) {
        dut->cpu_addr = 100 + i;
        dut->cpu_wdata = 0x10 + i;  // 0x10, 0x11, 0x12, 0x13
        tick();
    }
    dut->cpu_we = 0;
    tick();
    
    // Read them back consecutively
    int expected[] = {0x10, 0x11, 0x12, 0x13};
    int pass = 1;
    
    for (int i = 0; i < 4; i++) {
        dut->cpu_addr = 100 + i;
        tick();  // Address registered
        tick();  // Data available
        
        if ((dut->cpu_rdata & 0xFF) != expected[i]) {
            printf("  FAIL: addr %d: read 0x%02X, expected 0x%02X\n", 
                   100+i, dut->cpu_rdata & 0xFF, expected[i]);
            pass = 0;
            errors++;
        }
    }
    if (pass) {
        printf("  PASS: All consecutive reads correct\n");
    }
    
    // =========================================================================
    // Test 5: Byte writes
    // =========================================================================
    printf("\nTest 5: Byte writes\n");
    
    // Write full word first
    dut->cpu_we = 1;
    dut->cpu_be = 0xF;
    dut->cpu_addr = 50;
    dut->cpu_wdata = 0x00000000;
    tick();
    
    // Write byte 0 only
    dut->cpu_be = 0x1;
    dut->cpu_wdata = 0x000000AA;
    tick();
    
    // Write byte 2 only
    dut->cpu_be = 0x4;
    dut->cpu_wdata = 0x00BB0000;
    tick();
    
    dut->cpu_we = 0;
    dut->cpu_be = 0xF;
    tick();
    
    // Read back
    dut->cpu_addr = 50;
    tick();
    tick();
    
    if (dut->cpu_rdata == 0x00BB00AA) {
        printf("  PASS: Byte writes correct: 0x%08X\n", dut->cpu_rdata);
    } else {
        printf("  FAIL: Read 0x%08X, expected 0x00BB00AA\n", dut->cpu_rdata);
        errors++;
    }
    
    // =========================================================================
    // Test 6: Write and read back after many cycles (regression for UART issue)
    // =========================================================================
    printf("\nTest 6: Write then read after delay\n");
    
    // Write a value
    dut->cpu_we = 1;
    dut->cpu_addr = 80;  // TX_HEAD index
    dut->cpu_wdata = 0x00000004;
    tick();
    dut->cpu_we = 0;
    tick();
    
    // Read immediately
    dut->cpu_addr = 80;
    tick();
    tick();
    printf("  Immediate read: cpu_rdata = %u (expected 4)\n", dut->cpu_rdata);
    
    // Now do many other operations (like the UART test does)
    for (int i = 0; i < 1000; i++) {
        dut->cpu_addr = (i % 100);  // Cycle through different addresses
        tick();
    }
    
    // Read TX_HEAD again
    dut->cpu_addr = 80;
    tick();
    tick();
    
    if (dut->cpu_rdata == 4) {
        printf("  After delay: cpu_rdata = %u - PASS\n", dut->cpu_rdata);
    } else {
        printf("  After delay: cpu_rdata = %u - FAIL (expected 4)\n", dut->cpu_rdata);
        errors++;
    }
    
    // =========================================================================
    // Summary
    // =========================================================================
    printf("\n=== Summary ===\n");
    if (errors == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("FAILED: %d errors\n", errors);
    }
    
    tfp->close();
    delete dut;
    
    return errors ? 1 : 0;
}
