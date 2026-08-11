// PCIe VFIO Interface
// ============================================================================
// Low-level VFIO setup and BAR access functions for PCIe devices.
//
// Usage:
//   1. Call vfio_init() with PCI address and IOMMU group
//   2. Use read32/write32 or read64/write64 to access BAR registers
//   3. Call vfio_cleanup() when done (optional, OS cleans up on exit)
// ============================================================================

#include "pcie_vfio.h"

#include <fcntl.h>
#include <linux/vfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

// Global state
static volatile uint64_t *bar64 = NULL;
static size_t bar_size = 0;
static int container_fd = -1;
static int group_fd = -1;
static int device_fd = -1;

// ----------------------------------------------------------------------------
// BAR Access Functions
// ----------------------------------------------------------------------------

void write64(uint32_t offset, uint64_t value) { bar64[offset / 8] = value; }

uint64_t read64(uint32_t offset) { return bar64[offset / 8]; }

void write32(uint32_t offset, uint32_t value) {
  // Handle 32-bit writes properly for 64-bit BAR
  uint32_t aligned_offset = offset & ~7; // Align to 8 bytes
  uint64_t current = bar64[aligned_offset / 8];

  if (offset & 4) {
    // Upper 32 bits
    current = (current & 0xFFFFFFFF) | ((uint64_t)value << 32);
  } else {
    // Lower 32 bits
    current = (current & 0xFFFFFFFF00000000ULL) | value;
  }
  bar64[aligned_offset / 8] = current;
}

uint32_t read32(uint32_t offset) {
  uint64_t qword = read64(offset & ~7); // Align to 8 bytes
  if (offset & 4) {
    return (uint32_t)(qword >> 32); // Upper 32 bits
  } else {
    return (uint32_t)qword; // Lower 32 bits
  }
}

// ----------------------------------------------------------------------------
// VFIO Setup
// ----------------------------------------------------------------------------

int vfio_init(const char *pci_addr, int iommu_group) {
  char group_path[64];
  struct vfio_group_status group_status = {.argsz = sizeof(group_status)};
  struct vfio_device_info device_info = {.argsz = sizeof(device_info)};
  struct vfio_region_info region_info = {.argsz = sizeof(region_info),
                                         .index = VFIO_PCI_BAR0_REGION_INDEX};

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

  // Open IOMMU group
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
    fprintf(stderr,
            "VFIO group not viable (all devices must be bound to vfio-pci)\n");
    return -1;
  }

  // Add group to container
  if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container_fd) < 0) {
    perror("Failed to set container");
    return -1;
  }

  // Set IOMMU type
  if (ioctl(container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
    perror("Failed to set IOMMU");
    return -1;
  }

  // Get device FD
  device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_addr);
  if (device_fd < 0) {
    perror("Failed to get device FD");
    return -1;
  }

  // Get device info
  if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info) < 0) {
    perror("Failed to get device info");
    return -1;
  }

  // Get BAR0 info
  if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info) < 0) {
    perror("Failed to get BAR0 info");
    return -1;
  }

  // Map BAR0
  bar64 = mmap(NULL, region_info.size, PROT_READ | PROT_WRITE, MAP_SHARED,
               device_fd, region_info.offset);
  if (bar64 == MAP_FAILED) {
    perror("Failed to mmap BAR0");
    bar64 = NULL;
    return -1;
  }

  bar_size = region_info.size;
  printf("BAR0 mapped at %p, size %zu bytes\n", (void *)bar64, bar_size);
  return 0;
}

void vfio_cleanup(void) {
  if (bar64 && bar64 != MAP_FAILED) {
    munmap((void *)bar64, bar_size);
    bar64 = NULL;
  }
  if (device_fd >= 0) {
    close(device_fd);
    device_fd = -1;
  }
  if (group_fd >= 0) {
    close(group_fd);
    group_fd = -1;
  }
  if (container_fd >= 0) {
    close(container_fd);
    container_fd = -1;
  }
}

size_t vfio_get_bar_size(void) { return bar_size; }
