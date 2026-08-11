// Host Simulation Testbench
// ============================================================================
// Tests the same programs as riscv_host.c but via Verilator simulation.
// This validates host code changes without needing FPGA hardware.
// ============================================================================

#include <cstdio>
#include <cstdint>
#include <cstring>
#include "Vaxi_core_hw.h"
#include "verilated.h"
#include "../host/riscv_tests.h"

// BAR offsets (must match riscv_host.c)
#define BAR_CTRL      0x0000
#define BAR_STATUS    0x0008
#define BAR_PC        0x0010
#define BAR_CYCLES    0x0020
#define BAR_IMEM      0x20000
#define BAR_SNIFFER   0x40000
#define BAR_CPULOG    0x50000
#define BAR_DMEM      0x80000

#define CPULOG_COUNT  0x0000
#define CPULOG_CTRL   0x0008
#define CPULOG_ENTRY0 0x0010

#define CTRL_RUN      (1 << 0)
#define CTRL_RESET    (1 << 1)

// Simulated BAR access via AXI (copied from working tb_axi_core.cpp)
class SimulatedBAR {
public:
    Vaxi_core_hw *dut;
    uint64_t cycle;

    SimulatedBAR() {
        dut = new Vaxi_core_hw;
        cycle = 0;

        dut->clk = 0;
        dut->rst = 1;
        dut->axi_lite_s_awvalid = 0;
        dut->axi_lite_s_wvalid = 0;
        dut->axi_lite_s_bready = 1;
        dut->axi_lite_s_arvalid = 0;
        dut->axi_lite_s_rready = 1;
        dut->axi_lite_s_wstrb = 0xFF;

        // Reset
        for (int i = 0; i < 10; i++) tick();
        dut->rst = 0;
        for (int i = 0; i < 5; i++) tick();
    }

    ~SimulatedBAR() {
        dut->final();
        delete dut;
    }

