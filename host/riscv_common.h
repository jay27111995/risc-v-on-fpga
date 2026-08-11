// RISC-V SoC Common Definitions
// ============================================================================
// Shared between all host test programs.
//
// BAR Memory Map:
//   0x00000 - 0x000FF : Control registers (CTRL, STATUS, PC, CYCLES, etc.)
//   0x20000 - 0x3FFFF : IMEM - 128KB instruction memory
//   0x40000 - 0x40FFF : Bus Sniffer (host transaction logger)
//   0x50000 - 0x50FFF : CPU Logger (CPU memory access logger)
//   0x80000 - 0x87FFF : DMEM - 32KB data memory
// ============================================================================

#ifndef RISCV_COMMON_H
#define RISCV_COMMON_H

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
// Bus Sniffer Registers
// ----------------------------------------------------------------------------

#define SNIFF_COUNT 0x0000
#define SNIFF_CYCLE 0x0004
#define SNIFF_CTRL 0x0008   // [0]=enable, [1]=clear
#define SNIFF_ENTRY0 0x0010

typedef struct {
  uint64_t timestamp;
  uint32_t address;
  uint32_t data;
  int is_write;
} sniffer_entry_t;

// ----------------------------------------------------------------------------
// CPU Logger Registers
// ----------------------------------------------------------------------------

#define CPULOG_COUNT 0x0000
#define CPULOG_CYCLE 0x0004
#define CPULOG_CTRL 0x0008  // [0]=enable, [1]=clear, [2]=log_imem
#define CPULOG_ENTRY0 0x0010

#define CPULOG_TYPE_IFETCH 0
#define CPULOG_TYPE_DLOAD 1
#define CPULOG_TYPE_DSTORE 2

typedef struct {
  uint64_t timestamp;
  uint32_t address;
  uint32_t data;
  int type;
} cpulog_entry_t;

static const char *cpulog_type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};

// ----------------------------------------------------------------------------
// Memory Access Helpers (uses read32/write32 from pcie_vfio.h)
// ----------------------------------------------------------------------------

static inline void write_imem(uint32_t word_idx, uint32_t value) {
  write32(BAR_IMEM + word_idx * 4, value);
}

static inline uint32_t read_imem(uint32_t word_idx) {
  return read32(BAR_IMEM + word_idx * 4);
}

static inline void write_dmem(uint32_t word_idx, uint32_t value) {
  write32(BAR_DMEM + word_idx * 4, value);
}

static inline uint32_t read_dmem(uint32_t word_idx) {
  return read32(BAR_DMEM + word_idx * 4);
}

// ----------------------------------------------------------------------------
// CPU Control
// ----------------------------------------------------------------------------

static inline void cpu_reset(void) {
  write32(BAR_CTRL, CTRL_RESET);
  usleep(1000);
  write32(BAR_CTRL, 0);
  usleep(1000);
}

static inline void cpu_run(void) { write32(BAR_CTRL, CTRL_RUN); }

static inline void cpu_stop(void) { write32(BAR_CTRL, 0); }

static inline int cpu_is_halted(void) {
  return (read32(BAR_STATUS) & 0x2) != 0;
}

static inline int cpu_wait_halt(int timeout_ms) {
  for (int i = 0; i < timeout_ms * 10; i++) {
    if (cpu_is_halted())
      return 1;
    usleep(100);
  }
  return 0;
}

// ----------------------------------------------------------------------------
// Bus Sniffer Functions
// ----------------------------------------------------------------------------

static inline uint32_t sniffer_get_count(void) {
  return read32(BAR_SNIFFER + SNIFF_COUNT);
}

static inline void sniffer_clear(void) {
  write32(BAR_SNIFFER + SNIFF_CTRL, 0x03); // clear + enable
  usleep(1000);
  write32(BAR_SNIFFER + SNIFF_CTRL, 0x01); // enable only
}

static inline void sniffer_read_entry(int idx, sniffer_entry_t *entry) {
  uint32_t base = BAR_SNIFFER + SNIFF_ENTRY0 + idx * 0x10;
  uint32_t w0 = read32(base + 0x0);
  uint32_t w1 = read32(base + 0x4);
  uint32_t w2 = read32(base + 0x8);
  uint32_t w3 = read32(base + 0xC);

  entry->is_write = w0 & 1;
  entry->address = ((w0 >> 1) & 0x7FFFF) << 1;  // Restore full address
  entry->timestamp = ((uint64_t)w2 << 32) | w1;
  entry->data = w3;
}

