// RISC-V ELF Loader
// ============================================================================
// Loads an ELF file, putting .text in IMEM and .rodata/.data in DMEM
// ============================================================================

#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <elf.h>

// ----------------------------------------------------------------------------
// ELF Loading
// ----------------------------------------------------------------------------

static int load_elf(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("open ELF");
        return -1;
    }

    // Read ELF header
    Elf32_Ehdr ehdr;
    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
        fprintf(stderr, "Failed to read ELF header\n");
        fclose(f);
        return -1;
    }

    // Verify ELF magic
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Not an ELF file\n");
        fclose(f);
        return -1;
    }

    // Verify RISC-V
    if (ehdr.e_machine != EM_RISCV) {
        fprintf(stderr, "Not a RISC-V ELF (machine=%d)\n", ehdr.e_machine);
        fclose(f);
        return -1;
    }

    printf("ELF: %d-bit RISC-V, %d program headers, %d section headers\n",
           ehdr.e_ident[EI_CLASS] == ELFCLASS32 ? 32 : 64,
           ehdr.e_phnum, ehdr.e_shnum);
    printf("Entry point: 0x%08X\n", ehdr.e_entry);

    // Initialize memory
    printf("Initializing memory...\n");
    init_memory();

    // Read section headers
    Elf32_Shdr *shdrs = malloc(ehdr.e_shnum * sizeof(Elf32_Shdr));
    fseek(f, ehdr.e_shoff, SEEK_SET);
    fread(shdrs, sizeof(Elf32_Shdr), ehdr.e_shnum, f);

    // Read section name string table
    Elf32_Shdr *shstrtab = &shdrs[ehdr.e_shstrndx];
    char *strtab = malloc(shstrtab->sh_size);
    fseek(f, shstrtab->sh_offset, SEEK_SET);
    fread(strtab, 1, shstrtab->sh_size, f);

    // Process each section
    for (int i = 0; i < ehdr.e_shnum; i++) {
        Elf32_Shdr *shdr = &shdrs[i];
        const char *name = strtab + shdr->sh_name;

        // Skip empty or non-loadable sections
        if (shdr->sh_size == 0 || shdr->sh_type == SHT_NULL) continue;
        if (!(shdr->sh_flags & SHF_ALLOC)) continue;

        printf("Section %-12s: addr=0x%08X size=0x%04X flags=0x%X",
               name, shdr->sh_addr, shdr->sh_size, shdr->sh_flags);

        // Read section data
        uint8_t *data = malloc(shdr->sh_size);
        if (shdr->sh_type != SHT_NOBITS) {
            fseek(f, shdr->sh_offset, SEEK_SET);
            fread(data, 1, shdr->sh_size, f);
        } else {
            memset(data, 0, shdr->sh_size);
        }

        // Determine where to load
        if (shdr->sh_flags & SHF_EXECINSTR) {
            // Executable -> IMEM
            printf(" -> IMEM\n");
            uint32_t *words = (uint32_t *)data;
            int num_words = (shdr->sh_size + 3) / 4;
            for (int w = 0; w < num_words; w += 2) {
                uint32_t addr = shdr->sh_addr + w * 4;
                uint32_t even = words[w];
                uint32_t odd = (w + 1 < num_words) ? words[w + 1] : 0x00100073; // EBREAK
                write64(BAR_IMEM + (addr / 4) * 4, ((uint64_t)odd << 32) | even);
            }
        } else {
            // Data -> DMEM
            printf(" -> DMEM\n");
            uint32_t *words = (uint32_t *)data;
            int num_words = (shdr->sh_size + 3) / 4;
            for (int w = 0; w < num_words; w++) {
                // VMA is 0x10000xxx, strip high bits to get DMEM offset
                uint32_t dmem_addr = (shdr->sh_addr & 0xFFFF) + w * 4;
                write_dmem(dmem_addr / 4, words[w]);
            }
        }

        free(data);
    }

    free(strtab);
    free(shdrs);
    fclose(f);

    printf("ELF loaded successfully\n");
    return 0;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    const char *pci_addr = "0000:31:00.0";
    int iommu_group = 52;
    const char *elffile = NULL;
    int run_time_ms = 10;
    int no_run = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-run") == 0) {
            no_run = 1;
        } else if (strstr(argv[i], ".elf")) {
            elffile = argv[i];
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

    if (!elffile) {
        printf("Usage: %s <program.elf> [pci_addr] [iommu_group] [run_time_ms]\n", argv[0]);
        return 1;
    }

    printf("RISC-V ELF Loader\n");
    printf("=================\n");
    printf("PCI: %s, IOMMU group: %d\n", pci_addr, iommu_group);
    printf("ELF: %s\n", elffile);
    printf("\n");

    if (vfio_init(pci_addr, iommu_group) < 0) {
        return 1;
    }

    // Stop and reset CPU
    cpu_stop();
    cpu_reset();

    // Load ELF
    if (load_elf(elffile) < 0) {
        vfio_cleanup();
        return 1;
    }

    // Clear UART pointers
    write_dmem(0x140/4, 0);  // TX_HEAD
    write_dmem(0x144/4, 0);  // TX_TAIL
    write_dmem(0x240/4, 0);  // RX_HEAD
    write_dmem(0x244/4, 0);  // RX_TAIL

    printf("\nUART pointers cleared\n");

    if (no_run) {
        printf("Program loaded (--no-run).\n");
    } else {
        printf("Running CPU for %d ms...\n", run_time_ms);
        cpu_run();
        usleep(run_time_ms * 1000);
        cpu_stop();

        uint32_t pc = read32(BAR_PC);
        uint32_t cycles = read32(BAR_CYCLES);
        uint32_t instrs = read32(BAR_INSTRS);

        printf("\n=== Results ===\n");
        printf("PC:     0x%08X\n", pc);
        printf("Cycles: %u\n", cycles);
        printf("Instrs: %u\n", instrs);

        printf("\nTX_HEAD: %u\n", read_dmem(0x140/4));
        printf("TX_TAIL: %u\n", read_dmem(0x144/4));
    }

    vfio_cleanup();
    return 0;
}
