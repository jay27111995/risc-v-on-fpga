#!/bin/bash

# Build the host programs
set -e

echo "Building host programs..."

# Common libraries
gcc -O2 -Wall -c pcie_vfio.c -o pcie_vfio.o
gcc -O2 -Wall -c riscv_lib.c -o riscv_lib.o

LIBS="pcie_vfio.o riscv_lib.o"

gcc -O2 -Wall -c riscv_host.c -o riscv_host.o
gcc -O2 -Wall $LIBS riscv_host.o -o riscv_host

gcc -O2 -Wall -c loader.c -o loader.o
gcc -O2 -Wall $LIBS loader.o -o loader

gcc -O2 -Wall -c test_sniffer.c -o test_sniffer.o
gcc -O2 -Wall $LIBS test_sniffer.o -o test_sniffer

gcc -O2 -Wall -c test_logger.c -o test_logger.o
gcc -O2 -Wall $LIBS test_logger.o -o test_logger

gcc -O2 -Wall -c test_programs.c -o test_programs.o
gcc -O2 -Wall $LIBS test_programs.o -o test_programs

echo ""
echo "Built:"
echo "  riscv_host    - RV32I instruction tests"
echo "  loader        - Load binary to IMEM and run"
echo "  test_sniffer  - Bus sniffer test"
echo "  test_logger   - CPU logger test with IMEM tracing"
echo "  test_programs - Run sum.c and factorial.c"
echo ""

# Usage instructions
cat << 'EOF'
=== VFIO Setup ===

1. Find your FPGA device:
   lspci | grep -i '1172\|beef'

2. Bind to VFIO and run:
   PCI=0000:b1:00.0
   echo $PCI > /sys/bus/pci/devices/$PCI/driver/unbind 2>/dev/null
   echo vfio-pci > /sys/bus/pci/devices/$PCI/driver_override
   echo $PCI > /sys/bus/pci/drivers/vfio-pci/bind
   GRP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))

   ./riscv_host $PCI $GRP      # RV32I instruction tests
   ./test_sniffer $PCI $GRP    # Bus sniffer test
   ./test_logger $PCI $GRP     # CPU logger with IMEM trace
   ./test_programs $PCI $GRP   # Run sum.c/factorial.c

EOF
