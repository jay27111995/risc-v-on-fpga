#!/bin/bash
# Build script for RISC-V SoC programs

set -e

# RISC-V toolchain from Quartus RiscFree
TOOLCHAIN=/opt/ALTERA/quartuspro/25.3.1/riscfree/toolchain/riscv32-unknown-elf/bin
export PATH=$TOOLCHAIN:$PATH

CROSS=riscv32-unknown-elf-
CFLAGS="-march=rv32im -mabi=ilp32 -O2 -nostdlib -nostartfiles -ffreestanding -Ilib"
LDFLAGS="-T link.ld -nostdlib"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source.c>"
    echo "Example: $0 hello_sum.c"
    exit 1
fi

SRC=$1
NAME=$(basename "$SRC" .c)

echo "=== Building $NAME ==="

# Compile startup code
echo "Compiling startup..."
${CROSS}gcc $CFLAGS -c start.S -o start.o

# Compile main source
echo "Compiling..."
${CROSS}gcc $CFLAGS -c "$SRC" -o "${NAME}.o"

# Link
echo "Linking..."
${CROSS}gcc $CFLAGS $LDFLAGS start.o "${NAME}.o" -o "${NAME}.elf"

# Disassemble for debugging
echo "Disassembling..."
${CROSS}objdump -d "${NAME}.elf" > "${NAME}.dis"

# Show size
echo ""
echo "=== Size ==="
${CROSS}size "${NAME}.elf"

echo ""
echo "=== Build complete ==="
echo "  ELF: ${NAME}.elf"
echo "  DIS: ${NAME}.dis"
echo ""
echo "=== To Run ==="
echo "  cd ~/risc-v-on-fpga"
echo "  sudo host/bin/elf_loader sw/${NAME}.elf 0000:b1:00.0 12"
echo "  sudo host/bin/uart_console 0000:b1:00.0 12"
