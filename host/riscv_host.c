// RISC-V SoC Host Controller
// ============================================================================
// Loads programs to IMEM, runs CPU, verifies results from DMEM via PCIe BAR.
// Tests complete RV32I instruction set (37 instructions).
//
// BAR Memory Map:
//   0x00000 - 0x000FF : Control registers (CTRL, STATUS, PC, CYCLES, etc.)
//   0x20000 - 0x3FFFF : IMEM - 128KB instruction memory
//   0x40000 - 0x40FFF : Bus Sniffer (host transaction logger)
//   0x50000 - 0x50FFF : CPU Logger (CPU memory access logger)
//   0x80000 - 0x87FFF : DMEM - 32KB data memory
//
// Usage: sudo ./riscv_host [pci_addr] [iommu_group]
// ============================================================================

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pcie_vfio.h"

// ----------------------------------------------------------------------------
// BAR Memory Map
// ----------------------------------------------------------------------------

// Control registers
#define BAR_CTRL 0x0000     // [0] RUN, [1] RESET
#define BAR_STATUS 0x0008   // [0] RUNNING, [1] HALTED
#define BAR_PC 0x0010       // Current PC
#define BAR_CYCLES 0x0020   // Cycle counter
#define BAR_INSTRS 0x0024   // Instruction counter
#define BAR_STALLS 0x0028   // Stall counter
#define BAR_BRANCHES 0x002C // Branch counter
#define BAR_BR_TAKEN 0x0030 // Branches taken counter
#define BAR_LOADS 0x0034    // Load counter
#define BAR_STORES 0x0038   // Store counter

// Memory regions
#define BAR_IMEM 0x20000    // Instruction memory (128KB)
#define BAR_SNIFFER 0x40000 // Bus sniffer
#define BAR_CPULOG 0x50000  // CPU logger
#define BAR_DMEM 0x80000    // Data memory (32KB)

// Control bits
#define CTRL_RUN (1 << 0)
#define CTRL_RESET (1 << 1)

// Clock frequency (250 MHz = 4ns per cycle)
#define CLOCK_PERIOD_NS 4
#define CYCLES_TO_NS(c) ((c) * CLOCK_PERIOD_NS)

// ----------------------------------------------------------------------------
// Bus Sniffer Registers (host transaction logger)
// ----------------------------------------------------------------------------

#define SNIFF_COUNT 0x0000  // Total transactions logged
#define SNIFF_CYCLE 0x0004  // Current cycle counter
#define SNIFF_CTRL 0x0008   // [0]=enable, [1]=clear
#define SNIFF_ENTRY0 0x0010 // Entry[0] (128 bits = 4 words)

// ----------------------------------------------------------------------------
// CPU Logger Registers (CPU memory access logger)
// ----------------------------------------------------------------------------

#define CPULOG_COUNT 0x0000  // Total transactions logged (RO)
#define CPULOG_CYCLE 0x0004  // Current cycle counter (RO)
#define CPULOG_CTRL 0x0008   // [0]=enable, [1]=clear, [2]=log_imem
#define CPULOG_ENTRY0 0x0010 // Entry[0] (96 bits = 3 words)

// ----------------------------------------------------------------------------
// Log Entry Structures
// ----------------------------------------------------------------------------

// Bus sniffer entry (host transactions)
// Format: 128 bits
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits)
//   [31:20]  - reserved  (12 bits)
//   [19:1]   - address   (19 bits)
//   [0]      - type      (0=read, 1=write)
typedef struct {
  uint64_t timestamp;
  uint32_t address;
  uint32_t data;
  int is_write;
} sniffer_entry_t;

// CPU logger entry (CPU memory accesses)
// Format: 128 bits
//   [127:96] - data      (32 bits)
//   [95:32]  - timestamp (64 bits)
//   [31:20]  - reserved  (12 bits)
//   [19:2]   - address   (18 bits, word-aligned)
//   [1:0]    - type      (00=IFETCH, 01=DLOAD, 10=DSTORE)
typedef struct {
  uint64_t timestamp;
  uint32_t address;
  uint32_t data;
  uint8_t type; // 0=IFETCH, 1=DLOAD, 2=DSTORE
} cpulog_entry_t;

#define CPULOG_TYPE_IFETCH 0
#define CPULOG_TYPE_DLOAD 1
#define CPULOG_TYPE_DSTORE 2

