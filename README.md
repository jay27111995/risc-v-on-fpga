# RISC-V on FPGA

> **Note**: This project was developed with AI assistance (Claude/Kiro).

A complete RV32IM RISC-V CPU with PCIe BAR interface and virtual UART, targeting Intel Agilex 7 FPGA.

## Status

- **RV32IM CPU**: ✅ Working (all base + M extension instructions)
- **Virtual UART**: ✅ Working (1 char at a time TX/RX)
- **Timing**: ⚠️ Has violations (-0.578ns slack), causes instability with complex loops

## Specs

- **Target**: Intel Agilex 7 (AGF014)
- **Clock**: 250 MHz (PCIe clock domain)
- **Pipeline**: 6-stage (IF → ID → EX1 → EX2 → MEM → WB)
- **Memories**: 128KB IMEM, 32KB DMEM
- **Bitstream**: `riscv-soc-revid-0x30-git-78f20a2-md5-89fb9a9176d11e2a753a601e510a1df6.sof`

## Quick Start

```bash
# Set PCIe addresses
PCIE_EP=0000:b1:00.0  # Find with: lspci | grep -i 1172
PCIE_RP=0000:b0:03.0  # Root port
GRP=12                 # IOMMU group

# Program FPGA (hot-reload)
echo 1 | sudo tee /sys/bus/pci/devices/${PCIE_EP}/remove
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x50
configure_fpga riscv-soc-revid-0x30-*.sof
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x40
echo 1 | sudo tee /sys/bus/pci/rescan

# Setup VFIO
echo ${PCIE_EP} | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver_override
echo ${PCIE_EP} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind

# Build and run UART test
cd sw && ./build.sh uart_test.c
cd ../host && bash build.sh
sudo ./bin/loader --no-run ../sw/uart_test.bin $PCIE_EP $GRP
sudo ./bin/uart_console $PCIE_EP $GRP
```

## Virtual UART

Simple 1-char-at-a-time UART over shared DMEM:

**CPU side (`sw/uart.h`):**
```c
uart_putc('H');      // Send char
char c = uart_getc(); // Receive char (blocking)
```

**Host side:**
```bash
sudo ./bin/loader --no-run ../sw/uart_test.bin $PCIE_EP $GRP
sudo ./bin/uart_console $PCIE_EP $GRP
```

**Memory Map (DMEM offsets):**
| Offset | Name | Description |
|--------|------|-------------|
| 0x100 | TX_BUF | CPU writes char here |
| 0x204 | TX_READY | 1 = char ready for host |
| 0x300 | RX_BUF | Host writes char here |
| 0x404 | RX_READY | 1 = char ready for CPU |

## Directory Structure

```
├── rtl/                # SystemVerilog RTL
│   ├── riscv_soc.sv   # Top-level (CPU + memories)
│   ├── axi_core_hw.sv # AXI-Lite wrapper
│   ├── decoder.sv     # Instruction decoder
│   ├── alu.sv         # ALU (RV32IM)
│   ├── multiplier.sv  # Pipelined multiplier
│   └── divider.sv     # Multi-cycle divider
│
├── sw/                 # CPU software
│   ├── uart.h         # UART library (putc/getc)
│   ├── uart_test.c    # Echo test
│   ├── build.sh       # Build script
│   └── link.ld        # Linker script
│
├── host/               # Host tools
│   ├── src/
│   │   ├── loader.c       # Load binary to IMEM
│   │   ├── uart_console.c # UART terminal
│   │   └── riscv_lib.c    # CPU control library
│   └── build.sh
│
└── tb/                 # Verilator testbenches
    └── tb_axi_core.cpp # 44 tests (all pass)
```

## Simulation

All 44 tests pass in Verilator:

```bash
module load verilator/5.024
cd tb
verilator --cc --top-module axi_core_hw -I../rtl ../rtl/*.sv \
    --exe tb_axi_core.cpp -CFLAGS "-std=c++17" \
    -Wno-CASEINCOMPLETE -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-TIMESCALEMOD
make -C obj_dir -f Vaxi_core_hw.mk
./obj_dir/Vaxi_core_hw
```

## TODO

- [ ] Fix timing violations (need -0.578ns → positive slack)
  - Add pipeline registers to bus64to32 adapter
  - Or slow down CPU clock
- [ ] Multi-char UART buffering (currently broken due to timing issues)
- [ ] Interrupts for UART (currently polling)
- [ ] Full printf/scanf implementation

## Known Issues

1. **Timing violations**: 30,947 failing endpoints at 250MHz. Causes:
   - Polling loops sometimes exit early with garbage data
   - Complex C programs with loops may behave incorrectly
   - Simple char-by-char operations work reliably

2. **Workaround**: Keep CPU code simple, avoid tight polling loops

## BAR Memory Map

| Offset | Size | Description |
|--------|------|-------------|
| 0x00000 | 256B | Control registers |
| 0x20000 | 128KB | IMEM |
| 0x40000 | 4KB | Bus sniffer |
| 0x50000 | 4KB | CPU logger |
| 0x80000 | 32KB | DMEM |

### Control Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | CTRL | [0] RUN, [1] RESET |
| 0x08 | STATUS | [0] RUNNING, [1] HALTED |
| 0x10 | PC | Program counter |
| 0x20 | CYCLES | Cycle count |

## Supported Instructions

**RV32I** (37 instructions): ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND, ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI, LB, LH, LW, LBU, LHU, SB, SH, SW, BEQ, BNE, BLT, BGE, BLTU, BGEU, LUI, AUIPC, JAL, JALR, EBREAK

**RV32M** (8 instructions): MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU
