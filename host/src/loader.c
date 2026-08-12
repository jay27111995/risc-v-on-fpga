// RISC-V Binary Loader and Runner
// ============================================================================
// Loads a .bin file to IMEM and runs it on the FPGA.
//
// Usage: ./loader <binary.bin> [pci_addr] [iommu_group] [run_time_ms]
// Example: ./loader ../sw/sum.bin 0000:b1:00.0 12
// ============================================================================

#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ----------------------------------------------------------------------------
// Binary Loader (local - uses 64-bit writes for efficiency)
// ----------------------------------------------------------------------------

static void write_imem_pair(uint32_t pair_idx, uint32_t even, uint32_t odd) {
    write64(BAR_IMEM + pair_idx * 8, ((uint64_t)odd << 32) | even);
}

static int load_binary(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("open binary");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("Loading %s (%ld bytes)...\n", filename, size);

    uint32_t *buf = malloc((size + 3) & ~3);
    memset(buf, 0, (size + 3) & ~3);
    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "Failed to read file\n");
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    // Show first few instructions
    printf("First 8 instructions:\n");
    int num_words = (size + 3) / 4;
    for (int i = 0; i < 8 && i < num_words; i++) {
        printf("  [%02d] 0x%08X\n", i, buf[i]);
    }

    // Initialize memory
    printf("Initializing memory...\n");
    init_memory();

    // Load to IMEM
    printf("Loading program...\n");
    for (int i = 0; i < num_words; i += 2) {
        uint32_t even = buf[i];
        uint32_t odd = (i + 1 < num_words) ? buf[i + 1] : EBREAK_INSTR;
        write_imem_pair(i / 2, even, odd);
    }

    // Verify readback
    printf("Verify IMEM readback:\n");
    for (int i = 0; i < 8 && i < num_words; i++) {
        uint32_t word = read_imem(i);
        printf("  [%02d] 0x%08X %s\n", i, word, (word == buf[i]) ? "OK" : "MISMATCH!");
    }

    free(buf);
    printf("Loaded %d instructions to IMEM\n", num_words);
    return 0;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    const char *pci_addr = "0000:31:00.0";
    int iommu_group = 52;
    const char *binary = NULL;
    int run_time_ms = 10;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strstr(argv[i], ".bin")) {
            binary = argv[i];
        } else if (strstr(argv[i], ":")) {
            pci_addr = argv[i];
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            if (strchr(argv[i], '.')) {
                pci_addr = argv[i];
            } else {
                int val = atoi(argv[i]);
                if (val < 100) {
                    iommu_group = val;
                } else {
                    run_time_ms = val;
                }
            }
        }
    }

    if (!binary) {
        printf("Usage: %s <binary.bin> [pci_addr] [iommu_group] [run_time_ms]\n", argv[0]);
        printf("Example: %s ../sw/sum.bin 0000:b1:00.0 12\n", argv[0]);
        return 1;
    }

    printf("RISC-V Binary Loader\n");
    printf("====================\n");
    printf("PCI: %s, IOMMU group: %d\n", pci_addr, iommu_group);
    printf("Binary: %s\n", binary);
    printf("Run time: %d ms\n\n", run_time_ms);

    if (vfio_init(pci_addr, iommu_group) < 0) {
        return 1;
    }

    // Reset and stop CPU
    cpu_stop();
    cpu_reset();

    // Load binary (also initializes memory)
    if (load_binary(binary) < 0) {
        return 1;
    }

    // Run CPU
    printf("\nRunning CPU for %d ms...\n", run_time_ms);
    cpu_run();
    usleep(run_time_ms * 1000);
    cpu_stop();

    // Read results
    uint32_t pc = read32(BAR_PC);
    uint32_t cycles = read32(BAR_CYCLES);
    uint32_t instrs = read32(BAR_INSTRS);

    printf("\n=== Results ===\n");
    printf("PC:     0x%08X\n", pc);
    printf("Cycles: %u\n", cycles);
    printf("Instrs: %u\n", instrs);

    printf("\n=== DMEM Contents ===\n");
    for (int i = 0; i < 16; i++) {
        uint32_t val = read_dmem(i);
        if (val != 0) {
            printf("  DMEM[%2d] = %10u (0x%08X)\n", i, val, val);
        }
    }

    // Check for completion marker
    uint32_t dmem0 = read_dmem(0);
    uint32_t dmem1 = read_dmem(1);
    uint32_t dmem2 = read_dmem(2);

    printf("\n=== Verification ===\n");
    if (dmem0 == 55 && dmem1 == 10 && dmem2 == 0xDEAD) {
        printf("sum.bin: PASSED (sum(1..10) = %u)\n", dmem0);
    } else if (dmem2 == 0xDEAD) {
        printf("Program completed (marker found)\n");
        printf("  DMEM[0] = %u\n", dmem0);
    } else {
        printf("Program may not have completed (no marker at DMEM[2])\n");
    }

    vfio_cleanup();
    return 0;
}
