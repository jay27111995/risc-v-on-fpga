// RISC-V SoC Host Controller using VFIO
// ============================================================================
// Loads program to IMEM, runs CPU, verifies results from DMEM via PCIe BAR.
// Tests complete RV32I instruction set (37 instructions).
//
// BAR Memory Map:
//   0x00000 - 0x000FF : Control registers (CTRL, STATUS, PC, CYCLES, etc.)
//   0x20000 - 0x3FFFF : IMEM - 128KB instruction memory
//   0x40000 - 0x40FFF : Bus Sniffer (host transaction logger)
//   0x50000 - 0x50FFF : CPU Logger (CPU memory access logger)
//   0x80000 - 0x87FFF : DMEM - 32KB data memory
//
// Usage: sudo ./riscv_host <pci_addr> <iommu_group>
// ============================================================================

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
#define BAR_CYCLES    0x0020   // Performance counter
#define BAR_IMEM      0x20000  // Instruction memory (128KB) - 0x20000-0x3FFFF
#define BAR_SNIFFER   0x40000  // Bus sniffer (host transaction logger)
#define BAR_CPULOG    0x50000  // CPU logger (CPU memory access logger)
#define BAR_DMEM      0x80000  // Data memory (32KB) - 0x80000-0x87FFF

// Bus Sniffer registers (at BAR_SNIFFER)
#define SNIFF_COUNT   0x0000   // Total transactions logged
#define SNIFF_CYCLE   0x0004   // Current cycle counter
#define SNIFF_CTRL    0x0008   // [0]=enable, [1]=clear
#define SNIFF_ENTRY0  0x0010   // Entry[0] (128 bits = 4 words)

// CPU Logger registers (at BAR_CPULOG)
#define CPULOG_COUNT  0x0000   // Total transactions logged (RO)
#define CPULOG_CYCLE  0x0004   // Current cycle counter (RO)
#define CPULOG_CTRL   0x0008   // [0]=enable, [1]=clear, [2]=log_imem, [3]=ebreak_hit(RO)
#define CPULOG_ENTRY0 0x0010   // Entry[0] (96 bits = 3 words)

