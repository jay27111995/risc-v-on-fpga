# RISC-V on FPGA

A minimal RISC-V CPU (RV32I subset) with PCIe BAR interface, targeting Intel Agilex 7 FPGA at 500MHz.

## Architecture

5-stage pipeline designed for 500MHz operation:

```
┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
│ IF  │──▶│ ID  │──▶│ EX  │──▶│ MEM │──▶│ WB  │
└─────┘   └─────┘   └─────┘   └─────┘   └─────┘
  │         │         │         │         │
Fetch    Decode    Execute   Memory    Write
 IMEM    RegFile    ALU      DMEM     RegFile
```

**Pipeline Features:**
- Data forwarding from MEM and WB stages
- Branch resolution in MEM stage (3-cycle penalty)
- Registered stall signal for clean timing
- Load-use hazard detection with early stall prediction

```
                    PCIe (500MHz)
                      │
              ┌───────▼───────┐
              │   PCIe IP     │
              │  (AXI-Lite)   │
              └───────┬───────┘
                      │
              ┌───────▼───────┐
              │ axi_core_hw   │
              │  (AXI slave)  │
              └───────┬───────┘
                      │ BAR access
              ┌───────▼───────┐
              │  riscv_soc    │
              │ (5-stage CPU) │
              │               │
              │  ┌─────────┐  │
              │  │  IMEM   │◄─┼── Host writes program
              │  │  (4KB)  │  │
              │  └────┬────┘  │
              │       │       │
              │  ┌────▼────┐  │
              │  │   CPU   │  │
              │  │ (RV32I) │  │
              │  └────┬────┘  │
              │       │       │
              │  ┌────▼────┐  │
              │  │  DMEM   │◄─┼── Host reads/writes data
              │  │  (8KB)  │  │
              │  └─────────┘  │
              └───────────────┘
```

## BAR Memory Map

| Offset | Size | Description |
|--------|------|-------------|
| 0x0000 | 256B | Control registers |
| 0x1000 | 4KB  | IMEM (instruction memory) |
| 0x2000 | 8KB  | DMEM (data memory, shared) |

### Control Registers

| Offset | Name   | Description |
|--------|--------|-------------|
| 0x00   | CTRL   | [0] RUN, [1] RESET (self-clearing) |
| 0x08   | STATUS | [0] RUNNING |
| 0x10   | PC     | Current program counter (read-only) |
| 0x18   | RESULT | CPU result output (read-only) |

## Supported Instructions

| Type   | Instructions |
|--------|--------------|
| R-type | ADD, SUB, AND, OR, XOR |
| I-type | ADDI, ANDI, ORI, XORI, LW |
| S-type | SW |
| B-type | BEQ |

## Files

```
rtl/
  alu.sv           # Arithmetic Logic Unit
  regfile.sv       # 32x32-bit Register File (with write-first bypass)
  decoder.sv       # Instruction Decoder
  riscv_soc.sv     # SoC with 5-stage pipelined CPU, memories, control regs
  axi_core_hw.sv   # AXI-Lite slave wrapper (connects PCIe to SoC)

tb/
  tb_riscv_soc.cpp # Direct SoC test (no AXI)
  tb_axi_core.cpp  # Integration test (AXI + SoC)

host/
  riscv_host.c     # VFIO host program
  build.sh         # Build script

build/
  build_fpga.sh    # Quartus build script
  pcie_ed.qsf      # Quartus project settings
```

## Build & Test (Simulation)

```bash
cd tb

# Test SoC directly
verilator --cc --top-module riscv_soc ../rtl/*.sv \
          --exe tb_riscv_soc.cpp --build \
          -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-UNUSEDSIGNAL
./obj_dir/Vriscv_soc

# Test AXI wrapper + SoC
verilator --cc --top-module axi_core_hw ../rtl/*.sv \
          --exe tb_axi_core.cpp --build \
          -Wno-WIDTHEXPAND -Wno-WIDTHTRUNC -Wno-UNUSEDSIGNAL
./obj_dir/Vaxi_core_hw
```

## Build (FPGA)

Requires Quartus 25.x with Agilex 7 support:

```bash
./build_fpga.sh 0x01   # 0x01 = revision ID
```

Output: `riscv-soc-revid-0x01-git-XXXXX-md5-XXXXX.sof`

## Host Program (VFIO)

```bash
cd host
./build.sh

# Setup VFIO (as root)
PCI=0000:b1:00.0
echo $PCI > /sys/bus/pci/devices/$PCI/driver/unbind
echo vfio-pci > /sys/bus/pci/devices/$PCI/driver_override
echo $PCI > /sys/bus/pci/drivers/vfio-pci/bind

# Find IOMMU group
GRP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))

# Run test
./riscv_host $PCI $GRP
```

## Example Program

```asm
ADDI x1, x0, 5      # x1 = 5
ADDI x2, x0, 3      # x2 = 3  
ADD  x3, x1, x2     # x3 = 8
SW   x3, 0(x0)      # dmem[0] = 8
BEQ  x0, x0, 0      # loop forever
```

## Target FPGA

- **Device:** Intel Agilex 7 (AGIB027R29A1E1VB)
- **Board:** DK-DEV-AGI027-RA (I-Series Dev Kit)
- **PCIe:** x16 Gen4

## Roadmap

- [x] Basic CPU (RV32I subset)
- [x] SoC with BAR interface
- [x] AXI wrapper for PCIe
- [x] FPGA build infrastructure
- [x] 5-stage pipeline for 500MHz timing
- [x] VFIO host program
- [x] Simulation tests
- [ ] Hardware test on Agilex 7
- [ ] Add more instructions (shifts, JAL, JALR)
- [ ] Add interrupts
