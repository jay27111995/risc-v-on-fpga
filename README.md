# RISC-V on FPGA

> ⚠️ **Disclaimer**: This project contains AI-generated code (Claude/Kiro assisted).

A complete RV32I RISC-V CPU with PCIe BAR interface, targeting Intel Agilex 7 FPGA.

## Status: ✅ Working

All 37 RV32I base instructions implemented and verified on hardware.
**C programs compiled with GCC run correctly on the FPGA!**

- **Target**: Intel Agilex 7 (AGF014)
- **Clock**: 250 MHz (PCIe clock domain)
- **Pipeline**: 5-stage (IF → ID → EX → MEM → WB)
- **Memories**: 128KB IMEM, 32KB DMEM
- **Bitstream**: `riscv-soc-revid-0x2f-git-91a4c84-md5-a2936d3f2772083f6e6bcf3213e8759b.sof`

## Quick Start

```bash
# 1. Program FPGA (see "Running on FPGA" section for full PCIe hot-reload steps)
PCIE_EP=0000:b1:00.0  # Find with: lspci | grep -i 1172
PCIE_RP=0000:b0:03.0  # Root port
echo 1 | sudo tee /sys/bus/pci/devices/${PCIE_EP}/remove
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x50
configure_fpga riscv-soc-revid-0x2f-git-91a4c84-md5-a2936d3f2772083f6e6bcf3213e8759b.sof
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x40
echo 1 | sudo tee /sys/bus/pci/rescan

# 2. Build host tools and a C program
cd host && bash build.sh
cd ../sw && ./build.sh sum.c

# 3. Setup VFIO and run
GRP=$(basename $(readlink /sys/bus/pci/devices/${PCIE_EP}/iommu_group))
echo ${PCIE_EP} | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver_override
echo ${PCIE_EP} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
sudo ./host/bin/test_programs ${PCIE_EP} $GRP
```

## Architecture

```
              PCIe Host (x86)
                   │
                   ▼
          ┌────────────────┐
          │   PCIe R-Tile  │  250 MHz
          │   (AXI-Lite)   │
          └───────┬────────┘
                  │
          ┌───────▼────────┐
          │  axi_core_hw   │
          │  (64→32 bridge)│
          └───────┬────────┘
                  │
          ┌───────▼────────┐
          │   riscv_soc    │
          │  ┌──────────┐  │
          │  │ RV32I CPU│  │
          │  │ 5-stage  │  │
          │  └────┬─────┘  │
          │       │        │
          │  ┌────┴────┐   │
          │  │IMEM│DMEM│   │
          │  │128K│32K │   │
          │  └─────────┘   │
          └────────────────┘
```

### Pipeline

```
┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
│ IF  │──▶│ ID  │──▶│ EX  │──▶│ MEM │──▶│ WB  │
└─────┘   └─────┘   └─────┘   └─────┘   └─────┘
Fetch    Decode    Execute   Memory   Writeback
```

Features:
- Full data forwarding (EX→EX, MEM→EX)
- Load-use hazard detection with stall
- Branch resolution in EX stage
- EBREAK instruction halts CPU

## BAR Memory Map

| Offset | Size | Description |
|--------|------|-------------|
| 0x00000 | 256B | Control/status registers |
| 0x20000 | 128KB | IMEM (instruction memory) |
| 0x40000 | 4KB | Bus sniffer (host transaction logger) |
| 0x50000 | 4KB | CPU logger (CPU memory access logger) |
| 0x80000 | 32KB | DMEM (data memory) |

### Control Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | CTRL | [0] RUN, [1] RESET |
| 0x08 | STATUS | [0] RUNNING, [1] HALTED |
| 0x10 | PC | Current program counter |
| 0x20 | CYCLES | Cycle count (write to clear all counters) |
| 0x24 | INSTRS | Instructions retired |
| 0x28 | STALLS | Stall cycles |
| 0x2C | BRANCHES | Branch instructions |
| 0x30 | BR_TAKEN | Branches taken |
| 0x34 | LOADS | Load instructions |
| 0x38 | STORES | Store instructions |

### Debug Loggers

**Bus Sniffer** (0x40000): Logs host PCIe transactions to DMEM/IMEM regions.
- 128-bit entries with 64-bit timestamps
- Captures address, data, read/write type

**CPU Logger** (0x50000): Logs CPU memory accesses.
- 128-bit entries with 64-bit timestamps
- Captures instruction fetches (optional), data loads, data stores
- Ctrl bit [2] enables IMEM tracing

## Supported Instructions

Complete RV32I base instruction set (37 instructions):

