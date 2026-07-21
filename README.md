# RISC-V on FPGA

A simple RISC-V CPU (RV32I subset) with PCIe BAR interface, targeting Intel Cyclone V FPGA.

## Architecture

```
                    PCIe
                      │
                      ▼
              ┌───────────────┐
              │  AXI-Lite     │
              │   (BAM)       │
              └───────┬───────┘
                      │ BAR access
                      ▼
              ┌───────────────┐
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
| 0x1000 | 4KB | IMEM (instruction memory) |
| 0x2000 | 8KB | DMEM (data memory, shared) |

### Control Registers

| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | CTRL | [0] RUN, [1] RESET |
| 0x08 | STATUS | [0] RUNNING |
| 0x10 | PC | Current program counter |
| 0x18 | RESULT | CPU result output |

## Supported Instructions

| Type | Instructions |
|------|--------------|
| R-type | ADD, SUB, AND, OR, XOR |
| I-type | ADDI, ANDI, ORI, XORI, LW |
| S-type | SW |
| B-type | BEQ |

## Files

```
src/
├── alu.sv          # Arithmetic Logic Unit
├── regfile.sv      # 32x32-bit Register File
├── decoder.sv      # Instruction Decoder
├── riscv_soc.sv    # SoC with BAR interface

fpga/
├── axi_core_hw.sv  # AXI wrapper for PCIe
├── axi_core_hw.tcl # Platform Designer component
├── pcie_ed.qsf     # Quartus settings
├── pcie_ed.tcl     # Platform Designer system

tb/
├── tb_cpu.cpp      # CPU testbench
├── tb_riscv_soc.cpp # SoC testbench
```

## Build & Test (Simulation)

```bash
cd tb
verilator --cc ../src/alu.sv ../src/regfile.sv ../src/decoder.sv \
          ../src/riscv_soc.sv --top-module riscv_soc \
          --exe tb_riscv_soc.cpp --build
./obj_dir/Vriscv_soc
```

## Build (FPGA)

Requires Quartus with Cyclone V support:

```bash
module load altera/quartus/20.1
./build_fpga.sh 0x01
```

## Host Usage

```c
// Load program
for (int i = 0; i < program_size; i++)
    bar->write32(0x1000 + i*4, program[i]);

// Reset and run
bar->write64(0x00, 0x02);  // RESET
bar->write64(0x00, 0x01);  // RUN

// Wait for completion (or poll PC)
sleep(1);

// Read results
uint32_t result = bar->read32(0x2000);
```

## Roadmap

- [x] Basic CPU (RV32I subset)
- [x] SoC with BAR interface
- [x] FPGA build infrastructure
- [ ] Add UART peripheral
- [ ] Add more instructions
- [ ] Hardware test on Cyclone V
