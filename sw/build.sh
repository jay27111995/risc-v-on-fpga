#!/bin/bash
# Build script for RISC-V SoC programs

set -e

# RISC-V toolchain
TOOLCHAIN=/opt/ALTERA/quartuspro/25.3.1/riscfree/toolchain/riscv32-unknown-elf/bin
export PATH=$TOOLCHAIN:$PATH

CROSS=riscv32-unknown-elf-
CFLAGS="-march=rv32im -mabi=ilp32 -O2 -nostdlib -nostartfiles -ffreestanding -Ilib"
LDFLAGS="-T src/link.ld -nostdlib"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source.c>"
    echo "       $0 examples/hello_sum.c"
    exit 1
fi

SRC=$1
NAME=$(basename "$SRC" .c)

echo "=== Building $NAME ==="

# Build directory
mkdir -p build

# Compile startup
${CROSS}gcc $CFLAGS -c src/start.S -o build/start.o

# Compile source
${CROSS}gcc $CFLAGS -c "$SRC" -o build/${NAME}.o

# Link
${CROSS}gcc $CFLAGS $LDFLAGS build/start.o build/${NAME}.o -o build/${NAME}.elf

# Disassemble
${CROSS}objdump -d build/${NAME}.elf > build/${NAME}.dis

# Size
echo ""
${CROSS}size build/${NAME}.elf

echo ""
echo "=== Build complete ==="
echo "  ELF: build/${NAME}.elf"
echo ""
echo "=== To Run ==="
echo "  cd ~/risc-v-on-fpga"
echo "  sudo host/bin/elf_loader sw/build/${NAME}.elf 0000:b1:00.0 12"
echo "  sudo host/bin/uart_console 0000:b1:00.0 12"