| Type | Instructions |
|------|--------------|
| R-type | ADD, SUB, SLL, SLT, SLTU, XOR, SRL, SRA, OR, AND |
| I-type | ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI |
| I-type | LB, LH, LW, LBU, LHU, JALR |
| S-type | SB, SH, SW |
| B-type | BEQ, BNE, BLT, BGE, BLTU, BGEU |
| U-type | LUI, AUIPC |
| J-type | JAL |
| System | EBREAK (halts CPU) |

## Directory Structure

```
├── rtl/                    # Verilog/SystemVerilog RTL
│   ├── riscv_soc.sv       # Top-level SoC (CPU + memories + loggers)
│   ├── riscv_core.sv      # 5-stage pipelined CPU
│   ├── decoder.sv         # Instruction decoder
│   ├── alu.sv             # ALU
│   ├── axi_core_hw.sv     # AXI-Lite slave wrapper
│   ├── bus64to32.sv       # 64-to-32 bit bus adapter
│   ├── bus_sniffer.sv     # Host transaction logger
│   └── cpu_logger.sv      # CPU memory access logger
│
├── sw/                     # Software/firmware
│   ├── build.sh           # Build script (uses Quartus RiscFree GCC)
│   ├── link.ld            # Linker script
│   ├── start.S            # Startup assembly
│   ├── sum.c              # Example: sum(1..10)
│   └── factorial.c        # Example: factorial(5)
│
├── host/                   # Host-side tools (see host/README.md)
│   ├── src/               # Source files
│   ├── include/           # Header files
│   ├── bin/               # Built executables (gitignored)
│   └── build.sh           # Build script
│
├── tb/                     # Verilator testbenches
│   ├── tb_axi_core.cpp    # Main SoC testbench
│   ├── tb_host_sim.cpp    # Host simulation tests
│   ├── tb_bus_sniffer.cpp # Bus sniffer tests
│   └── tb_cpu_logger.cpp  # CPU logger tests
│
├── build/                  # Quartus build output
│
└── *.sof                   # Working bitstream (checked in)
```

## Building

### Host Tools

```bash
cd host && bash build.sh
```

### Software (C Programs)

```bash
cd sw && ./build.sh sum.c
```

See [sw/README.md](sw/README.md) for toolchain setup.

### FPGA Bitstream

```bash
cd build && quartus_sh --flow compile pcie_ed
```

## Running on FPGA

### 1. Program FPGA

Set up your PCIe endpoint and root port addresses:
```bash
PCIE_EP=0000:b1:00.0  # Endpoint (FPGA) - find with: lspci | grep -i 1172
PCIE_RP=0000:b0:03.0  # Root port (upstream bridge)
```

Program the FPGA (hot-reload safe):
```bash
# Remove endpoint from PCIe bus
echo 1 | sudo tee /sys/bus/pci/devices/${PCIE_EP}/remove

# Disable link (set link disable bit)
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x50

# Program FPGA via JTAG
configure_fpga riscv-soc-revid-0x2f-git-91a4c84-md5-a2936d3f2772083f6e6bcf3213e8759b.sof

# Re-enable link
sudo setpci -s ${PCIE_RP} CAP_EXP+0x10.B=0x40

# Rescan PCIe bus to re-enumerate the device
echo 1 | sudo tee /sys/bus/pci/rescan

# Verify device is back
sudo lspci -s ${PCIE_EP} -vvv
```

### 2. Setup VFIO

```bash
GRP=$(basename $(readlink /sys/bus/pci/devices/${PCIE_EP}/iommu_group))

echo ${PCIE_EP} | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/${PCIE_EP}/driver_override
echo ${PCIE_EP} | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
```

### 3. Run Tests

```bash
cd host
sudo ./bin/riscv_host $PCI $GRP      # RV32I instruction tests
sudo ./bin/test_logger $PCI $GRP     # CPU logger test
sudo ./bin/test_sniffer $PCI $GRP    # Bus sniffer test
sudo ./bin/test_programs $PCI $GRP   # Run sum.c/factorial.c
sudo ./bin/loader ../sw/program.bin $PCI $GRP  # Load custom program
```

See [host/README.md](host/README.md) for details on each tool.

## Simulation

```bash
module load verilator/5.024
cd tb

# Run all testbenches
verilator --cc --top-module axi_core_hw -I../rtl ../rtl/*.sv \
    --exe tb_axi_core.cpp -CFLAGS "-std=c++17"
make -C obj_dir -f Vaxi_core_hw.mk && ./obj_dir/Vaxi_core_hw
```

## Timing

Current build: -0.185ns slack at worst-case corner (Slow 100C).
Marginal but functional on hardware.