static const char *cpulog_type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};

// ----------------------------------------------------------------------------
// CPU Control Functions
// ----------------------------------------------------------------------------

static void cpu_reset(void) {
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
}

static void cpu_run(void) { write32(BAR_CTRL, CTRL_RUN); }

static void cpu_stop(void) { write32(BAR_CTRL, 0); }

// ----------------------------------------------------------------------------
// Memory Access Functions
// ----------------------------------------------------------------------------

static void write_imem_pair(uint32_t pair_idx, uint32_t even_word,
                            uint32_t odd_word) {
  uint32_t offset = BAR_IMEM + pair_idx * 8;
  uint64_t data = ((uint64_t)odd_word << 32) | even_word;
  write64(offset, data);
}

static void write_imem(uint32_t word_idx, uint32_t value) {
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

static uint64_t read_dmem64(uint32_t idx) { return read64(BAR_DMEM + idx * 8); }

static void write_dmem64(uint32_t idx, uint64_t value) {
  write64(BAR_DMEM + idx * 8, value);
}

static uint32_t read_dmem(uint32_t word_idx) {
  uint64_t data = read_dmem64(word_idx / 2);
  return (word_idx & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
}

static void load_program(const uint32_t *program, size_t count) {
  for (size_t i = 0; i + 1 < count; i += 2) {
    write_imem_pair(i / 2, program[i], program[i + 1]);
  }
  if (count & 1) {
    write_imem_pair(count / 2, program[count - 1], 0x00000013); // NOP padding
  }
}

// ----------------------------------------------------------------------------
// Bus Sniffer Functions
// ----------------------------------------------------------------------------

static uint32_t sniffer_get_count(void) {
  return read32(BAR_SNIFFER + SNIFF_COUNT);
}

static void sniffer_clear(void) {
  write32(BAR_SNIFFER + SNIFF_CTRL, 0x03); // clear + enable
  usleep(1000);
  write32(BAR_SNIFFER + SNIFF_CTRL, 0x01); // enable only
}

static void sniffer_read_entry(int idx, sniffer_entry_t *entry) {
  uint32_t base = BAR_SNIFFER + SNIFF_ENTRY0 + idx * 0x10;
  uint32_t w0 = read32(base + 0x0);
  uint32_t w1 = read32(base + 0x4);
  uint32_t w2 = read32(base + 0x8);
  uint32_t w3 = read32(base + 0xC);

  entry->is_write = w0 & 1;
  entry->address = (w0 >> 1) & 0x7FFFF;
  entry->timestamp = ((uint64_t)w2 << 32) | w1;
  entry->data = w3;
}

static void sniffer_print_entry(int idx, const sniffer_entry_t *entry,
                                uint64_t base_cycle) {
  uint64_t offset = entry->timestamp - base_cycle;
  uint64_t offset_ns = CYCLES_TO_NS(offset);
  printf("    [%2d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n", idx,
         offset, offset_ns, entry->is_write ? "WR" : "RD", entry->address,
         entry->data);
}

static void sniffer_dump(int max_entries) {
  uint32_t count = sniffer_get_count();
  if (count == 0) {
    printf("  (no entries)\n");
    return;
  }
  int n = (count < (uint32_t)max_entries) ? count : max_entries;

  // Read all entries first to find base cycle (oldest entry = last one)
  sniffer_entry_t entries[64];
  for (int i = 0; i < n; i++) {
    sniffer_read_entry(i, &entries[i]);
  }
  uint64_t base_cycle = entries[n - 1].timestamp;

  for (int i = 0; i < n; i++) {
    sniffer_print_entry(i, &entries[i], base_cycle);
  }
}

// ----------------------------------------------------------------------------
// CPU Logger Functions
// ----------------------------------------------------------------------------

static uint32_t cpulog_get_count(void) {
  return read32(BAR_CPULOG + CPULOG_COUNT);
}

static void cpulog_clear(void) {
  write32(BAR_CPULOG + CPULOG_CTRL, 0x03); // clear + enable
  usleep(1000);
  write32(BAR_CPULOG + CPULOG_CTRL, 0x01); // enable, log_imem=0
}

static void cpulog_read_entry(int idx, cpulog_entry_t *entry) {
  uint32_t base = BAR_CPULOG + CPULOG_ENTRY0 + idx * 0x10;
  uint32_t w0 = read32(base + 0x0);
  uint32_t w1 = read32(base + 0x4);
  uint32_t w2 = read32(base + 0x8);
  uint32_t w3 = read32(base + 0xC);

  entry->type = w0 & 0x3;
  entry->address = ((w0 >> 2) & 0x3FFFF) << 2;
  entry->timestamp = ((uint64_t)w2 << 32) | w1;
  entry->data = w3;
}

static void cpulog_print_entry(int idx, const cpulog_entry_t *entry,
                               uint64_t base_cycle) {
  uint64_t offset = entry->timestamp - base_cycle;
  uint64_t offset_ns = CYCLES_TO_NS(offset);
  printf("    [%3d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n", idx,
         offset, offset_ns, cpulog_type_names[entry->type & 3], entry->address,
         entry->data);
}

static void cpulog_dump(int max_entries) {
  uint32_t count = cpulog_get_count();
  if (count == 0) {
    printf("  (no entries)\n");
    return;
  }
  int n = (count < (uint32_t)max_entries) ? count : max_entries;

  // Read all entries first to find base cycle (oldest entry = last one)
  cpulog_entry_t entries[256];
  for (int i = 0; i < n; i++) {
    cpulog_read_entry(i, &entries[i]);
  }
  uint64_t base_cycle = entries[n - 1].timestamp;

  for (int i = 0; i < n; i++) {
    cpulog_print_entry(i, &entries[i], base_cycle);
  }
}

// ----------------------------------------------------------------------------
// Performance Counters
// ----------------------------------------------------------------------------

static void print_perf_counters(void) {
  printf("=== Performance Counters ===\n");

  uint32_t cycles = read32(BAR_CYCLES);
  uint32_t instrs = read32(BAR_INSTRS);
  uint32_t stalls = read32(BAR_STALLS);
  uint32_t branches = read32(BAR_BRANCHES);
  uint32_t br_taken = read32(BAR_BR_TAKEN);
  uint32_t loads = read32(BAR_LOADS);
  uint32_t stores = read32(BAR_STORES);

  printf("  Cycles:         %u\n", cycles);
  printf("  Instructions:   %u\n", instrs);
  printf("  Stalls:         %u\n", stalls);
  printf("  Branches:       %u (taken: %u)\n", branches, br_taken);
  printf("  Loads:          %u\n", loads);
  printf("  Stores:         %u\n", stores);

  if (instrs > 0) {
    printf("  CPI: %.2f\n", (float)cycles / instrs);
    printf("  IPC: %.2f\n", (float)instrs / cycles);
  }
  printf("\n");
}

// ----------------------------------------------------------------------------
// Test Infrastructure
// ----------------------------------------------------------------------------

static int run_test(const char *name, const uint32_t *program, size_t prog_size,
                    uint32_t dmem_word, uint32_t expected, const char *desc) {
  printf("=== %s ===\n", name);

  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(program, prog_size);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t result = read_dmem(dmem_word);
  int pass = (result == expected);

  if (desc) {
    printf("  %s\n", desc);
  }
  printf("  Result: %u (expected %u) - %s\n\n", result, expected,
         pass ? "PASS" : "FAIL");
  return pass;
}

static int run_test_hex(const char *name, const uint32_t *program,
                        size_t prog_size, uint32_t dmem_word, uint32_t expected,
                        const char *desc) {
  printf("=== %s ===\n", name);

  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(program, prog_size);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t result = read_dmem(dmem_word);
  int pass = (result == expected);

  if (desc) {
    printf("  %s\n", desc);
  }
  printf("  Result: 0x%08X (expected 0x%08X) - %s\n\n", result, expected,
         pass ? "PASS" : "FAIL");
  return pass;
}

// ----------------------------------------------------------------------------
// RV32I Tests
// ----------------------------------------------------------------------------

static int test_basic_alu(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0x00300113, // ADDI x2, x0, 3
      0x002081B3, // ADD  x3, x1, x2
      0x00302023, // SW   x3, 0(x0)
      0x00000063, // BEQ  x0, x0, 0 (loop)
  };
  return run_test("Test 1: Basic ALU", prog, 5, 0, 8, "5 + 3 = 8");
}

static int test_bne(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFF08093, // ADDI x1, x1, -1
      0xFE1010E3, // BNE  x1, x0, -4
      0x00102223, // SW   x1, 4(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 2: BNE (count down)", prog, 5, 1, 0, "Count 5->0");
}

static int test_blt(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5
      0x00300113, // ADDI x2, x0, 3
      0x00100193, // ADDI x3, x0, 1
      0x0020C463, // BLT  x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302423, // SW   x3, 8(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 3: BLT (signed)", prog, 7, 2, 1, "-5 < 3 = true");
}

static int test_bltu(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5 (= 0xFFFFFFFB)
      0x00300113, // ADDI x2, x0, 3
      0x00100193, // ADDI x3, x0, 1
      0x0020E463, // BLTU x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302623, // SW   x3, 12(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 4: BLTU (unsigned)", prog, 7, 3, 2, "0xFFFFFFFB > 3");
}

static int test_bge(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFD00113, // ADDI x2, x0, -3
      0x00100193, // ADDI x3, x0, 1
      0x0020D463, // BGE  x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302823, // SW   x3, 16(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 5: BGE (signed)", prog, 7, 4, 1, "5 >= -3 = true");
}

static int test_bgeu(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFD00113, // ADDI x2, x0, -3 (= 0xFFFFFFFD)
      0x00100193, // ADDI x3, x0, 1
      0x0020F463, // BGEU x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302A23, // SW   x3, 20(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 6: BGEU (unsigned)", prog, 7, 5, 2, "5 < 0xFFFFFFFD");
}

static int test_shifts(void) {
  uint32_t prog[] = {
      0x00800093, // ADDI x1, x0, 8
      0x00200113, // ADDI x2, x0, 2
      0x002091B3, // SLL  x3, x1, x2  (8 << 2 = 32)
      0x0020D233, // SRL  x4, x1, x2  (8 >> 2 = 2)
      0x80000293, // ADDI x5, x0, -2048
      0x4022D333, // SRA  x6, x5, x2  (0xFFFFF800 >>> 2)
      0x00302C23, // SW   x3, 24(x0)
      0x00402E23, // SW   x4, 28(x0)
      0x02602023, // SW   x6, 32(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 7: Shifts (SLL, SRL, SRA) ===\n");
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(prog, 10);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t sll = read_dmem(6);
  uint32_t srl = read_dmem(7);
  uint32_t sra = read_dmem(8);

  printf("  SLL: %u (expected 32)\n", sll);
  printf("  SRL: %u (expected 2)\n", srl);
  printf("  SRA: 0x%08X (expected 0xFFFFFE00)\n", sra);

  int pass = (sll == 32 && srl == 2 && sra == 0xFFFFFE00);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_imm_shifts(void) {
  uint32_t prog[] = {
      0x00100093, // ADDI x1, x0, 1
      0x01009113, // SLLI x2, x1, 16  (1 << 16 = 65536)
      0x00815193, // SRLI x3, x2, 8   (65536 >> 8 = 256)
      0x80000213, // ADDI x4, x0, -2048
      0x40425293, // SRAI x5, x4, 4   (0xFFFFF800 >>> 4)
      0x02202223, // SW   x2, 36(x0)
      0x02302423, // SW   x3, 40(x0)
      0x02502623, // SW   x5, 44(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 8: Immediate Shifts ===\n");
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(prog, 9);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t slli = read_dmem(9);
  uint32_t srli = read_dmem(10);
  uint32_t srai = read_dmem(11);

  printf("  SLLI: %u (expected 65536)\n", slli);
  printf("  SRLI: %u (expected 256)\n", srli);
  printf("  SRAI: 0x%08X (expected 0xFFFFFF80)\n", srai);

  int pass = (slli == 65536 && srli == 256 && srai == 0xFFFFFF80);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_slt(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5
      0x00300113, // ADDI x2, x0, 3
      0x0020A1B3, // SLT  x3, x1, x2  (-5 < 3 signed = 1)
      0x0020B233, // SLTU x4, x1, x2  (0xFFFFFFFB < 3 unsigned = 0)
      0x02302823, // SW   x3, 48(x0)
      0x02402A23, // SW   x4, 52(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 9: SLT/SLTU ===\n");
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(prog, 7);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t slt = read_dmem(12);
  uint32_t sltu = read_dmem(13);

  printf("  SLT:  %u (expected 1, -5 < 3 signed)\n", slt);
  printf("  SLTU: %u (expected 0, 0xFFFFFFFB > 3 unsigned)\n", sltu);

  int pass = (slt == 1 && sltu == 0);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_jal(void) {
  uint32_t prog[] = {
      0x00500093, // 0x00: ADDI x1, x0, 5
      0x00C000EF, // 0x04: JAL x1, 12 (jump to 0x10, x1 = 8)
      0x02102C23, // 0x08: SW x1, 56(x0)
      0x00000063, // 0x0C: BEQ x0, x0, 0
      0x00000013, // 0x10: NOP
      0xFF5FF06F, // 0x14: JAL x0, -12 (back to 0x08)
  };
  return run_test("Test 10: JAL", prog, 6, 14, 8, "Return address = PC+4 = 8");
}

static int test_jalr(void) {
  uint32_t prog[] = {
      0x00300513, // 0x00: ADDI x10, x0, 3
      0x00C000EF, // 0x04: JAL x1, 12 (call 0x10)
      0x02A02E23, // 0x08: SW x10, 60(x0)
      0x00000063, // 0x0C: BEQ x0, x0, 0
      0x00550513, // 0x10: ADDI x10, x10, 5
      0x00008067, // 0x14: JALR x0, x1, 0 (return)
  };
  return run_test("Test 11: JALR", prog, 6, 15, 8, "Function: 3 + 5 = 8");
}

static int test_lui(void) {
  uint32_t prog[] = {
      0xDEADB0B7, // LUI x1, 0xDEADB
      0x04102023, // SW  x1, 64(x0)
      0x00000063, // BEQ x0, x0, 0
  };
  return run_test_hex("Test 12: LUI", prog, 3, 16, 0xDEADB000, NULL);
}

static int test_auipc(void) {
  uint32_t prog[] = {
      0x00001097, // AUIPC x1, 1 (x1 = PC + 0x1000 = 0x1000)
      0x04102223, // SW    x1, 68(x0)
      0x00000063, // BEQ   x0, x0, 0
  };
  return run_test_hex("Test 13: AUIPC", prog, 3, 17, 0x1000,
                      "PC + 0x1000 = 0x1000");
}

static int test_byte_ops(void) {
  uint32_t prog[] = {
      0x0FF00093, // ADDI x1, x0, 255
      0x04800113, // ADDI x2, x0, 72
      0x00110023, // SB   x1, 0(x2)
      0x00010183, // LB   x3, 0(x2) (sign extend)
      0x00014203, // LBU  x4, 0(x2) (zero extend)
      0x04302423, // SW   x3, 72(x0)
      0x04402623, // SW   x4, 76(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 14: SB/LB/LBU ===\n");
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(prog, 8);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t lb = read_dmem(18);
  uint32_t lbu = read_dmem(19);

  printf("  LB:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lb);
  printf("  LBU: 0x%08X (expected 0x000000FF, zero extended)\n", lbu);

  int pass = (lb == 0xFFFFFFFF && lbu == 0x000000FF);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_halfword_ops(void) {
  uint32_t prog[] = {
      0xFFF00093, // ADDI x1, x0, -1
      0x05000113, // ADDI x2, x0, 80
      0x00111023, // SH   x1, 0(x2)
      0x00011183, // LH   x3, 0(x2) (sign extend)
      0x00015203, // LHU  x4, 0(x2) (zero extend)
      0x04302823, // SW   x3, 80(x0)
      0x04402A23, // SW   x4, 84(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 15: SH/LH/LHU ===\n");
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  load_program(prog, 8);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t lh = read_dmem(20);
  uint32_t lhu = read_dmem(21);

  printf("  LH:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lh);
  printf("  LHU: 0x%08X (expected 0x0000FFFF, zero extended)\n", lhu);

  int pass = (lh == 0xFFFFFFFF && lhu == 0x0000FFFF);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ----------------------------------------------------------------------------
// Debug Module Tests
// ----------------------------------------------------------------------------

static int test_bus_sniffer(void) {
  printf("=== Bus Sniffer Test ===\n");

  sniffer_clear();

  // Generate some transactions
  write32(BAR_DMEM, 0xDEADBEEF);
  (void)read32(BAR_DMEM);
  write32(BAR_DMEM + 4, 0xCAFEBABE);

  usleep(1000);

  uint32_t count = sniffer_get_count();
  printf("  Log count: %u (expected >= 3)\n", count);

  if (count > 0) {
    printf("  Transactions (newest first):\n");
    sniffer_dump(64);  // dump all (max 64)
  }

  int pass = (count >= 3);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_cpu_logger(void) {
  printf("=== CPU Logger Test ===\n");

  cpu_reset();
  cpulog_clear();

  // Clear IMEM
  for (int i = 0; i < 64; i++) {
    write_imem(i, 0x00000013); // NOP
  }

  // Program with stores and loads
  uint32_t prog[] = {
      0x00A00093, // ADDI x1, x0, 10
      0x00102023, // SW   x1, 0(x0)      <- DSTORE
      0x00002103, // LW   x2, 0(x0)      <- DLOAD
      0x00210133, // ADD  x2, x2, x2
      0x00202223, // SW   x2, 4(x0)      <- DSTORE
      0x00402183, // LW   x3, 4(x0)      <- DLOAD
      0xDEA00213, // ADDI x4, x0, 0xDEA
      0x00402423, // SW   x4, 8(x0)      <- DSTORE
      0x00000013, // NOP
      0x00000013, // NOP
      0x00100073, // EBREAK
  };

  for (size_t i = 0; i < sizeof(prog) / sizeof(prog[0]); i++) {
    write_imem(i, prog[i]);
  }

  cpu_run();
  usleep(10);
  cpu_stop();

  uint32_t count = cpulog_get_count();
  printf("  Log count: %u\n", count);

  if (count < 3) {
    printf("  ERROR: Expected at least 3 DMEM transactions\n");
    printf("  FAIL\n\n");
    return 0;
  }

  printf("  Log entries (newest first):\n");
  cpulog_dump(256);  // dump all (max 256)

  // Count transaction types
  int stores = 0, loads = 0;
  int n = (count < 256) ? count : 256;
  for (int i = 0; i < n; i++) {
    cpulog_entry_t entry;
    cpulog_read_entry(i, &entry);
    if (entry.type == CPULOG_TYPE_DLOAD)
      loads++;
    if (entry.type == CPULOG_TYPE_DSTORE)
      stores++;
  }

  printf("  Found %d stores, %d loads\n", stores, loads);
  int pass = (stores >= 2 && loads >= 1);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  const char *pci_addr = "0000:31:00.0";
  int iommu_group = 52;

  if (argc >= 2)
    pci_addr = argv[1];
  if (argc >= 3)
    iommu_group = atoi(argv[2]);

  printf("RISC-V SoC Test\n");
  printf("===============\n");
  printf("PCI: %s, IOMMU group: %d\n\n", pci_addr, iommu_group);

  if (vfio_init(pci_addr, iommu_group) < 0) {
    return 1;
  }

  // Clear DMEM
  for (int i = 0; i < 32; i++)
    write_dmem64(i, 0);

  // Run RV32I tests
  int pass = 0;
  pass += test_basic_alu();
  pass += test_bne();
  pass += test_blt();
  pass += test_bltu();
  pass += test_bge();
  pass += test_bgeu();
  pass += test_shifts();
  pass += test_imm_shifts();
  pass += test_slt();
  pass += test_jal();
  pass += test_jalr();
  pass += test_lui();
  pass += test_auipc();
  pass += test_byte_ops();
  pass += test_halfword_ops();

  printf("=== RV32I Summary: %d/15 ===\n\n", pass);

  // Run debug module tests
  int sniffer_pass = test_bus_sniffer();
  int cpulog_pass = test_cpu_logger();

  // Print performance counters from last test
  print_perf_counters();

  // Final summary
  printf("=== Final Summary ===\n");
  printf("  RV32I Tests: %d/15\n", pass);
  printf("  Bus Sniffer: %s\n", sniffer_pass ? "PASS" : "FAIL");
  printf("  CPU Logger:  %s\n", cpulog_pass ? "PASS" : "FAIL");

  int all_pass = (pass == 15) && sniffer_pass && cpulog_pass;
  printf("\n%s\n",
         all_pass ? "=== ALL TESTS PASSED ===" : "=== SOME TESTS FAILED ===");

  vfio_cleanup();
  return all_pass ? 0 : 1;
}
