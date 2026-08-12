#!/bin/bash

# Build the host programs
set -e

echo "Building host programs..."

CFLAGS="-O2 -Wall -I include"
SRC=src
OUT=bin

mkdir -p $OUT

# Common libraries
gcc $CFLAGS -c $SRC/pcie_vfio.c -o $OUT/pcie_vfio.o
gcc $CFLAGS -c $SRC/riscv_lib.c -o $OUT/riscv_lib.o

LIBS="$OUT/pcie_vfio.o $OUT/riscv_lib.o"

# Build executables
gcc $CFLAGS -c $SRC/riscv_host.c -o $OUT/riscv_host.o
gcc $CFLAGS $LIBS $OUT/riscv_host.o -o $OUT/riscv_host

gcc $CFLAGS -c $SRC/loader.c -o $OUT/loader.o
gcc $CFLAGS $LIBS $OUT/loader.o -o $OUT/loader

gcc $CFLAGS -c $SRC/test_sniffer.c -o $OUT/test_sniffer.o
gcc $CFLAGS $LIBS $OUT/test_sniffer.o -o $OUT/test_sniffer

gcc $CFLAGS -c $SRC/test_logger.c -o $OUT/test_logger.o
gcc $CFLAGS $LIBS $OUT/test_logger.o -o $OUT/test_logger

gcc $CFLAGS -c $SRC/test_programs.c -o $OUT/test_programs.o
gcc $CFLAGS $LIBS $OUT/test_programs.o -o $OUT/test_programs

# Clean up object files
rm -f $OUT/*.o

echo ""
echo "Built in $OUT/:"
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

   ./bin/riscv_host $PCI $GRP      # RV32I instruction tests
   ./bin/test_sniffer $PCI $GRP    # Bus sniffer test
   ./bin/test_logger $PCI $GRP     # CPU logger with IMEM trace
   ./bin/test_programs $PCI $GRP   # Run sum.c/factorial.c

EOF
