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

    // Test program: x1=5, x2=3, x3=x1+x2, store x3 to dmem[0], branch to self
    uint32_t program[] = {
        0x00500093,  // addi x1, x0, 5
        0x00300113,  // addi x2, x0, 3
        0x002081B3,  // add  x3, x1, x2
        0x00302023,  // sw   x3, 0(x0)
        0x00000063,  // beq  x0, x0, 0 (infinite loop)
    };
    size_t prog_len = sizeof(program) / sizeof(program[0]);

    // Clear bus sniffer BEFORE loading so we capture IMEM writes
    printf("Clearing bus sniffer...\n");
    write32(BAR_SNIFFER + 0x08, 0x03);  // Clear + enable

    // Reset and load
    cpu_reset();
    printf("Loading %zu instructions...\n", prog_len);
    load_program(program, prog_len);

    // Verify IMEM
    printf("\nVerifying IMEM:\n");
    int imem_ok = 1;
    for (size_t i = 0; i < prog_len; i++) {
        uint32_t rb = read_imem(i);
        printf("  IMEM[%zu] = 0x%08X (expected 0x%08X) %s\n", 
               i, rb, program[i], rb == program[i] ? "OK" : "FAIL");
        if (rb != program[i]) imem_ok = 0;
    }
    if (!imem_ok) {
        printf("\nIMEM verification FAILED!\n");
        return 1;
    }

    // Test host DMEM access
    printf("\n=== Testing host DMEM write/read ===\n");
    write_dmem64(0, 0xDEADBEEF);
    uint64_t rb = read_dmem64(0);
    printf("  Wrote 0xDEADBEEF, read back 0x%lX\n", rb);
    if ((rb & 0xFFFFFFFF) != 0xDEADBEEF) {
        printf("  Host DMEM access FAILED!\n");
        return 1;
    }
    printf("  Host DMEM access OK\n");

    // Clear DMEM[0] and run CPU
    write_dmem64(0, 0);
    printf("\nDMEM[0] before: %ld\n", read_dmem64(0));
    
    // Clear CPU logger before running (sniffer already cleared before IMEM load)
    printf("Clearing CPU logger...\n");
    printf("  CPUlog ctrl before:  0x%X\n", read32(BAR_CPULOG + 0x08));
    write32(BAR_CPULOG + 0x08, 0x03);   // Clear + enable CPU logger
    printf("  CPUlog ctrl after:   0x%X\n", read32(BAR_CPULOG + 0x08));
    
    printf("Running CPU...\n");
    cpu_run();
    usleep(100000);  // 100ms
    
    // Stop loggers
    write32(BAR_SNIFFER + 0x08, 0x00);  // Disable bus sniffer
    write32(BAR_CPULOG + 0x08, 0x00);   // Disable CPU logger
    
    uint32_t status = read32(BAR_STATUS);
    uint32_t pc = cpu_get_pc();
    printf("  STATUS: 0x%X\n", status);
    printf("  PC: 0x%X\n", pc);

    // Read result
    uint64_t dmem0 = read_dmem64(0);
    uint32_t result = (uint32_t)dmem0;
    printf("\nResults:\n");
    printf("  DMEM[0] (64-bit): 0x%016lX\n", dmem0);
    printf("  DMEM[0] (low 32): %d (expected 8)\n", result);

    if (result == 8) {
        printf("\n=== TEST PASSED ===\n");
    } else {
        printf("\n=== TEST FAILED ===\n");
    }
    
    // =========================================================================
    // Read Bus Sniffer Logs
    // =========================================================================
    printf("\n=== Bus Sniffer Log (host transactions) ===\n");
    uint32_t sniff_count = read32(BAR_SNIFFER + 0x00);
    uint32_t sniff_cycle = read32(BAR_SNIFFER + 0x04);
    printf("Total transactions: %u, Current cycle: %u\n", sniff_count, sniff_cycle);
    
    int sniff_entries = (sniff_count < 16) ? sniff_count : 16;  // Show up to 16
    for (int i = 0; i < sniff_entries; i++) {
        uint32_t base = BAR_SNIFFER + 0x10 + i * 0x10;
        uint32_t w0 = read32(base + 0x00);  // [31:0]: type at bit 0
        uint32_t w1 = read32(base + 0x04);  // [63:32]: addr[15:0]<<16 | timestamp[15:0]
        (void)read32(base + 0x08);          // [95:64]: padding (unused)
        uint32_t w3 = read32(base + 0x0C);  // [127:96]: data
        
        uint32_t type = w0 & 1;
        uint32_t timestamp = w1 & 0xFFFF;
        uint32_t addr = (w1 >> 16) & 0xFFFF;
        uint32_t data = w3;  // Data is in the high word
        
        printf("  [%d] cycle=%5u %s addr=0x%04X data=0x%08X\n",
               i, timestamp, type ? "WR" : "RD", addr, data);
    }
    
    // =========================================================================
    // Read CPU Logger Logs
    // =========================================================================
    printf("\n=== CPU Logger (CPU memory accesses) ===\n");
    uint32_t cpu_count = read32(BAR_CPULOG + 0x00);
    uint32_t cpu_cycle = read32(BAR_CPULOG + 0x04);
    printf("Total accesses: %u, Current cycle: %u\n", cpu_count, cpu_cycle);
    
    const char* type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???"};
    int cpu_entries = (cpu_count < 10) ? cpu_count : 10;
    for (int i = 0; i < cpu_entries; i++) {
        uint32_t base = BAR_CPULOG + 0x10 + i * 0x10;
        uint32_t w0 = read32(base + 0x00);  // [31:0]: timestamp[31:16], reserved[15:2], type[1:0]
        uint32_t w1 = read32(base + 0x04);  // [63:32]: address
        uint32_t w2 = read32(base + 0x08);  // [95:64]: data
        
        // Parse: [95:64]=data, [63:32]=addr, [31:16]=timestamp, [1:0]=type
        uint32_t type = w0 & 3;
        uint32_t timestamp = (w0 >> 16) & 0xFFFF;
        uint32_t addr = w1;
        uint32_t data = w2;
        
        printf("  [%2d] cycle=%5u %s addr=0x%08X data=0x%08X\n",
               i, timestamp, type_names[type], addr, data);
    }

    return (result == 8) ? 0 : 1;
}