static inline void sniffer_print_entry(int idx, const sniffer_entry_t *entry,
                                       uint64_t base_cycle) {
  uint64_t offset = entry->timestamp - base_cycle;
  uint64_t offset_ns = CYCLES_TO_NS(offset);
  printf("    [%2d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n", idx,
         offset, offset_ns, entry->is_write ? "WR" : "RD", entry->address,
         entry->data);
}

static inline void sniffer_dump(int max_entries) {
  uint32_t count = sniffer_get_count();
  if (count == 0) {
    printf("  (no entries)\n");
    return;
  }
  int n = (count < (uint32_t)max_entries) ? count : max_entries;

  sniffer_entry_t entries[64];
  for (int i = 0; i < n; i++) {
    sniffer_read_entry(i, &entries[i]);
  }

  // Find minimum timestamp as base
  uint64_t base_cycle = entries[0].timestamp;
  for (int i = 1; i < n; i++) {
    if (entries[i].timestamp < base_cycle)
      base_cycle = entries[i].timestamp;
  }

  for (int i = 0; i < n; i++) {
    sniffer_print_entry(i, &entries[i], base_cycle);
  }
}

// ----------------------------------------------------------------------------
// CPU Logger Functions
// ----------------------------------------------------------------------------

static inline uint32_t cpulog_get_count(void) {
  return read32(BAR_CPULOG + CPULOG_COUNT);
}

static inline void cpulog_clear(void) {
  write32(BAR_CPULOG + CPULOG_CTRL, 0x03); // clear + enable
  usleep(1000);
  write32(BAR_CPULOG + CPULOG_CTRL, 0x01); // enable, log_imem=0
}

static inline void cpulog_clear_with_imem(void) {
  write32(BAR_CPULOG + CPULOG_CTRL, 0x03); // clear + enable
  usleep(1000);
  write32(BAR_CPULOG + CPULOG_CTRL, 0x05); // enable + log_imem
}

static inline void cpulog_read_entry(int idx, cpulog_entry_t *entry) {
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

static inline void cpulog_print_entry(int idx, const cpulog_entry_t *entry,
                                      uint64_t base_cycle) {
  uint64_t offset = entry->timestamp - base_cycle;
  uint64_t offset_ns = CYCLES_TO_NS(offset);
  printf("    [%3d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n", idx,
         offset, offset_ns, cpulog_type_names[entry->type & 3], entry->address,
         entry->data);
}

static inline void cpulog_dump(int max_entries) {
  uint32_t count = cpulog_get_count();
  if (count == 0) {
    printf("  (no entries)\n");
    return;
  }
  int n = (count < (uint32_t)max_entries) ? count : max_entries;

  cpulog_entry_t entries[256];
  for (int i = 0; i < n; i++) {
    cpulog_read_entry(i, &entries[i]);
  }

  // Find minimum timestamp as base
  uint64_t base_cycle = entries[0].timestamp;
  for (int i = 1; i < n; i++) {
    if (entries[i].timestamp < base_cycle)
      base_cycle = entries[i].timestamp;
  }

  for (int i = 0; i < n; i++) {
    cpulog_print_entry(i, &entries[i], base_cycle);
  }
}

// ----------------------------------------------------------------------------
// Performance Counters
// ----------------------------------------------------------------------------

static inline void print_perf_counters(void) {
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
// Program Loading
// ----------------------------------------------------------------------------

static inline int load_program_file(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    printf("  ERROR: Cannot open %s\n", filename);
    return -1;
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  printf("  Loading %s (%ld bytes, %ld instructions)\n", filename, size,
         size / 4);

  uint32_t *prog = malloc(size);
  if (!prog) {
    fclose(f);
    return -1;
  }

  if (fread(prog, 1, size, f) != (size_t)size) {
    printf("  ERROR: Failed to read %s\n", filename);
    free(prog);
    fclose(f);
    return -1;
  }
  fclose(f);

  for (long i = 0; i < size / 4; i++) {
    write_imem(i, prog[i]);
  }
  free(prog);

  return size / 4;
}

// ----------------------------------------------------------------------------
// Common Init/Cleanup
// ----------------------------------------------------------------------------

static inline int common_init(int argc, char *argv[], const char *prog_name) {
  const char *pci_addr = "0000:b1:00.0";
  int iommu_group = 52;

  if (argc >= 2)
    pci_addr = argv[1];
  if (argc >= 3)
    iommu_group = atoi(argv[2]);

  printf("%s\n", prog_name);
  printf("================================================================================\n");
  printf("PCI: %s, IOMMU group: %d\n\n", pci_addr, iommu_group);

  if (vfio_init(pci_addr, iommu_group) < 0) {
    fprintf(stderr, "Failed to initialize VFIO\n");
    return -1;
  }

  return 0;
}

static inline void common_cleanup(void) { vfio_cleanup(); }

#endif // RISCV_COMMON_H
