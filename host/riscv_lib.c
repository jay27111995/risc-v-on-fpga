// RISC-V SoC Library - Implementation
// ============================================================================

#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

const char *cpulog_type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};

// ----------------------------------------------------------------------------
// Memory Access
// ----------------------------------------------------------------------------

void write_imem(uint32_t word_idx, uint32_t value) {
    write32(BAR_IMEM + word_idx * 4, value);
}

uint32_t read_imem(uint32_t word_idx) {
    return read32(BAR_IMEM + word_idx * 4);
}

void write_dmem(uint32_t word_idx, uint32_t value) {
    write32(BAR_DMEM + word_idx * 4, value);
}

uint32_t read_dmem(uint32_t word_idx) {
    return read32(BAR_DMEM + word_idx * 4);
}

void init_imem(void) {
    uint64_t ebreak_pair = ((uint64_t)EBREAK_INSTR << 32) | EBREAK_INSTR;
    for (int i = 0; i < IMEM_SIZE_WORDS / 2; i++) {
        write64(BAR_IMEM + i * 8, ebreak_pair);
    }
}

void init_dmem(void) {
    for (int i = 0; i < DMEM_SIZE_WORDS / 2; i++) {
        write64(BAR_DMEM + i * 8, 0);
    }
}

void init_memory(void) {
    init_imem();
    init_dmem();
    sniffer_clear();
    cpulog_clear();
}

// ----------------------------------------------------------------------------
// CPU Control
// ----------------------------------------------------------------------------

void cpu_reset(void) {
    write32(BAR_CTRL, CTRL_RESET);
    usleep(1000);
    write32(BAR_CTRL, 0);
    usleep(1000);
}

void cpu_run(void) {
    write32(BAR_CTRL, CTRL_RUN);
}

void cpu_stop(void) {
    write32(BAR_CTRL, 0);
}

int cpu_is_halted(void) {
    return (read32(BAR_STATUS) & 0x2) != 0;
}

int cpu_wait_halt(int timeout_ms) {
    for (int i = 0; i < timeout_ms * 10; i++) {
        if (cpu_is_halted())
            return 1;
        usleep(100);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Bus Sniffer
// ----------------------------------------------------------------------------

uint32_t sniffer_get_count(void) {
    return read32(BAR_SNIFFER + SNIFF_COUNT);
}

void sniffer_clear(void) {
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x01);  // enable only
}

void sniffer_read_entry(int idx, sniffer_entry_t *entry) {
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

static void sniffer_print_entry(int idx, const sniffer_entry_t *entry,
                                uint64_t base_cycle) {
    uint64_t offset = entry->timestamp - base_cycle;
    uint64_t offset_ns = CYCLES_TO_NS(offset);
    printf("    [%2d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n",
           idx, offset, offset_ns, entry->is_write ? "WR" : "RD",
           entry->address, entry->data);
}

void sniffer_dump(int max_entries) {
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
// CPU Logger
// ----------------------------------------------------------------------------

uint32_t cpulog_get_count(void) {
    return read32(BAR_CPULOG + CPULOG_COUNT);
}

void cpulog_clear(void) {
    write32(BAR_CPULOG + CPULOG_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_CPULOG + CPULOG_CTRL, 0x01);  // enable, log_imem=0
}

void cpulog_clear_with_imem(void) {
    write32(BAR_CPULOG + CPULOG_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_CPULOG + CPULOG_CTRL, 0x05);  // enable + log_imem
}

void cpulog_read_entry(int idx, cpulog_entry_t *entry) {
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
    printf("    [%3d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n",
           idx, offset, offset_ns, cpulog_type_names[entry->type & 3],
           entry->address, entry->data);
}

void cpulog_dump(int max_entries) {
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

void print_perf_counters(void) {
    printf("=== Performance Counters ===\n");

    uint32_t cycles   = read32(BAR_CYCLES);
    uint32_t instrs   = read32(BAR_INSTRS);
    uint32_t stalls   = read32(BAR_STALLS);
    uint32_t branches = read32(BAR_BRANCHES);
    uint32_t br_taken = read32(BAR_BR_TAKEN);
    uint32_t loads    = read32(BAR_LOADS);
    uint32_t stores   = read32(BAR_STORES);

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

int load_program_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("  ERROR: Cannot open %s\n", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("  Loading %s (%ld bytes, %ld instructions)\n", filename, size, size / 4);

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
// Init/Cleanup
// ----------------------------------------------------------------------------

int common_init(int argc, char *argv[], const char *prog_name) {
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

void common_cleanup(void) {
    vfio_cleanup();
}
