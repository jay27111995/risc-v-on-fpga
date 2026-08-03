// RISC-V SoC Host Controller using VFIO
// Loads program, runs CPU, reads results via PCIe BAR

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/vfio.h>

// BAR Memory Map
#define BAR_CTRL      0x0000   // [0] RUN, [1] RESET
#define BAR_STATUS    0x0008   // [0] RUNNING
#define BAR_PC        0x0010   // Current PC
#define BAR_IMEM      0x1000   // Instruction memory (4KB)
#define BAR_DMEM      0x2000   // Data memory (8KB)
#define BAR_SNIFFER   0x4000   // Bus sniffer logs
#define BAR_CPULOG    0x5000   // CPU logger logs

// Control bits
#define CTRL_RUN      (1 << 0)
#define CTRL_RESET    (1 << 1)

// Global state
static volatile uint64_t *bar64 = NULL;
static int container_fd = -1;
static int group_fd = -1;
static int device_fd = -1;

// Register access
void write64(uint32_t offset, uint64_t value) {
    bar64[offset / 8] = value;
}

uint64_t read64(uint32_t offset) {
    return bar64[offset / 8];
}

void write32(uint32_t offset, uint32_t value) {
    write64(offset, value);
}

uint32_t read32(uint32_t offset) {
    uint64_t qword = read64(offset & ~7);  // Align to 8 bytes
    if (offset & 4) {
        return (uint32_t)(qword >> 32);  // Upper 32 bits
    } else {
        return (uint32_t)qword;           // Lower 32 bits
    }
}

// Control functions
void cpu_reset(void) {
    write32(BAR_CTRL, CTRL_RESET);
    usleep(1000);
}

void cpu_run(void) {
    write32(BAR_CTRL, CTRL_RUN);
}

void cpu_stop(void) {
    write32(BAR_CTRL, 0);
}

uint32_t cpu_get_pc(void) {
    return read32(BAR_PC);
}

// Memory access
void write_imem_pair(uint32_t pair_idx, uint32_t even_word, uint32_t odd_word) {
    uint32_t offset = BAR_IMEM + pair_idx * 8;
    uint64_t data = ((uint64_t)odd_word << 32) | even_word;
    write64(offset, data);
}

