#!/bin/bash
# Build script for RISC-V SoC programs

set -e

# RISC-V toolchain from Quartus RiscFree
TOOLCHAIN=/opt/ALTERA/quartuspro/25.3.1/riscfree/toolchain/riscv32-unknown-elf/bin
export PATH=$TOOLCHAIN:$PATH

CROSS=riscv32-unknown-elf-
CFLAGS="-march=rv32im -mabi=ilp32 -O2 -nostdlib -nostartfiles -ffreestanding"
LDFLAGS="-T link.ld -nostdlib"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <source.c>"
    echo "Example: $0 factorial.c"
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

# Link (startup first!)
echo "Linking..."
${CROSS}gcc $CFLAGS $LDFLAGS start.o "${NAME}.o" -o "${NAME}.elf"

# Generate binary
echo "Generating binary..."
${CROSS}objcopy -O binary "${NAME}.elf" "${NAME}.bin"

# Generate hex for loading
echo "Generating hex..."
${CROSS}objcopy -O verilog "${NAME}.elf" "${NAME}.hex"

# Disassemble for debugging
echo "Disassembling..."
${CROSS}objdump -d "${NAME}.elf" > "${NAME}.dis"

# Show size
echo ""
echo "=== Size ==="
${CROSS}size "${NAME}.elf"

# Show first few instructions
echo ""
echo "=== First 20 instructions ==="
head -40 "${NAME}.dis" | tail -30

echo ""
echo "=== Build complete ==="
echo "  ELF:  ${NAME}.elf"
echo "  BIN:  ${NAME}.bin"
echo "  HEX:  ${NAME}.hex"
echo "  DIS:  ${NAME}.dis"
