// PCIe VFIO Interface
// ============================================================================
// Low-level VFIO setup and BAR access functions for PCIe devices.
// ============================================================================

#ifndef PCIE_VFIO_H
#define PCIE_VFIO_H

#include <stddef.h>
#include <stdint.h>

// Initialize VFIO and map BAR0
// Returns 0 on success, -1 on failure
int vfio_init(const char *pci_addr, int iommu_group);

// Clean up VFIO resources
void vfio_cleanup(void);

// Get BAR0 size (valid after vfio_init)
size_t vfio_get_bar_size(void);

// 64-bit BAR access (native width)
void write64(uint32_t offset, uint64_t value);
uint64_t read64(uint32_t offset);

// 32-bit BAR access (handles 64-bit BAR alignment)
void write32(uint32_t offset, uint32_t value);
uint32_t read32(uint32_t offset);

#endif // PCIE_VFIO_H
