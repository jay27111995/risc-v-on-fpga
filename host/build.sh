#!/bin/bash

# Build the host program
gcc -O2 -Wall -o riscv_host riscv_host.c
echo "Built: riscv_host"
echo ""

# Usage instructions
cat << 'EOF'
=== VFIO Setup ===

1. Find your FPGA device:
   lspci | grep -i '1172\|beef'
   # Example output: b1:00.0 Unassigned class [ff00]: ...

2. Unbind from current driver (if any):
   echo 0000:b1:00.0 > /sys/bus/pci/devices/0000:b1:00.0/driver/unbind

3. Bind to vfio-pci:
   echo 'vfio-pci' > /sys/bus/pci/devices/0000:b1:00.0/driver_override
   echo 0000:b1:00.0 > /sys/bus/pci/drivers/vfio-pci/bind

4. Find IOMMU group:
   basename $(readlink /sys/bus/pci/devices/0000:b1:00.0/iommu_group)
   # Example output: 89

5. Run the test:
   ./riscv_host 0000:b1:00.0 89

=== Quick one-liner (replace b1:00.0 with your address) ===

PCI=0000:b1:00.0; echo $PCI > /sys/bus/pci/devices/$PCI/driver/unbind 2>/dev/null; echo vfio-pci > /sys/bus/pci/devices/$PCI/driver_override; echo $PCI > /sys/bus/pci/drivers/vfio-pci/bind; GRP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group)); ./riscv_host $PCI $GRP

EOF
