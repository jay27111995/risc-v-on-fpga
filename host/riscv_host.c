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
#define BAR_DBG_CPU_ADDR  0x0020   // Debug: last CPU DMEM write address
#define BAR_DBG_CPU_WDATA 0x0028   // Debug: last CPU DMEM write data
#define BAR_DBG_CPU_COUNT 0x0030   // Debug: CPU DMEM write count
#define BAR_DBG_MUX_ADDR  0x0038   // Debug: muxed DMEM write address
#define BAR_DBG_MUX_DATA  0x0040   // Debug: muxed DMEM write data
#define BAR_DBG_FLAGS     0x0048   // Debug: bit0=host_wen, bit1=dmem_wen
#define BAR_DBG_DMEM0     0x0050   // Debug: direct dmem[0] read
#define BAR_DBG_DMEM1     0x0058   // Debug: direct dmem[1] read
#define BAR_IMEM      0x1000   // Instruction memory (4KB)
#define BAR_DMEM      0x2000   // Data memory (8KB)
#define BAR_DBG_AWADDR 0x100   // Debug: last AWADDR
#define BAR_DBG_WDATA  0x108   // Debug: last WDATA
#define BAR_DBG_WSTRB  0x110   // Debug: last WSTRB + write count
#define BAR_DBG_SOC_RDATA 0x118 // Debug: last SoC read data
#define BAR_DBG_SOC_RADDR 0x11C // Debug: last SoC read addr
#define BAR_DBG_READ_MUX  0x120 // Debug: last read_data_mux
#define BAR_TEST_MEM   0x200   // Test memory (256 bytes)

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

// Debug: print last AXI write transaction
void print_debug(void) {
    uint64_t awaddr = read64(BAR_DBG_AWADDR);
    uint64_t wdata = read64(BAR_DBG_WDATA);
    uint64_t wstrb_cnt = read64(BAR_DBG_WSTRB);
    uint32_t wstrb = (wstrb_cnt >> 32) & 0xFF;
    uint32_t count = wstrb_cnt & 0xFFFFFFFF;
    printf("  DBG: AWADDR=0x%06lX WDATA=0x%016lX WSTRB=0x%02X count=%u\n",
           awaddr, wdata, wstrb, count);
}

// Memory access - AXI wrapper splits 64-bit write into two 32-bit SoC writes
// Host must write 64-bit pairs: {odd_word, even_word}

void write_imem_pair(uint32_t pair_idx, uint32_t even_word, uint32_t odd_word) {
    uint32_t offset = BAR_IMEM + pair_idx * 8;
    uint64_t data = ((uint64_t)odd_word << 32) | even_word;
    write64(offset, data);
}

uint32_t read_imem(uint32_t word_idx) {
    uint32_t offset = BAR_IMEM + (word_idx & ~1) * 4;  // Align to pair
    uint64_t data = read64(offset);
    if (word_idx & 1)
        return (uint32_t)(data >> 32);  // Odd word in upper
    else
        return (uint32_t)data;           // Even word in lower
}

void write_dmem_pair(uint32_t pair_idx, uint32_t even_word, uint32_t odd_word) {
    uint32_t offset = BAR_DMEM + pair_idx * 8;
    uint64_t data = ((uint64_t)odd_word << 32) | even_word;
    write64(offset, data);
}

void write_dmem(uint32_t word_idx, uint32_t data) {
    // Read-modify-write to preserve other word in pair
    uint32_t pair_idx = word_idx / 2;
    uint32_t offset = BAR_DMEM + pair_idx * 8;
    uint64_t existing = read64(offset);
    uint64_t new_data;
    if (word_idx & 1) {
        new_data = (existing & 0xFFFFFFFF) | ((uint64_t)data << 32);
    } else {
        new_data = (existing & 0xFFFFFFFF00000000ULL) | data;
    }
    write64(offset, new_data);
}

uint32_t read_dmem(uint32_t word_idx) {
    uint32_t offset = BAR_DMEM + (word_idx & ~1) * 4;  // Align to pair
    uint64_t data = read64(offset);
    printf("  [DEBUG] read_dmem(%d): offset=0x%X raw=0x%016lX\n", word_idx, offset, data);
    if (word_idx & 1)
        return (uint32_t)(data >> 32);  // Odd word in upper
    else
        return (uint32_t)data;           // Even word in lower
}

