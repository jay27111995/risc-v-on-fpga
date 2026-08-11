// ============================================================================
// Bus Sniffer Testbench
// ============================================================================
//
// Tests the bus_sniffer debug module which captures host AXI transactions:
// - Write transactions (address + data)
// - Read transactions (address + returned data)
//
// The sniffer maintains a circular buffer of recent host transactions for
// debugging PCIe BAR accesses to the SoC.
//
// Log Entry Format (128 bits):
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits)
//   [31:20]  - reserved  (12 bits)
//   [19:1]   - address   (19 bits)
//   [0]      - type      (0=read, 1=write)
//
// ============================================================================

#include <cstdio>
#include "Vbus_sniffer.h"
#include "verilated.h"

class Testbench {
public:
    Vbus_sniffer* dut;
    uint64_t cycle;
    uint32_t mem[256];  // Simulated downstream memory

    Testbench() {
        dut = new Vbus_sniffer;
        cycle = 0;

        dut->clk = 0;
        dut->rst_n = 0;
        dut->log_enable = 0;
        dut->log_clear = 0;
        dut->in_addr = 0;
        dut->in_wdata = 0;
        dut->in_wen = 0;
        dut->in_ren = 0;
        dut->in_rdata = 0;
        dut->log_idx = 0;

        for (int i = 0; i < 256; i++) mem[i] = 0;

        // Reset sequence
        for (int i = 0; i < 5; i++) tick();
        dut->rst_n = 1;
        for (int i = 0; i < 2; i++) tick();

        // Enable logging
        dut->log_enable = 1;
    }

    ~Testbench() {
        dut->final();
        delete dut;
    }

    void tick() {
        // Simulate downstream memory (1 cycle read latency)
        if (dut->out_wen) {
            uint32_t idx = (dut->out_addr >> 2) & 0xFF;
            mem[idx] = dut->out_wdata;
        }
        if (dut->out_ren) {
            uint32_t idx = (dut->out_addr >> 2) & 0xFF;
            dut->in_rdata = mem[idx];
        }

        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
        cycle++;
    }

    void write(uint32_t addr, uint32_t data) {
        dut->in_addr = addr;
        dut->in_wdata = data;
        dut->in_wen = 1;
        tick();
        dut->in_wen = 0;
        tick();  // Pipeline flush
    }

    uint32_t read(uint32_t addr) {
        dut->in_addr = addr;
        dut->in_ren = 1;
        tick();
        dut->in_ren = 0;
        tick();  // Wait for data + pipeline
        tick();  // Extra for pipeline
        return dut->out_rdata;
    }

    // Get parsed log entry
    struct LogEntry {
        bool is_write;
        uint32_t addr;
        uint64_t timestamp;
        uint32_t data;
    };

    LogEntry get_log_entry(int idx) {
        dut->log_idx = idx;
        dut->eval();

        LogEntry e;
        uint32_t w0 = dut->log_entry[0];
        uint32_t w1 = dut->log_entry[1];
        uint32_t w2 = dut->log_entry[2];
        uint32_t w3 = dut->log_entry[3];

        e.is_write = w0 & 1;
        e.addr = ((w0 >> 1) & 0x7FFFF) << 1;  // Restore LSB
        e.timestamp = ((uint64_t)w2 << 32) | w1;
        e.data = w3;
        return e;
    }

    void print_log_entry(int idx) {
        LogEntry e = get_log_entry(idx);
        printf("  [%2d] cycle=%lu %s addr=0x%05X data=0x%08X\n",
               idx, e.timestamp, e.is_write ? "WR" : "RD", e.addr, e.data);
    }

