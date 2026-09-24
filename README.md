# RISC-V on FPGA

> **Note**: This project was developed with AI assistance (Claude/Kiro).

A complete RV32IM RISC-V CPU with PCIe BAR interface and virtual UART, targeting Intel Agilex 7 FPGA.

## Status

- **RV32IM CPU**: ✅ Working (all base + M extension instructions)
- **Virtual UART**: ✅ Working (printf/scanf over PCIe)
- **Test Suite**: ✅ 47/47 tests passing

## Specs

- **Target**: Intel Agilex 7 (AGF014)
- **Clock**: 250 MHz (PCIe clock domain)
- **Pipeline**: 6-stage (IF → ID → EX1 → EX2 → MEM → WB)
- **Memories**: 128KB IMEM (instructions), 32KB DMEM (data)
- **Bitstream**: `riscv-soc-revid-0x01-git-0b15919-md5-88959086559224bbe749632b8cb164e8.sof`

## Quick Start

### 1. Build a program
```bash
cd ~/risc-v-on-fpga/sw
./build.sh hello_sum.c
```

### 2. Load and run
```bash
cd ~/risc-v-on-fpga
sudo host/bin/elf_loader sw/hello_sum.elf 0000:b1:00.0 12
sudo host/bin/uart_console 0000:b1:00.0 12
```

## Writing Programs

Include `uart.h` for I/O:

```c
#include "uart.h"

int main(void) {
    int a, b;
    printf("Enter two numbers: ");
    scanf("%d %d", &a, &b);
    printf("%d + %d = %d\n", a, b, a + b);
    return 0;
}
```

### Available Functions (uart.h)

- `printf(fmt, ...)` - Supports %d, %s, %c, %x
- `scanf(fmt, ...)` - Supports %d
- `putchar(c)` - Output single character
- `getchar()` - Read single character (blocking)
- `puts(s)` - Output string with newline
- `print(s)` - Output string without newline

## Memory Architecture

- **IMEM**: Instruction memory at 0x00000000 (separate from data)
- **DMEM**: Data memory at 0x00000000 (separate physical memory)
- Linker uses VMA 0x10000xxx for DMEM sections to avoid overlap

### UART Memory Map (DMEM offsets)
```
0x100-0x13F: TX_BUF (16 words circular buffer)
0x140:       TX_HEAD
0x144:       TX_TAIL
0x200-0x23F: RX_BUF (16 words circular buffer)
0x240:       RX_HEAD
0x244:       RX_TAIL
```

## Host Tools

- `host/bin/elf_loader` - Load ELF to FPGA (routes .text→IMEM, .data/.rodata→DMEM)
- `host/bin/uart_console` - Interactive UART terminal

## PCIe Setup (if needed)

```bash
PCIE_EP=0000:b1:00.0
PCIE_RP=0000:b0:03.0
GRP=12

# Program FPGA
echo 1 | sudo tee /sys/bus/pci/devices/${PCIE_EP}/remove
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x50
configure_fpga riscv-soc-*.sof
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x40
echo 1 | sudo tee /sys/bus/pci/rescan

# Setup VFIO
echo ${PCIE_EP} | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver_override
echo ${PCIE_EP} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
```

## License

MIT
