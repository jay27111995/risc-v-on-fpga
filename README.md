# RISC-V on FPGA

A simple RISC-V CPU (RV32I subset) with PCIe BAR interface, targeting Intel Agilex 7 FPGA.

## Architecture

```
                    PCIe (500MHz)
                      │
                      ▼
              ┌───────────────┐
              │   PCIe IP     │
              │  (AXI-Lite)   │
              └───────┬───────┘
                      │
              ┌───────▼───────┐
              │ axi_core_hw   │
              │ (clk divider) │ ◄── 500MHz / 4 = 125MHz for CPU
              └───────┬───────┘
                      │ BAR access
              ┌───────▼───────┐
              │  riscv_soc    │
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
src/
  alu.sv           # Arithmetic Logic Unit
  regfile.sv       # 32x32-bit Register File  
  decoder.sv       # Instruction Decoder
  riscv_soc.sv     # SoC with CPU, memories, control regs

fpga/
  axi_core_hw.sv   # AXI wrapper (connects PCIe to SoC)
  axi_core_hw.tcl  # Platform Designer component
  pcie_ed.tcl      # Platform Designer system
  pcie_ed.qsf      # Quartus pin assignments

tb/
  tb_riscv_soc.cpp # Unit test for SoC
  tb_axi_core.cpp  # Integration test (AXI + SoC)

host/
  riscv_host.c     # VFIO host program
  build.sh         # Build script with setup instructions
```

## Build & Test (Simulation)

```bash
# Test SoC
cd tb
verilator --cc ../src/*.sv --top-module riscv_soc \
          --exe tb_riscv_soc.cpp --build -Wno-TIMESCALEMOD -Wno-CASEINCOMPLETE
./obj_dir/Vriscv_soc

# Test full FPGA design (AXI + SoC)
verilator --cc ../src/*.sv ../fpga/axi_core_hw.sv --top-module axi_core_hw \
          --exe tb_axi_core.cpp --build -Wno-TIMESCALEMOD -Wno-CASEINCOMPLETE
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
- [x] Clock divider for timing closure
- [x] VFIO host program
- [x] Simulation tests
- [ ] Hardware test on Agilex 7
- [ ] Add more instructions (shifts, JAL, JALR)
- [ ] Add interrupts