    void clear_log() {
        dut->log_clear = 1;
        tick();
        dut->log_clear = 0;
        tick();
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    Testbench tb;
    int errors = 0;

    printf("bus_sniffer Testbench\n");
    printf("=====================\n\n");

    // -------------------------------------------------------------------------
    // Test 1: Basic write and read transactions
    // -------------------------------------------------------------------------
    printf("Test 1: Basic transactions\n");
    printf("--------------------------\n");

    tb.clear_log();

    tb.write(0x0000, 0xAAAAAAAA);
    tb.write(0x0004, 0xBBBBBBBB);
    tb.read(0x0000);
    tb.write(0x0008, 0xCCCCCCCC);
    tb.read(0x0004);
    tb.read(0x0008);

    printf("Log count: %d (expected 6)\n", tb.dut->log_count);

    if (tb.dut->log_count != 6) {
        printf("ERROR: Expected 6 logged transactions!\n");
        errors++;
    } else {
        printf("PASS: Correct transaction count\n");
    }

    printf("\nTransaction log (newest first):\n");
    for (uint32_t i = 0; i < tb.dut->log_count && i < 10; i++) {
        tb.print_log_entry(i);
    }

    // -------------------------------------------------------------------------
    // Test 2: Verify passthrough - data reaches downstream
    // -------------------------------------------------------------------------
    printf("\nTest 2: Passthrough verification\n");
    printf("--------------------------------\n");

    tb.clear_log();

    tb.write(0x0100, 0xDEADBEEF);
    uint32_t readback = tb.read(0x0100);

    printf("Wrote 0xDEADBEEF to 0x0100\n");
    printf("Read back: 0x%08X\n", readback);

    if (readback == 0xDEADBEEF) {
        printf("PASS: Passthrough working\n");
    } else {
        printf("ERROR: Passthrough failed!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 3: Clear functionality
    // -------------------------------------------------------------------------
    printf("\nTest 3: Clear functionality\n");
    printf("---------------------------\n");

    // Do some transactions
    tb.write(0x0000, 0x11111111);
    tb.write(0x0004, 0x22222222);

    uint32_t count_before = tb.dut->log_count;
    printf("Log count before clear: %d\n", count_before);

    tb.clear_log();

    printf("Log count after clear: %d (expected 0)\n", tb.dut->log_count);

    if (tb.dut->log_count != 0) {
        printf("ERROR: Log count should be 0 after clear!\n");
        errors++;
    } else {
        printf("PASS: Clear works\n");
    }

    // Verify logging resumes
    tb.write(0x0008, 0x33333333);
    if (tb.dut->log_count != 1) {
        printf("ERROR: Logging should resume after clear!\n");
        errors++;
    } else {
        printf("PASS: Logging resumes after clear\n");
    }

    // -------------------------------------------------------------------------
    // Test 4: Log enable/disable
    // -------------------------------------------------------------------------
    printf("\nTest 4: Log enable/disable\n");
    printf("--------------------------\n");

    tb.clear_log();

    // Log while enabled
    tb.write(0x0000, 0xAAAAAAAA);
    uint32_t count_enabled = tb.dut->log_count;

    // Disable logging
    tb.dut->log_enable = 0;
    tb.write(0x0004, 0xBBBBBBBB);
    tb.write(0x0008, 0xCCCCCCCC);
    uint32_t count_disabled = tb.dut->log_count;

    // Re-enable
    tb.dut->log_enable = 1;
    tb.write(0x000C, 0xDDDDDDDD);
    uint32_t count_reenabled = tb.dut->log_count;

    printf("Count after enabled write: %d\n", count_enabled);
    printf("Count after disabled writes: %d (should be same)\n", count_disabled);
    printf("Count after re-enable write: %d\n", count_reenabled);

    if (count_disabled != count_enabled) {
        printf("ERROR: Logging occurred while disabled!\n");
        errors++;
    } else if (count_reenabled != count_enabled + 1) {
        printf("ERROR: Logging didn't resume!\n");
        errors++;
    } else {
        printf("PASS: Enable/disable working\n");
    }

    // -------------------------------------------------------------------------
    // Test 5: Verify read data captured correctly
    // -------------------------------------------------------------------------
    printf("\nTest 5: Read data capture\n");
    printf("-------------------------\n");

    tb.clear_log();

    // Write a known value, then read it
    tb.write(0x0200, 0xCAFEBABE);
    tb.read(0x0200);

    // The read entry should have the data that was returned
    Testbench::LogEntry e = tb.get_log_entry(0);  // Newest = the read

    printf("Read transaction: addr=0x%05X data=0x%08X is_write=%d\n",
           e.addr, e.data, e.is_write);

    if (!e.is_write && e.data == 0xCAFEBABE) {
        printf("PASS: Read data captured correctly\n");
    } else {
        printf("ERROR: Read data mismatch!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 6: Address range (19-bit)
    // -------------------------------------------------------------------------
    printf("\nTest 6: 19-bit address range\n");
    printf("----------------------------\n");

    tb.clear_log();

    // Test high address
    uint32_t high_addr = 0x7FFF0;  // Near top of 19-bit range
    tb.write(high_addr, 0x12345678);

    e = tb.get_log_entry(0);
    printf("Test address:   0x%05X\n", high_addr);
    printf("Logged address: 0x%05X\n", e.addr);

    if (e.addr == high_addr) {
        printf("PASS: High address captured correctly\n");
    } else {
        printf("ERROR: Address mismatch!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 7: Circular buffer wrap
    // -------------------------------------------------------------------------
    printf("\nTest 7: Circular buffer (write 35 entries, depth=32)\n");
    printf("----------------------------------------------------\n");

    tb.clear_log();

    // Write more than LOG_DEPTH entries
    for (int i = 0; i < 35; i++) {
        tb.write(i * 4, i);
    }

    printf("Log count: %d (should be 35, trans_count)\n", tb.dut->log_count);

    // Entry 0 = newest = 34
    e = tb.get_log_entry(0);
    printf("Newest (idx=0): data=0x%08X (expected 34)\n", e.data);

    // Entry 31 = oldest still in buffer = 34-31 = 3
    e = tb.get_log_entry(31);
    printf("Oldest (idx=31): data=0x%08X (expected 3)\n", e.data);

    if (tb.get_log_entry(0).data == 34 && tb.get_log_entry(31).data == 3) {
        printf("PASS: Circular buffer working\n");
    } else {
        printf("ERROR: Circular buffer issue\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Test 8: Timestamp ordering
    // -------------------------------------------------------------------------
    printf("\nTest 8: Timestamp ordering\n");
    printf("--------------------------\n");

    tb.clear_log();

    tb.write(0x0000, 0x11111111);
    tb.write(0x0004, 0x22222222);
    tb.write(0x0008, 0x33333333);

    Testbench::LogEntry e0 = tb.get_log_entry(0);  // Newest
    Testbench::LogEntry e1 = tb.get_log_entry(1);
    Testbench::LogEntry e2 = tb.get_log_entry(2);  // Oldest

    printf("Entry 0 (newest): timestamp=%lu\n", e0.timestamp);
    printf("Entry 1:          timestamp=%lu\n", e1.timestamp);
    printf("Entry 2 (oldest): timestamp=%lu\n", e2.timestamp);

    if (e0.timestamp > e1.timestamp && e1.timestamp > e2.timestamp) {
        printf("PASS: Timestamps in correct order (newest > oldest)\n");
    } else {
        printf("ERROR: Timestamp ordering wrong!\n");
        errors++;
    }

    // -------------------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------------------
    printf("\n");
    printf("========================================\n");
    if (errors == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== FAILED: %d errors ===\n", errors);
    }
    printf("========================================\n");

    return errors;
}
