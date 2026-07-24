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
#define BAR_RESULT    0x0018   // CPU result
#define BAR_IMEM      0x1000   // Instruction memory (4KB)
#define BAR_DMEM      0x2000   // Data memory (8KB)

// Control bits
#define CTRL_RUN      (1 << 0)
#define CTRL_RESET    (1 << 1)

// Global state
static volatile uint64_t *bar64 = NULL;
static volatile uint32_t *bar32 = NULL;
static size_t bar_size = 0;
static int container_fd = -1;
static int group_fd = -1;
static int device_fd = -1;

// Register access (64-bit aligned for AXI-Lite)
void write64(uint32_t offset, uint64_t value) {
    bar64[offset / 8] = value;
}

uint64_t read64(uint32_t offset) {
    return bar64[offset / 8];
}

// 32-bit access (for control registers, which are 64-bit but we only use low 32)
void write32(uint32_t offset, uint32_t value) {
    write64(offset, value);
}

uint32_t read32(uint32_t offset) {
    return (uint32_t)read64(offset);
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
// Hardware writes both 32-bit words from 64-bit data to aligned address
// So we write pairs: {word1, word0} to even address

void load_program(const uint32_t *program, size_t count) {
    size_t i;
    printf("Loading %zu instructions...\n", count);
    
    // Write instruction pairs
    for (i = 0; i + 1 < count; i += 2) {
        uint32_t offset = BAR_IMEM + i * 4;
        uint64_t pair = ((uint64_t)program[i + 1] << 32) | program[i];
        write64(offset, pair);
    }
    // Handle odd last instruction (pair with NOP)
    if (i < count) {
        uint32_t offset = BAR_IMEM + i * 4;
        uint64_t pair = ((uint64_t)0x00000013 << 32) | program[i];  // NOP in upper
        write64(offset, pair);
    }
}

uint32_t read_imem(uint32_t word_idx) {
    uint32_t aligned_offset = BAR_IMEM + (word_idx & ~1) * 4;
    uint64_t pair = read64(aligned_offset);
    if (word_idx & 1)
        return (uint32_t)(pair >> 32);  // Odd: upper 32 bits
    else
        return (uint32_t)pair;           // Even: lower 32 bits
}

void write_dmem(uint32_t word_idx, uint32_t data) {
    // Must write as pair - read-modify-write
    uint32_t aligned_offset = BAR_DMEM + (word_idx & ~1) * 4;
    uint64_t pair = read64(aligned_offset);
    if (word_idx & 1)
        pair = (pair & 0xFFFFFFFF) | ((uint64_t)data << 32);
    else
        pair = (pair & 0xFFFFFFFF00000000ULL) | data;
    write64(aligned_offset, pair);
}

uint32_t read_dmem(uint32_t word_idx) {
    uint32_t aligned_offset = BAR_DMEM + (word_idx & ~1) * 4;
    uint64_t pair = read64(aligned_offset);
    if (word_idx & 1)
        return (uint32_t)(pair >> 32);  // Odd: upper 32 bits
    else
        return (uint32_t)pair;           // Even: lower 32 bits
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

    // Open VFIO container
    container_fd = open("/dev/vfio/vfio", O_RDWR);
    if (container_fd < 0) {
        perror("Failed to open /dev/vfio/vfio");
        return -1;
    }

    // Check API version
    if (ioctl(container_fd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        fprintf(stderr, "VFIO API version mismatch\n");
        return -1;
    }

    // Check IOMMU support
    if (!ioctl(container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        fprintf(stderr, "VFIO Type1 IOMMU not supported\n");
        return -1;
    }

    // Open VFIO group
    snprintf(group_path, sizeof(group_path), "/dev/vfio/%d", iommu_group);
    group_fd = open(group_path, O_RDWR);
    if (group_fd < 0) {
        perror("Failed to open VFIO group");
        return -1;
    }

    // Check group is viable
    if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) < 0) {
        perror("Failed to get group status");
        return -1;
    }
    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "VFIO group not viable (all devices bound to vfio-pci?)\n");
        return -1;
    }

    // Add group to container
    if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) < 0) {
        perror("Failed to set container");
        return -1;
    }

    // Enable IOMMU
    if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
        perror("Failed to set IOMMU type");
        return -1;
    }

    // Get device fd
    device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_addr);
    if (device_fd < 0) {
        perror("Failed to get device fd");
        return -1;
    }

    // Get device info
    if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info) < 0) {
        perror("Failed to get device info");
        return -1;
    }
    printf("Device has %d regions, %d IRQs\n", 
           device_info.num_regions, device_info.num_irqs);

    // Get BAR0 region info
    if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info) < 0) {
        perror("Failed to get BAR0 info");
        return -1;
    }

    bar_size = region_info.size;
    printf("BAR0: size=0x%llx, offset=0x%llx, flags=0x%x\n",
           (unsigned long long)region_info.size, 
           (unsigned long long)region_info.offset, 
           region_info.flags);

    // Map BAR0
    bar64 = mmap(NULL, bar_size, PROT_READ | PROT_WRITE,
                 MAP_SHARED, device_fd, region_info.offset);
    if (bar64 == MAP_FAILED) {
        perror("Failed to mmap BAR0");
        return -1;
    }
    bar32 = (volatile uint32_t *)bar64;
    printf("Mapped BAR0 at %p\n\n", bar64);

    return 0;
}