    void tick() {
        dut->clk = 0;
        dut->eval();
        dut->clk = 1;
        dut->eval();
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

    // Same API as riscv_host.c
    void write64(uint32_t offset, uint64_t value) {
        axi_write(offset, value);
    }

    uint64_t read64(uint32_t offset) {
        return axi_read(offset);
    }

    void write32(uint32_t offset, uint32_t value) {
        uint32_t aligned = offset & ~7;
        uint64_t current = axi_read(aligned);
        if (offset & 4) {
            current = (current & 0xFFFFFFFF) | ((uint64_t)value << 32);
        } else {
            current = (current & 0xFFFFFFFF00000000ULL) | value;
        }
        axi_write(aligned, current);
    }

    uint32_t read32(uint32_t offset) {
        uint64_t qword = axi_read(offset & ~7);
        if (offset & 4) {
            return (uint32_t)(qword >> 32);
        } else {
            return (uint32_t)qword;
        }
    }

    // Memory helpers
    void write_imem(uint32_t word_idx, uint32_t value) {
        uint32_t pair_idx = word_idx / 2;
        uint32_t offset = BAR_IMEM + pair_idx * 8;
        uint64_t data = read64(offset);
        if (word_idx & 1) {
            data = (data & 0xFFFFFFFF) | ((uint64_t)value << 32);
        } else {
            data = (data & 0xFFFFFFFF00000000ULL) | value;
        }
        write64(offset, data);
    }

    uint32_t read_dmem(uint32_t word_idx) {
        uint32_t offset = BAR_DMEM + (word_idx / 2) * 8;
        uint64_t data = read64(offset);
        return (word_idx & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
    }

    void load_program(const uint32_t *prog, size_t count) {
        for (size_t i = 0; i < count; i++) {
            write_imem(i, prog[i]);
        }
        // Pad with NOP if odd
        if (count & 1) {
            write_imem(count, 0x00000013);
        }
    }

    void cpu_reset() {
        write32(BAR_CTRL, CTRL_RESET);
        for (int i = 0; i < 10; i++) tick();
    }

    void cpu_run() {
        write32(BAR_CTRL, CTRL_RUN);
    }

    void cpu_stop() {
        write32(BAR_CTRL, 0);
    }

    void run_cycles(int n) {
        for (int i = 0; i < n; i++) tick();
    }

    void clear_dmem() {
        for (int i = 0; i < 32; i++) {
            write64(BAR_DMEM + i * 8, 0);
        }
    }
};

// ----------------------------------------------------------------------------
// CPU Logger Entry (same as riscv_host.c)
// ----------------------------------------------------------------------------

typedef struct {
    uint64_t timestamp;
    uint32_t address;
    uint32_t data;
    uint8_t type;
} cpulog_entry_t;

#define CPULOG_TYPE_IFETCH  0
#define CPULOG_TYPE_DLOAD   1
#define CPULOG_TYPE_DSTORE  2

static const char *cpulog_type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};

static void cpulog_read_entry(SimulatedBAR *bar, int idx, cpulog_entry_t *entry) {
    uint32_t base = BAR_CPULOG + CPULOG_ENTRY0 + idx * 0x10;
    uint32_t w0 = bar->read32(base + 0x0);
    uint32_t w1 = bar->read32(base + 0x4);
    uint32_t w2 = bar->read32(base + 0x8);
    uint32_t w3 = bar->read32(base + 0xC);

    entry->type = w0 & 0x3;
    entry->address = ((w0 >> 2) & 0x3FFFF) << 2;
    entry->timestamp = ((uint64_t)w2 << 32) | w1;
    entry->data = w3;
}

static void cpulog_print_entry(int idx, const cpulog_entry_t *entry) {
    printf("    [%d] cycle=%lu %s addr=0x%05X data=0x%08X\n",
           idx, entry->timestamp, cpulog_type_names[entry->type & 3],
           entry->address, entry->data);
}

// ----------------------------------------------------------------------------
// Test Runner
// ----------------------------------------------------------------------------

static int run_single_test(SimulatedBAR *bar, const riscv_test_t *test, int test_num) {
    printf("=== Test %d: %s ===\n", test_num, test->name);

    bar->cpu_reset();
    bar->load_program(test->program, test->prog_size);
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t result = bar->read_dmem(test->dmem_word);
    int pass = (result == test->expected);

    if (test->desc) {
        printf("  %s\n", test->desc);
    }
    if (test->expect_hex) {
        printf("  Result: 0x%08X (expected 0x%08X) - %s\n\n",
               result, test->expected, pass ? "PASS" : "FAIL");
    } else {
        printf("  Result: %u (expected %u) - %s\n\n",
               result, test->expected, pass ? "PASS" : "FAIL");
    }
    return pass;
}

static int test_shifts(SimulatedBAR *bar) {
    printf("=== Test: Shifts (SLL, SRL, SRA) ===\n");

    bar->cpu_reset();
    bar->load_program(prog_shifts, ARRAY_SIZE(prog_shifts));
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t sll = bar->read_dmem(6);
    uint32_t srl = bar->read_dmem(7);
    uint32_t sra = bar->read_dmem(8);

    printf("  SLL: %u (expected 32)\n", sll);
    printf("  SRL: %u (expected 2)\n", srl);
    printf("  SRA: 0x%08X (expected 0xFFFFFE00)\n", sra);

    int pass = (sll == 32 && srl == 2 && sra == 0xFFFFFE00);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static int test_imm_shifts(SimulatedBAR *bar) {
    printf("=== Test: Immediate Shifts ===\n");

    bar->cpu_reset();
    bar->load_program(prog_imm_shifts, ARRAY_SIZE(prog_imm_shifts));
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t slli = bar->read_dmem(9);
    uint32_t srli = bar->read_dmem(10);
    uint32_t srai = bar->read_dmem(11);

    printf("  SLLI: %u (expected 65536)\n", slli);
    printf("  SRLI: %u (expected 256)\n", srli);
    printf("  SRAI: 0x%08X (expected 0xFFFFFF80)\n", srai);

    int pass = (slli == 65536 && srli == 256 && srai == 0xFFFFFF80);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static int test_slt(SimulatedBAR *bar) {
    printf("=== Test: SLT/SLTU ===\n");

    bar->cpu_reset();
    bar->load_program(prog_slt, ARRAY_SIZE(prog_slt));
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t slt = bar->read_dmem(12);
    uint32_t sltu = bar->read_dmem(13);

    printf("  SLT:  %u (expected 1)\n", slt);
    printf("  SLTU: %u (expected 0)\n", sltu);

    int pass = (slt == 1 && sltu == 0);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static int test_byte_ops(SimulatedBAR *bar) {
    printf("=== Test: SB/LB/LBU ===\n");

    bar->cpu_reset();
    bar->load_program(prog_byte_ops, ARRAY_SIZE(prog_byte_ops));
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t lb = bar->read_dmem(18);
    uint32_t lbu = bar->read_dmem(19);

    printf("  LB:  0x%08X (expected 0xFFFFFFFF)\n", lb);
    printf("  LBU: 0x%08X (expected 0x000000FF)\n", lbu);

    int pass = (lb == 0xFFFFFFFF && lbu == 0x000000FF);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static int test_halfword_ops(SimulatedBAR *bar) {
    printf("=== Test: SH/LH/LHU ===\n");

    bar->cpu_reset();
    bar->load_program(prog_halfword_ops, ARRAY_SIZE(prog_halfword_ops));
    bar->cpu_run();
    bar->run_cycles(5000);
    bar->cpu_stop();

    uint32_t lh = bar->read_dmem(20);
    uint32_t lhu = bar->read_dmem(21);

    printf("  LH:  0x%08X (expected 0xFFFFFFFF)\n", lh);
    printf("  LHU: 0x%08X (expected 0x0000FFFF)\n", lhu);

    int pass = (lh == 0xFFFFFFFF && lhu == 0x0000FFFF);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static int test_cpu_logger(SimulatedBAR *bar) {
    printf("=== Test: CPU Logger ===\n");

    bar->cpu_reset();

    // Clear and enable logger
    bar->write32(BAR_CPULOG + CPULOG_CTRL, 0x03);  // clear + enable
    bar->run_cycles(10);
    bar->write32(BAR_CPULOG + CPULOG_CTRL, 0x01);  // enable, log_imem=0

    // Clear IMEM
    for (int i = 0; i < 64; i++) {
        bar->write_imem(i, 0x00000013);
    }

    bar->load_program(prog_cpulog_test, ARRAY_SIZE(prog_cpulog_test));
    bar->cpu_run();
    bar->run_cycles(500);
    bar->cpu_stop();

    uint32_t count = bar->read32(BAR_CPULOG + CPULOG_COUNT);
    printf("  Log count: %u\n", count);

    if (count < 3) {
        printf("  ERROR: Expected at least 3 DMEM transactions\n");
        printf("  FAIL\n\n");
        return 0;
    }

    printf("  Log entries (newest first):\n");
    int n = (count < 8) ? count : 8;
    int stores = 0, loads = 0;

    for (int i = 0; i < n; i++) {
        cpulog_entry_t entry;
        cpulog_read_entry(bar, i, &entry);
        cpulog_print_entry(i, &entry);
        if (entry.type == CPULOG_TYPE_DLOAD) loads++;
        if (entry.type == CPULOG_TYPE_DSTORE) stores++;
    }

    printf("  Found %d stores, %d loads\n", stores, loads);
    int pass = (stores >= 2 && loads >= 1);
    printf("  %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    printf("Host Simulation Test\n");
    printf("====================\n");
    printf("Testing riscv_host.c logic via Verilator\n\n");

    SimulatedBAR bar;
    bar.clear_dmem();

    int pass = 0;

    // Run simple tests from table
    for (size_t i = 0; i < RISCV_TEST_COUNT; i++) {
        pass += run_single_test(&bar, &riscv_tests[i], i + 1);
    }

    // Run multi-result tests
    pass += test_shifts(&bar);
    pass += test_imm_shifts(&bar);
    pass += test_slt(&bar);
    pass += test_byte_ops(&bar);
    pass += test_halfword_ops(&bar);

    // Test CPU logger parsing
    pass += test_cpu_logger(&bar);

    int total = RISCV_TEST_COUNT + 6;
    printf("=== Summary: %d/%d tests passed ===\n", pass, total);

    if (pass == total) {
        printf("\n=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("\n=== SOME TESTS FAILED ===\n");
        return 1;
    }
}