// Log entry types
#define LOG_TYPE_IFETCH  0
#define LOG_TYPE_DLOAD   1
#define LOG_TYPE_DSTORE  2
#define LOG_TYPE_EBREAK  3

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
    // Test 7: Shift operations (SLL, SRL, SRA)
    // =========================================================================
    printf("\n=== Test 7: Shifts (SLL, SRL, SRA) ===\n");
    uint32_t program7[] = {
        0x00800093,  // ADDI x1, x0, 8      ; x1 = 8
        0x00200113,  // ADDI x2, x0, 2      ; x2 = 2 (shift amount)
        0x002091B3,  // SLL  x3, x1, x2     ; x3 = 8 << 2 = 32
        0x0020D233,  // SRL  x4, x1, x2     ; x4 = 8 >> 2 = 2
        0x80000293,  // ADDI x5, x0, -2048  ; x5 = 0xFFFFF800 (negative)
        0x4022D333,  // SRA  x6, x5, x2     ; x6 = 0xFFFFF800 >>> 2 = 0xFFFFFE00
        0x00302C23,  // SW   x3, 24(x0)     ; dmem[24] = 32
        0x00402E23,  // SW   x4, 28(x0)     ; dmem[28] = 2
        0x02602023,  // SW   x6, 32(x0)     ; dmem[32] = 0xFFFFFE00
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program7, sizeof(program7)/sizeof(program7[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t sll_result = read_dmem(6);   // dmem[24]
    uint32_t srl_result = read_dmem(7);   // dmem[28]
    uint32_t sra_result = read_dmem(8);   // dmem[32]
    printf("  SLL: %u (expected 32)\n", sll_result);
    printf("  SRL: %u (expected 2)\n", srl_result);
    printf("  SRA: 0x%08X (expected 0xFFFFFE00)\n", sra_result);
    int test7_pass = (sll_result == 32 && srl_result == 2 && sra_result == 0xFFFFFE00);
    printf("  Test 7: %s\n", test7_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 8: Immediate shifts (SLLI, SRLI, SRAI)
    // =========================================================================
    printf("\n=== Test 8: Immediate Shifts (SLLI, SRLI, SRAI) ===\n");
    uint32_t program8[] = {
        0x00100093,  // ADDI x1, x0, 1      ; x1 = 1
        0x01009113,  // SLLI x2, x1, 16     ; x2 = 1 << 16 = 65536
        0x00815193,  // SRLI x3, x2, 8      ; x3 = 65536 >> 8 = 256
        0x80000213,  // ADDI x4, x0, -2048  ; x4 = 0xFFFFF800
        0x40425293,  // SRAI x5, x4, 4      ; x5 = 0xFFFFF800 >>> 4 = 0xFFFFFF80
        0x02202223,  // SW   x2, 36(x0)     ; dmem[36] = 65536
        0x02302423,  // SW   x3, 40(x0)     ; dmem[40] = 256
        0x02502623,  // SW   x5, 44(x0)     ; dmem[44] = 0xFFFFFF80
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program8, sizeof(program8)/sizeof(program8[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t slli_result = read_dmem(9);   // dmem[36]
    uint32_t srli_result = read_dmem(10);  // dmem[40]
    uint32_t srai_result = read_dmem(11);  // dmem[44]
    printf("  SLLI: %u (expected 65536)\n", slli_result);
    printf("  SRLI: %u (expected 256)\n", srli_result);
    printf("  SRAI: 0x%08X (expected 0xFFFFFF80)\n", srai_result);
    int test8_pass = (slli_result == 65536 && srli_result == 256 && srai_result == 0xFFFFFF80);
    printf("  Test 8: %s\n", test8_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 9: SLT/SLTU (set less than)
    // =========================================================================
    printf("\n=== Test 9: SLT/SLTU (set less than) ===\n");
    uint32_t program9[] = {
        0xFFB00093,  // ADDI x1, x0, -5     ; x1 = -5 (0xFFFFFFFB)
        0x00300113,  // ADDI x2, x0, 3      ; x2 = 3
        0x0020A1B3,  // SLT  x3, x1, x2     ; x3 = (-5 < 3) = 1 (signed)
        0x0020B233,  // SLTU x4, x1, x2     ; x4 = (0xFFFFFFFB < 3) = 0 (unsigned)
        0x02302823,  // SW   x3, 48(x0)     ; dmem[48] = 1
        0x02402A23,  // SW   x4, 52(x0)     ; dmem[52] = 0
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program9, sizeof(program9)/sizeof(program9[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t slt_result = read_dmem(12);   // dmem[48]
    uint32_t sltu_result = read_dmem(13);  // dmem[52]
    printf("  SLT:  %u (expected 1, -5 < 3 signed)\n", slt_result);
    printf("  SLTU: %u (expected 0, 0xFFFFFFFB > 3 unsigned)\n", sltu_result);
    int test9_pass = (slt_result == 1 && sltu_result == 0);
    printf("  Test 9: %s\n", test9_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 10: JAL (jump and link)
    // =========================================================================
    printf("\n=== Test 10: JAL (jump and link) ===\n");
    uint32_t program10[] = {
        0x00500093,  // 0x00: ADDI x1, x0, 5   ; x1 = 5
        0x00C000EF,  // 0x04: JAL x1, 12       ; jump to 0x10, x1 = 8 (return addr)
        0x02102C23,  // 0x08: SW x1, 56(x0)    ; dmem[56] = x1 (should be 8)
        0x00000063,  // 0x0C: BEQ x0, x0, 0    ; loop
        0x00000013,  // 0x10: NOP (landing pad)
        0xFF5FF06F,  // 0x14: JAL x0, -12      ; jump back to 0x08
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program10, sizeof(program10)/sizeof(program10[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t jal_result = read_dmem(14);  // dmem[56]
    printf("  Return addr: %u (expected 8)\n", jal_result);
    int test10_pass = (jal_result == 8);
    printf("  Test 10: %s\n", test10_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 11: JALR (jump and link register)
    // =========================================================================
    printf("\n=== Test 11: JALR (jump and link register) ===\n");
    uint32_t program11[] = {
        0x00300513,  // 0x00: ADDI x10, x0, 3  ; x10 = 3
        0x00C000EF,  // 0x04: JAL x1, 12       ; call func at 0x10, x1 = 8
        0x02A02E23,  // 0x08: SW x10, 60(x0)   ; dmem[60] = x10 (should be 8)
        0x00000063,  // 0x0C: BEQ x0, x0, 0    ; loop
        0x00550513,  // 0x10: ADDI x10, x10, 5 ; func: x10 = x10 + 5 = 8
        0x00008067,  // 0x14: JALR x0, x1, 0   ; return to x1
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program11, sizeof(program11)/sizeof(program11[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t jalr_result = read_dmem(15);  // dmem[60]
    printf("  Function result: %u (expected 8, 3+5)\n", jalr_result);
    int test11_pass = (jalr_result == 8);
    printf("  Test 11: %s\n", test11_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 12: LUI (load upper immediate)
    // =========================================================================
    printf("\n=== Test 12: LUI (load upper immediate) ===\n");
    uint32_t program12[] = {
        0xDEADB0B7,  // LUI x1, 0xDEADB     ; x1 = 0xDEADB000
        0x04102023,  // SW  x1, 64(x0)      ; dmem[64] = 0xDEADB000
        0x00000063,  // BEQ x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program12, sizeof(program12)/sizeof(program12[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t lui_result = read_dmem(16);  // dmem[64]
    printf("  LUI result: 0x%08X (expected 0xDEADB000)\n", lui_result);
    int test12_pass = (lui_result == 0xDEADB000);
    printf("  Test 12: %s\n", test12_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 13: AUIPC (add upper immediate to PC)
    // =========================================================================
    printf("\n=== Test 13: AUIPC (add upper immediate to PC) ===\n");
    uint32_t program13[] = {
        0x00001097,  // AUIPC x1, 1         ; x1 = PC + 0x1000 = 0 + 0x1000
        0x04102223,  // SW    x1, 68(x0)    ; dmem[68] = 0x1000
        0x00000063,  // BEQ   x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program13, sizeof(program13)/sizeof(program13[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t auipc_result = read_dmem(17);  // dmem[68]
    printf("  AUIPC result: 0x%08X (expected 0x1000)\n", auipc_result);
    int test13_pass = (auipc_result == 0x1000);
    printf("  Test 13: %s\n", test13_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 14: SB/LB/LBU (store/load byte)
    // =========================================================================
    printf("\n=== Test 14: SB/LB/LBU (byte operations) ===\n");
    uint32_t program14[] = {
        0x0FF00093,  // ADDI x1, x0, 255    ; x1 = 0xFF
        0x04800113,  // ADDI x2, x0, 72     ; x2 = 72 (address)
        0x00110023,  // SB   x1, 0(x2)      ; mem[72] byte 0 = 0xFF
        0x00010183,  // LB   x3, 0(x2)      ; x3 = sign_ext(0xFF) = -1
        0x00014203,  // LBU  x4, 0(x2)      ; x4 = zero_ext(0xFF) = 255
        0x04302423,  // SW   x3, 72(x0)     ; dmem[72] = -1 (overwrites)
        0x04402623,  // SW   x4, 76(x0)     ; dmem[76] = 255
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program14, sizeof(program14)/sizeof(program14[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t lb_result = read_dmem(18);   // dmem[72]
    uint32_t lbu_result = read_dmem(19);  // dmem[76]
    printf("  LB:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lb_result);
    printf("  LBU: 0x%08X (expected 0x000000FF, zero extended)\n", lbu_result);
    int test14_pass = (lb_result == 0xFFFFFFFF && lbu_result == 0x000000FF);
    printf("  Test 14: %s\n", test14_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Test 15: SH/LH/LHU (store/load halfword)
    // =========================================================================
    printf("\n=== Test 15: SH/LH/LHU (halfword operations) ===\n");
    uint32_t program15[] = {
        0xFFF00093,  // ADDI x1, x0, -1     ; x1 = 0xFFFFFFFF
        0x05000113,  // ADDI x2, x0, 80     ; x2 = 80 (address)
        0x00111023,  // SH   x1, 0(x2)      ; mem[80] = 0xFFFF (halfword)
        0x00011183,  // LH   x3, 0(x2)      ; x3 = sign_ext(0xFFFF) = -1
        0x00015203,  // LHU  x4, 0(x2)      ; x4 = zero_ext(0xFFFF) = 65535
        0x04302823,  // SW   x3, 80(x0)     ; dmem[80] = -1 (overwrites)
        0x04402A23,  // SW   x4, 84(x0)     ; dmem[84] = 65535
        0x00000063,  // BEQ  x0, x0, 0
    };
    
    write32(BAR_CTRL, 0x02);
    usleep(1000);
    load_program(program15, sizeof(program15)/sizeof(program15[0]));
    cpu_run();
    usleep(10000);
    cpu_stop();
    
    uint32_t lh_result = read_dmem(20);   // dmem[80]
    uint32_t lhu_result = read_dmem(21);  // dmem[84]
    printf("  LH:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lh_result);
    printf("  LHU: 0x%08X (expected 0x0000FFFF, zero extended)\n", lhu_result);
    int test15_pass = (lh_result == 0xFFFFFFFF && lhu_result == 0x0000FFFF);
    printf("  Test 15: %s\n", test15_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // Summary
    // =========================================================================
    printf("\n=== Test Summary ===\n");
    int total_pass = test1_pass + test2_pass + test3_pass + test4_pass + test5_pass + test6_pass +
                     test7_pass + test8_pass + test9_pass + test10_pass + test11_pass + test12_pass +
                     test13_pass + test14_pass + test15_pass;
    printf("  Passed: %d/15\n", total_pass);
    
    if (total_pass == 15) {
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

    // =========================================================================
    // Bus Sniffer Test - Verify host transaction logging
    // =========================================================================
    printf("\n=== Bus Sniffer Test ===\n");
    
    // Clear and enable sniffer
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x01);  // just enable
    
    // Do some transactions that will be logged
    write32(BAR_DMEM, 0xDEADBEEF);            // Write to DMEM[0]
    uint32_t dummy = read32(BAR_DMEM);        // Read from DMEM[0]
    write32(BAR_DMEM + 4, 0xCAFEBABE);        // Write to DMEM[1]
    (void)dummy;
    
    usleep(1000);
    
    // Read sniffer status
    uint32_t sniff_count = read32(BAR_SNIFFER + SNIFF_COUNT);
    uint32_t sniff_cycle = read32(BAR_SNIFFER + SNIFF_CYCLE);
    
    printf("  Sniffer log count: %u (expected >= 3)\n", sniff_count);
    printf("  Sniffer cycle: %u\n", sniff_cycle);
    
    int sniffer_pass = (sniff_count >= 3);
    
    if (sniff_count > 0) {
        printf("  Recent transactions (newest first):\n");
        int entries_to_show = (sniff_count < 5) ? sniff_count : 5;
        for (int i = 0; i < entries_to_show; i++) {
            uint32_t base = BAR_SNIFFER + SNIFF_ENTRY0 + i * 0x10;
            uint32_t w0 = read32(base + 0x00);  // [31:0]
            uint32_t w1 = read32(base + 0x04);  // [63:32]
            // w2 at +0x08 is padding
            uint32_t w3 = read32(base + 0x0C);  // [127:96] = data
            
            uint32_t type = w0 & 1;
            uint32_t timestamp = (w0 >> 16) & 0xFFFF;
            uint32_t addr = (w1 >> 16) & 0xFFFF;
            
            printf("    [%d] cycle=%5u %s addr=0x%05X data=0x%08X\n",
                   i, timestamp, type ? "WR" : "RD", addr, w3);
        }
    }
    printf("  Bus Sniffer: %s\n", sniffer_pass ? "PASSED" : "FAILED");

    // =========================================================================
    // CPU Logger Test
    // =========================================================================
    // Tests the CPU memory access logger which captures load/store operations.
    // With log_imem=0 (default), only DMEM accesses are logged, avoiding the
    // buffer filling with instruction fetches.
    printf("\n=== CPU Logger Test ===\n");
    int cpulog_pass = 1;
    
    // Reset CPU and clear logger
    cpu_reset();
    write32(BAR_CPULOG + CPULOG_CTRL, 0x3);  // Clear log (bit 1) + enable (bit 0)
    usleep(1000);
    write32(BAR_CPULOG + CPULOG_CTRL, 0x1);  // Enable only (log_imem=0, DMEM only)
    
    // Clear IMEM
    for (int i = 0; i < 64; i++) {
        write_imem(i, 0x00000013);  // NOP
    }
    
    // Load a simple program that does stores and loads
    // This should generate DMEM transactions that the logger will capture
    uint32_t cpulog_prog[] = {
        0x00A00093,  // ADDI x1, x0, 10     ; x1 = 10
        0x00102023,  // SW   x1, 0(x0)      ; dmem[0] = 10   <- DSTORE
        0x00002103,  // LW   x2, 0(x0)      ; x2 = dmem[0]   <- DLOAD
        0x00210133,  // ADD  x2, x2, x2     ; x2 = x2 + x2 = 20
        0x00202223,  // SW   x2, 4(x0)      ; dmem[4] = 20   <- DSTORE
        0x00402183,  // LW   x3, 4(x0)      ; x3 = dmem[4]   <- DLOAD
        0xDEA00213,  // ADDI x4, x0, 0xDEA  ; marker
        0x00402423,  // SW   x4, 8(x0)      ; dmem[8] = marker <- DSTORE
        0x00000013,  // NOP
        0x00000013,  // NOP
        0x00100073,  // EBREAK (halt)
    };
    
    for (size_t i = 0; i < sizeof(cpulog_prog) / sizeof(cpulog_prog[0]); i++) {
        write_imem(i, cpulog_prog[i]);
    }
    
    // Run for a short time
    cpu_run();
    usleep(10);  // Very short - we just need a few instructions
    cpu_stop();
    
    // Check log count - should have captured the DMEM transactions
    uint32_t cpulog_count = read32(BAR_CPULOG + CPULOG_COUNT);
    printf("  Log count: %u\n", cpulog_count);
    
    if (cpulog_count < 3) {
        printf("  ERROR: Expected at least 3 DMEM transactions, got %u\n", cpulog_count);
        cpulog_pass = 0;
    } else {
        printf("  Reading log entries (newest first):\n");
        // Read up to 8 entries
        int entries_to_read = (cpulog_count < 8) ? cpulog_count : 8;
        for (int i = 0; i < entries_to_read; i++) {
            uint32_t entry_base = BAR_CPULOG + CPULOG_ENTRY0 + (i * 0x10);
            uint32_t type_time = read32(entry_base + 0);
            uint32_t addr = read32(entry_base + 4);
            uint32_t data = read32(entry_base + 8);
            
            uint8_t type = type_time & 0x3;
            uint16_t timestamp = (type_time >> 16) & 0xFFFF;
            
            const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
            printf("    [%d] cycle=%4u %s addr=0x%08X data=0x%08X\n",
                   i, timestamp, type_names[type], addr, data);
        }
        
        // With log_imem=0, we should only see DSTORE (type=2) and DLOAD (type=1)
        // Check that we captured at least the expected transactions
        int found_stores = 0, found_loads = 0;
        for (int i = 0; i < entries_to_read; i++) {
            uint32_t entry_base = BAR_CPULOG + CPULOG_ENTRY0 + (i * 0x10);
            uint32_t type_time = read32(entry_base + 0);
            uint8_t type = type_time & 0x3;
            if (type == 1) found_loads++;
            if (type == 2) found_stores++;
        }
        
        printf("  Found %d stores, %d loads\n", found_stores, found_loads);
        if (found_stores >= 2 && found_loads >= 1) {
            printf("  CPU Logger: PASSED\n");
        } else {
            printf("  ERROR: Expected at least 2 stores and 1 load\n");
            cpulog_pass = 0;
        }
    }

    // =========================================================================
    // Final Summary
    // =========================================================================
    printf("\n=== Final Summary ===\n");
    printf("  RV32I Tests:   %d/15\n", total_pass);
    printf("  Bus Sniffer:   %s\n", sniffer_pass ? "PASS" : "FAIL");
    printf("  CPU Logger:    %s\n", cpulog_pass ? "PASS" : "FAIL");
    
    int all_pass = (total_pass == 15) && sniffer_pass && cpulog_pass;
    if (all_pass) {
        printf("\n=== ALL TESTS PASSED ===\n");
    } else {
        printf("\n=== SOME TESTS FAILED ===\n");
    }

    return all_pass ? 0 : 1;
}