void vfio_cleanup(void) {
    if (bar64) munmap((void *)bar64, bar_size);
    if (device_fd >= 0) close(device_fd);
    if (group_fd >= 0) close(group_fd);
    if (container_fd >= 0) close(container_fd);
}

void print_usage(const char *prog) {
    printf("Usage: %s <pci_address> <iommu_group>\n", prog);
    printf("Example: %s 0000:b1:00.0 89\n", prog);
}

// Test program: compute 5 + 3, store to DMEM[0]
uint32_t test_program[] = {
    0x00500093,  // ADDI x1, x0, 5     ; x1 = 5
    0x00300113,  // ADDI x2, x0, 3     ; x2 = 3
    0x002081b3,  // ADD  x3, x1, x2    ; x3 = 8
    0x00302023,  // SW   x3, 0(x0)     ; dmem[0] = 8
    0x00000063,  // BEQ  x0, x0, 0     ; loop forever
};

int main(int argc, char *argv[]) {
    const char *pci_addr;
    int iommu_group;
    uint32_t result;

    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    pci_addr = argv[1];
    iommu_group = atoi(argv[2]);

    printf("RISC-V SoC Host Controller (VFIO)\n");
    printf("=================================\n\n");
    printf("PCI Address: %s\n", pci_addr);
    printf("IOMMU Group: %d\n\n", iommu_group);

    if (vfio_init(pci_addr, iommu_group) < 0) {
        return 1;
    }

    // Reset CPU
    printf("Resetting CPU...\n");
    cpu_reset();
    printf("  CTRL after reset: 0x%X\n", read32(BAR_CTRL));
    printf("  STATUS after reset: 0x%X\n", read32(BAR_STATUS));
    printf("  PC after reset: 0x%X\n", cpu_get_pc());

    // Load program
    load_program(test_program, sizeof(test_program) / sizeof(test_program[0]));
    
    // Verify IMEM
    printf("Verifying IMEM:\n");
    for (int i = 0; i < 5; i++) {
        printf("  IMEM[%d] = 0x%08X (expected 0x%08X)\n", 
               i, read_imem(i), test_program[i]);
    }

    // Clear DMEM[0]
    write_dmem(0, 0);
    printf("DMEM[0] before: %d\n", read_dmem(0));

    // Run CPU
    printf("Running CPU...\n");
    cpu_run();
    printf("  CTRL after run: 0x%X\n", read32(BAR_CTRL));
    printf("  STATUS after run: 0x%X\n", read32(BAR_STATUS));
    
    usleep(100000);  // 100ms
    
    printf("  STATUS after 100ms: 0x%X\n", read32(BAR_STATUS));
    printf("  PC after 100ms: 0x%X\n", cpu_get_pc());

    // Check results
    printf("\nResults:\n");
    printf("  PC:      0x%X\n", cpu_get_pc());
    result = read_dmem(0);
    printf("  DMEM[0]: %d (expected 8)\n", result);

    cpu_stop();
    vfio_cleanup();

    if (result == 8) {
        printf("\nTEST PASSED!\n");
        return 0;
    } else {
        printf("\nTEST FAILED!\n");
        return 1;
    }
}
