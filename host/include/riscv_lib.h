// RISC-V SoC Library - Header
// ============================================================================
// Helper functions for RISC-V SoC host programs.
// ============================================================================

#ifndef RISCV_LIB_H
#define RISCV_LIB_H

#include <stdint.h>

#include "pcie_vfio.h"

// ----------------------------------------------------------------------------
// BAR Memory Map
// ----------------------------------------------------------------------------

// Control registers
#define BAR_CTRL     0x0000   // [0] RUN, [1] RESET
#define BAR_STATUS   0x0008   // [0] RUNNING, [1] HALTED
#define BAR_PC       0x0010   // Current PC
#define BAR_CYCLES   0x0020   // Cycle counter
#define BAR_INSTRS   0x0024   // Instruction counter
#define BAR_STALLS   0x0028   // Stall counter
#define BAR_BRANCHES 0x002C   // Branch counter
#define BAR_BR_TAKEN 0x0030   // Branches taken counter
#define BAR_LOADS    0x0034   // Load counter
#define BAR_STORES   0x0038   // Store counter

// Memory regions
#define BAR_IMEM    0x20000   // Instruction memory (128KB)
#define BAR_SNIFFER 0x40000   // Bus sniffer
#define BAR_CPULOG  0x50000   // CPU logger
#define BAR_DMEM    0x80000   // Data memory (32KB)

// Control bits
#define CTRL_RUN   (1 << 0)
#define CTRL_RESET (1 << 1)

// Memory sizes (from RTL)
#define IMEM_SIZE_WORDS 32768   // 128KB / 4 bytes
#define DMEM_SIZE_WORDS 8192    // 32KB / 4 bytes

// Special instructions
#define EBREAK_INSTR 0x00100073
#define NOP_INSTR    0x00000013

// Clock frequency (250 MHz = 4ns per cycle)
#define CLOCK_PERIOD_NS 4
#define CYCLES_TO_NS(c) ((c) * CLOCK_PERIOD_NS)

// ----------------------------------------------------------------------------
// Bus Sniffer
// ----------------------------------------------------------------------------

#define SNIFF_COUNT  0x0000
#define SNIFF_CYCLE  0x0004
#define SNIFF_CTRL   0x0008   // [0]=enable, [1]=clear
#define SNIFF_ENTRY0 0x0010

typedef struct {
    uint64_t timestamp;
    uint32_t address;
    uint32_t data;
    int is_write;
} sniffer_entry_t;

uint32_t sniffer_get_count(void);
void sniffer_clear(void);
void sniffer_read_entry(int idx, sniffer_entry_t *entry);
void sniffer_dump(int max_entries);

// ----------------------------------------------------------------------------
// CPU Logger
// ----------------------------------------------------------------------------

#define CPULOG_COUNT  0x0000
#define CPULOG_CYCLE  0x0004
#define CPULOG_CTRL   0x0008  // [0]=enable, [1]=clear, [2]=log_imem
#define CPULOG_ENTRY0 0x0010

#define CPULOG_TYPE_IFETCH 0
#define CPULOG_TYPE_DLOAD  1
#define CPULOG_TYPE_DSTORE 2

typedef struct {
    uint64_t timestamp;
    uint32_t address;
    uint32_t data;
    int type;
} cpulog_entry_t;

extern const char *cpulog_type_names[];

uint32_t cpulog_get_count(void);
void cpulog_clear(void);
void cpulog_clear_with_imem(void);
void cpulog_read_entry(int idx, cpulog_entry_t *entry);
void cpulog_dump(int max_entries);

// ----------------------------------------------------------------------------
// Memory Access
// ----------------------------------------------------------------------------

void write_imem(uint32_t word_idx, uint32_t value);
uint32_t read_imem(uint32_t word_idx);
void write_dmem(uint32_t word_idx, uint32_t value);
uint32_t read_dmem(uint32_t word_idx);

// Initialize memory (call before loading program)
void init_imem(void);   // Fill with EBREAK
void init_dmem(void);   // Fill with zeros
void init_memory(void); // Both

// ----------------------------------------------------------------------------
// CPU Control
// ----------------------------------------------------------------------------

void cpu_reset(void);
void cpu_run(void);
void cpu_stop(void);
int cpu_is_halted(void);
int cpu_wait_halt(int timeout_ms);

// ----------------------------------------------------------------------------
// Performance & Debug
// ----------------------------------------------------------------------------

void print_perf_counters(void);
int load_program_file(const char *filename);

// ----------------------------------------------------------------------------
// Init/Cleanup
// ----------------------------------------------------------------------------

int common_init(int argc, char *argv[], const char *prog_name);
void common_cleanup(void);

#endif // RISCV_LIB_H