void test_axi_memory(void) {
    printf("\n=== Testing AXI wrapper test memory (0x200-0x2FF) ===\n");
    printf("64-bit writes split into two 32-bit writes by AXI wrapper\n\n");
    
    // Test 64-bit pair writes
    uint32_t test_vals[] = {0xDEADBEEF, 0x12345678, 0xCAFEBABE, 0xABCD1234};
    
    // Write pairs
    for (int i = 0; i < 4; i += 2) {
        uint32_t offset = BAR_TEST_MEM + i * 4;
        uint64_t data = ((uint64_t)test_vals[i+1] << 32) | test_vals[i];
        printf("Writing pair to 0x%03X: {0x%08X, 0x%08X} = 0x%016lX\n", 
               offset, test_vals[i], test_vals[i+1], data);
        write64(offset, data);
        print_debug();
    }
    
    printf("\nReading back (64-bit reads):\n");
    for (int i = 0; i < 4; i += 2) {
        uint32_t offset = BAR_TEST_MEM + i * 4;
        uint64_t rb = read64(offset);
        uint32_t even = (uint32_t)rb;
        uint32_t odd = (uint32_t)(rb >> 32);
        printf("  [0x%03X] read 0x%016lX -> even=0x%08X (exp 0x%08X) odd=0x%08X (exp 0x%08X)\n",
               offset, rb, even, test_vals[i], odd, test_vals[i+1]);
        if (even == test_vals[i] && odd == test_vals[i+1]) {
            printf("    OK!\n");
        } else {
            printf("    FAIL!\n");
        }
    }
    printf("\n");
}

void load_program(const uint32_t *program, size_t count) {
    size_t i;
    printf("Loading %zu instructions as pairs...\n", count);
    for (i = 0; i + 1 < count; i += 2) {
        printf("Writing IMEM[%zu,%zu] = {0x%08X, 0x%08X}\n", i, i+1, program[i], program[i+1]);
        write_imem_pair(i / 2, program[i], program[i + 1]);
        print_debug();
    }
    // Handle odd last instruction
    if (i < count) {
        printf("Writing IMEM[%zu] = 0x%08X (paired with NOP)\n", i, program[i]);
        write_imem_pair(i / 2, program[i], 0x00000013);  // NOP
        print_debug();
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

    // Test AXI wrapper memory first
    test_axi_memory();

    // Load program
    load_program(test_program, sizeof(test_program) / sizeof(test_program[0]));
    
    // Verify IMEM
    printf("Verifying IMEM:\n");
    for (int i = 0; i < 5; i++) {
        printf("  IMEM[%d] = 0x%08X (expected 0x%08X)\n", 
               i, read_imem(i), test_program[i]);
    }

    // Test host DMEM access first
    printf("\n=== Testing host DMEM write/read ===\n");
    write_dmem(0, 0xDEADBEEF);
    uint32_t readback = read_dmem(0);
    printf("  Wrote 0xDEADBEEF, read back 0x%X\n", readback);
    if (readback != 0xDEADBEEF) {
        printf("  ERROR: Host DMEM access broken!\n");
        return 1;
    }
    printf("  Host DMEM access OK\n");

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

    // Check CPU DMEM write debug
    printf("\nCPU DMEM write debug:\n");
    printf("  Write count: %d\n", read32(BAR_DBG_CPU_COUNT));
    printf("  Last addr:   0x%X\n", read32(BAR_DBG_CPU_ADDR));
    printf("  Last data:   %d (0x%X)\n", read32(BAR_DBG_CPU_WDATA), read32(BAR_DBG_CPU_WDATA));
    printf("  Mux addr:    0x%X\n", read32(BAR_DBG_MUX_ADDR));
    printf("  Mux data:    %d (0x%X)\n", read32(BAR_DBG_MUX_DATA), read32(BAR_DBG_MUX_DATA));
    uint32_t flags = read32(BAR_DBG_FLAGS);
    printf("  Host WEN:    %d\n", flags & 1);
    printf("  DMEM WEN:    %d\n", (flags >> 1) & 1);

    // Check results
    printf("\nResults:\n");
    printf("  PC:      0x%X\n", cpu_get_pc());
    
    result = read_dmem(0);
    printf("  DMEM[0]: %d (expected 8)\n", result);
    
    // Direct dmem read via debug registers (bypasses normal read path)
    printf("\nDirect DMEM read (via debug regs):\n");
    printf("  dmem[0]:     %d (0x%X)\n", read32(BAR_DBG_DMEM0), read32(BAR_DBG_DMEM0));
    printf("  dmem[1]:     %d (0x%X)\n", read32(BAR_DBG_DMEM1), read32(BAR_DBG_DMEM1));
    
    // AXI read path debug (captured during DMEM read above)
    printf("\nAXI read path debug:\n");
    printf("  SoC rdata:   0x%X\n", read32(BAR_DBG_SOC_RDATA));
    printf("  SoC raddr:   0x%X\n", read32(BAR_DBG_SOC_RADDR));
    printf("  read_mux:    0x%X\n", read32(BAR_DBG_READ_MUX));

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
