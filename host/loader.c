// RISC-V Binary Loader and Runner
// Loads a .bin file to IMEM and runs it

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/vfio.h>

// BAR Memory Map
#define BAR_CTRL      0x0000
#define BAR_STATUS    0x0008
#define BAR_PC        0x0010
#define BAR_CYCLES    0x0020
#define BAR_IMEM      0x20000
#define BAR_DMEM      0x80000

#define CTRL_RUN      (1 << 0)
#define CTRL_RESET    (1 << 1)

// Global state
static volatile uint64_t *bar64 = NULL;
static int container_fd = -1;
static int group_fd = -1;
static int device_fd = -1;

void write64(uint32_t offset, uint64_t value) { bar64[offset / 8] = value; }
uint64_t read64(uint32_t offset) { return bar64[offset / 8]; }

void write32(uint32_t offset, uint32_t value) { write64(offset, value); }
uint32_t read32(uint32_t offset) {
    uint64_t qword = read64(offset & ~7);
    return (offset & 4) ? (uint32_t)(qword >> 32) : (uint32_t)qword;
}

void cpu_reset(void) { write32(BAR_CTRL, CTRL_RESET); usleep(1000); }
void cpu_run(void) { write32(BAR_CTRL, CTRL_RUN); }
void cpu_stop(void) { write32(BAR_CTRL, 0); }

void write_imem_pair(uint32_t pair_idx, uint32_t even, uint32_t odd) {
    write64(BAR_IMEM + pair_idx * 8, ((uint64_t)odd << 32) | even);
}

uint32_t read_dmem(uint32_t word_idx) {
    uint64_t data = read64(BAR_DMEM + (word_idx / 2) * 8);
    return (word_idx & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
}

int vfio_init(const char *pci_addr, int iommu_group) {
    char group_path[64];
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
    struct vfio_region_info region_info = { 
        .argsz = sizeof(region_info),
        .index = VFIO_PCI_BAR0_REGION_INDEX
    };

    container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) { perror("open vfio"); return -1; }

    if (ioctl(container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        fprintf(stderr, "VFIO version mismatch\n"); return -1;
    }

    snprintf(group_path, sizeof(group_path), "/dev/vfio/%d", iommu_group);
    group_fd = open(group_path, O_RDWR);
    if (group_fd < 0) { perror("open group"); return -1; }

    if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) < 0) {
        perror("group status"); return -1;
    }
    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "Group not viable\n"); return -1;
    }

    ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd);
    ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);

    device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_addr);
    if (device_fd < 0) { perror("device fd"); return -1; }

    ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);

    bar64 = mmap(NULL, region_info.size, PROT_READ | PROT_WRITE,
                 MAP_SHARED, device_fd, region_info.offset);
    if (bar64 == MAP_FAILED) { perror("mmap"); return -1; }

    printf("BAR0 mapped at %p, size %llu bytes\n", bar64, region_info.size);
    return 0;
}

int load_binary(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("open binary");
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("Loading %s (%ld bytes)...\n", filename, size);

    // Read file into buffer
    uint32_t *buf = malloc((size + 3) & ~3);  // Round up to 4 bytes
    memset(buf, 0, (size + 3) & ~3);
    fread(buf, 1, size, f);
    fclose(f);

    // Show first few instructions
    printf("First 8 instructions:\n");
    int num_words = (size + 3) / 4;
    for (int i = 0; i < 8 && i < num_words; i++) {
        printf("  [%02d] 0x%08X\n", i, buf[i]);
    }

    // Load to IMEM (in pairs of 32-bit words)
    for (int i = 0; i < num_words; i += 2) {
        uint32_t even = buf[i];
        uint32_t odd = (i + 1 < num_words) ? buf[i + 1] : 0x00000013;  // NOP
        write_imem_pair(i / 2, even, odd);
    }

    // Verify by reading back
    printf("Verify IMEM readback:\n");
    for (int i = 0; i < 8 && i < num_words; i++) {
        uint32_t offset = BAR_IMEM + (i & ~1) * 4;
        uint64_t data = read64(offset);
        uint32_t word = (i & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
        printf("  [%02d] 0x%08X %s\n", i, word, (word == buf[i]) ? "OK" : "MISMATCH!");
    }

    free(buf);
    printf("Loaded %d instructions to IMEM\n", num_words);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *pci_addr = "0000:31:00.0";
    int iommu_group = 52;
    const char *binary = NULL;
    int run_time_ms = 10;  // Default 10ms

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strstr(argv[i], ".bin")) {
            binary = argv[i];
        } else if (strstr(argv[i], ":")) {
            pci_addr = argv[i];
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            if (strchr(argv[i], '.')) {
                // It's a PCI address
                pci_addr = argv[i];
            } else {
                // It's a number - either IOMMU group or run time
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

    // Clear DMEM
    printf("Clearing DMEM...\n");
    for (int i = 0; i < 256; i++) {
        write64(BAR_DMEM + i * 8, 0);
    }

    // Load binary
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
    uint32_t instrs = read32(0x24);

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

    // Check expected values for sum.bin
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

    return 0;
}