uint32_t read_imem(uint32_t word_idx) {
    uint32_t offset = BAR_IMEM + (word_idx & ~1) * 4;
    uint64_t data = read64(offset);
    return (word_idx & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
}

uint64_t read_dmem64(uint32_t idx) {
    return read64(BAR_DMEM + idx * 8);
}

void write_dmem64(uint32_t idx, uint64_t value) {
    write64(BAR_DMEM + idx * 8, value);
}

uint32_t read_dmem(uint32_t word_idx) {
    uint64_t data = read_dmem64(word_idx / 2);
    return (word_idx & 1) ? (uint32_t)(data >> 32) : (uint32_t)data;
}

void load_program(const uint32_t *program, size_t count) {
    for (size_t i = 0; i + 1 < count; i += 2) {
        write_imem_pair(i / 2, program[i], program[i + 1]);
    }
    if (count & 1) {
        write_imem_pair(count / 2, program[count - 1], 0x00000013);
    }
}

// VFIO setup
int vfio_init(const char *pci_addr, int iommu_group) {
    char group_path[64];
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
    struct vfio_region_info region_info = { 
        .argsz = sizeof(region_info),
        .index = VFIO_PCI_BAR0_REGION_INDEX
    };

    container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) {
        perror("Failed to open /dev/vfio/vfio");
        return -1;
    }

    if (ioctl(container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        fprintf(stderr, "VFIO API version mismatch\n");
        return -1;
    }

    if (!ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        fprintf(stderr, "VFIO Type1 IOMMU not supported\n");
        return -1;
    }

    snprintf(group_path, sizeof(group_path), "/dev/vfio/%d", iommu_group);
    group_fd = open(group_path, O_RDWR);
    if (group_fd < 0) {
        perror("Failed to open VFIO group");
        return -1;
    }

    if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) < 0) {
        perror("Failed to get group status");
        return -1;
    }
    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "VFIO group not viable\n");
        return -1;
    }

    if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) < 0) {
        perror("Failed to set container");
        return -1;
    }

    if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
        perror("Failed to set IOMMU");
        return -1;
    }

    device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_addr);
    if (device_fd < 0) {
        perror("Failed to get device FD");
        return -1;
    }

    if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info) < 0) {
        perror("Failed to get device info");
        return -1;
    }

    if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info) < 0) {
        perror("Failed to get BAR0 info");
        return -1;
    }

    bar64 = mmap(NULL, region_info.size, PROT_READ | PROT_WRITE,
                 MAP_SHARED, device_fd, region_info.offset);
    if (bar64 == MAP_FAILED) {
        perror("Failed to mmap BAR0");
        return -1;
    }

    printf("BAR0 mapped at %p, size %llu bytes\n", bar64, region_info.size);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *pci_addr = "0000:31:00.0";
    int iommu_group = 52;

    if (argc >= 2) pci_addr = argv[1];
    if (argc >= 3) iommu_group = atoi(argv[2]);

    printf("RISC-V SoC Test\n");
    printf("===============\n");
    printf("PCI: %s, IOMMU group: %d\n\n", pci_addr, iommu_group);

    if (vfio_init(pci_addr, iommu_group) < 0) {
        return 1;
    }

    // =========================================================================
    // Test 1: Basic ALU (x1=5, x2=3, x3=x1+x2, store to dmem[0])
    // =========================================================================
    printf("\n=== Test 1: Basic ALU ===\n");
    uint32_t program1[] = {
        0x00500093,  // ADDI x1, x0, 5
        0x00300113,  // ADDI x2, x0, 3
        0x002081B3,  // ADD  x3, x1, x2
        0x00302023,  // SW   x3, 0(x0)      ; dmem[0] = 8
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    
    // Stop and reset CPU
    write32(BAR_CTRL, 0x00);
    usleep(1000);
    write32(BAR_CTRL, 0x02);  // RESET
    usleep(1000);
    
    // Clear DMEM test area
    for (int i = 0; i < 8; i++) write_dmem64(i, 0);
    
    printf("Loading program...\n");
    load_program(program1, sizeof(program1)/sizeof(program1[0]));
    
    printf("Running CPU...\n");
    cpu_run();
    usleep(10000);  // 10ms
    cpu_stop();
    
    uint32_t result1 = read_dmem(0);
    printf("  DMEM[0] = %d (expected 8)\n", result1);
    int test1_pass = (result1 == 8);
    printf("  Test 1: %s\n", test1_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 2: BNE - Count down from 5 to 0
    // =========================================================================
    printf("\n=== Test 2: BNE (count down) ===\n");
    uint32_t program2[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFF08093,  // ADDI x1, x1, -1     ; x1 = x1 - 1
        0xFE1010E3,  // BNE  x1, x0, -4     ; if (x1 != 0) goto -4
        0x00102223,  // SW   x1, 4(x0)      ; dmem[4] = 0
        0x00000063,  // BEQ  x0, x0, 0      ; infinite loop
    };
    
    write32(BAR_CTRL, 0x02);  // RESET
    usleep(1000);
    load_program(program2, sizeof(program2)/sizeof(program2[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t result2 = read_dmem(1);  // dmem[4] = word index 1
    printf("  DMEM[4] = %d (expected 0)\n", result2);
    int test2_pass = (result2 == 0);
    printf("  Test 2: %s\n", test2_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 3: BLT (signed) - (-5) < 3 should be true
    // =========================================================================
    printf("\n=== Test 3: BLT (signed comparison) ===\n");
    uint32_t program3[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020C463,  // BLT  x1, x2, 8      ; if (x1 < x2 signed) skip next
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302423,  // SW   x3, 8(x0)      ; dmem[8] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program3, sizeof(program3)/sizeof(program3[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t result3 = read_dmem(2);  // dmem[8] = word index 2
    printf("  DMEM[8] = %d (expected 1, BLT taken because -5 < 3)\n", result3);
    int test3_pass = (result3 == 1);
    printf("  Test 3: %s\n", test3_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 4: BLTU (unsigned) - 0xFFFFFFFB < 3 should be FALSE
    // =========================================================================
    printf("\n=== Test 4: BLTU (unsigned comparison) ===\n");
    uint32_t program4[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = 0xFFFFFFFB
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020E463,  // BLTU x1, x2, 8      ; if (x1 < x2 unsigned) skip
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302623,  // SW   x3, 12(x0)     ; dmem[12] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program4, sizeof(program4)/sizeof(program4[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t result4 = read_dmem(3);  // dmem[12] = word index 3
    printf("  DMEM[12] = %d (expected 2, BLTU not taken because 0xFFFFFFFB > 3)\n", result4);
    int test4_pass = (result4 == 2);
    printf("  Test 4: %s\n", test4_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 5: BGE (signed) - 5 >= -3 should be TRUE
    // =========================================================================
    printf("\n=== Test 5: BGE (signed comparison) ===\n");
    uint32_t program5[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFD00113,  // ADDI x2, x0, -3     ; x2 = -3
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020D463,  // BGE  x1, x2, 8      ; if (x1 >= x2 signed) skip
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302823,  // SW   x3, 16(x0)     ; dmem[16] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program5, sizeof(program5)/sizeof(program5[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t result5 = read_dmem(4);  // dmem[16] = word index 4
    printf("  DMEM[16] = %d (expected 1, BGE taken because 5 >= -3)\n", result5);
    int test5_pass = (result5 == 1);
    printf("  Test 5: %s\n", test5_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 6: BGEU (unsigned) - 5 >= 0xFFFFFFFD should be FALSE
    // =========================================================================
    printf("\n=== Test 6: BGEU (unsigned comparison) ===\n");
    uint32_t program6[] = {
        0x00500093,  // ADDI x1, x0, 5      ; x1 = 5
        0xFFD00113,  // ADDI x2, x0, -3     ; x2 = 0xFFFFFFFD
        0x00100193,  // ADDI x3, x0, 1      ; x3 = 1 (assume taken)
        0x0020F463,  // BGEU x1, x2, 8      ; if (x1 >= x2 unsigned) skip
        0x00200193,  // ADDI x3, x0, 2      ; x3 = 2 (not taken)
        0x00302A23,  // SW   x3, 20(x0)     ; dmem[20] = x3
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program6, sizeof(program6)/sizeof(program6[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t result6 = read_dmem(5);  // dmem[20] = word index 5
    printf("  DMEM[20] = %d (expected 2, BGEU not taken because 5 < 0xFFFFFFFD)\n", result6);
    int test6_pass = (result6 == 2);
    printf("  Test 6: %s\n", test6_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Summary
    // =========================================================================
    printf("\n=== Test Summary ===\n");
    int total_pass = test1_pass + test2_pass + test3_pass + test4_pass + test5_pass + test6_pass;
    printf("  Passed: %d/6\n", total_pass);
    
    if (total_pass == 6) {
        printf("\n=== ALL TESTS PASSED ===\n");
    } else {
        printf("\n=== SOME TESTS FAILED ===\n");
    }
    
    // =========================================================================
    // Read Performance Counters (from last test)
    // =========================================================================
    printf("\n=== Performance Counters (last test) ===\n");
    uint32_t cycles   = read32(0x20);
    uint32_t instrs   = read32(0x24);
    uint32_t stalls   = read32(0x28);
    uint32_t branches = read32(0x2C);
    uint32_t br_taken = read32(0x30);
    uint32_t loads    = read32(0x34);
    uint32_t stores   = read32(0x38);
    
    printf("  Cycles:         %u\n", cycles);
    printf("  Instructions:   %u\n", instrs);
    printf("  Stalls:         %u\n", stalls);
    printf("  Branches:       %u\n", branches);
    printf("  Branches taken: %u\n", br_taken);
    printf("  Loads:          %u\n", loads);
    printf("  Stores:         %u\n", stores);
    
    if (instrs > 0) {
        float cpi = (float)cycles / instrs;
        float ipc = (float)instrs / cycles;
        printf("\n  CPI: %.2f\n", cpi);
        printf("  IPC: %.2f\n", ipc);
    }

    return (total_pass == 6) ? 0 : 1;
}
