# RISC-V on FPGA

A complete RV32I RISC-V CPU with PCIe BAR interface, targeting Intel Agilex 7 FPGA.

## Status: ✅ Working

All 37 RV32I base instructions implemented and verified on hardware.
**C programs compiled with GCC run correctly on the FPGA!**

- **Target**: Intel Agilex 7 (AGF014)
- **Clock**: 250 MHz (PCIe clock domain)
- **Pipeline**: 5-stage (IF → ID → EX → MEM → WB)
- **Memories**: 128KB IMEM, 32KB DMEM
- **Bitstream**: `riscv-soc-revid-0x2a-git-889988e-md5-d7fed8669d33b7202e660c61af79cef3.sof`

## Quick Start

```bash
# 1. Program FPGA
quartus_pgm -c 1 -m jtag -o "p;riscv-soc-*.sof"

# 2. Build a C program
cd sw && ./build.sh sum2.c

# 3. Setup VFIO and run
cd host
sudo ./loader ../sw/sum2.bin 0000:b1:00.0 12
```

Expected output:
```
=== DMEM Contents ===
  DMEM[ 0] =         55 (0x00000037)   ← sum(1..10) = 55
  DMEM[ 1] =         10 (0x0000000A)
  DMEM[ 2] =      57005 (0x0000DEAD)   ← marker

sum.bin: PASSED (sum(1..10) = 55)
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
| 0x08 | STATUS | [0] RUNNING |
| 0x10 | PC | Current program counter |
| 0x20 | CYCLES | Cycle count (write to clear) |

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

## Directory Structure

```
├── rtl/                    # Verilog/SystemVerilog RTL
│   ├── riscv_soc.sv       # Top-level SoC (CPU + memories)
│   ├── axi_core_hw.sv     # AXI-Lite slave wrapper
│   ├── bus64to32.sv       # 64-to-32 bit bus adapter
│   ├── bus_sniffer.sv     # Host transaction logger
│   └── cpu_logger.sv      # CPU memory access logger
│
├── sw/                     # Software/firmware
│   ├── build.sh           # Build script (uses Quartus RiscFree GCC)
│   ├── link.ld            # Linker script
│   ├── start.S            # Startup assembly (jump to _start)
│   └── sum2.c             # Example: sum(1..10) test program
│
├── host/                   # Host-side tools
│   ├── riscv_host.c       # Instruction test suite (15 tests)
│   ├── loader.c           # Binary loader for .bin files
│   └── Makefile
│
├── tb/                     # Verilator testbenches
│   ├── tb_axi_core.cpp    # Main testbench (30 tests)
│   └── ...
│
├── build/                  # Quartus build output
│   └── *.sof              # FPGA bitstream
│
└── *.sof                   # Working bitstream (checked in)
```

## Building Software

See [sw/README.md](sw/README.md) for details on compiling C programs.

```bash
cd sw
./build.sh sum2.c    # Builds sum2.bin
```

## Running on FPGA

### 1. Program FPGA

```bash
quartus_pgm -c 1 -m jtag -o "p;riscv-soc-*.sof"
```

### 2. Setup VFIO

```bash
PCI=0000:b1:00.0  # Find with lspci
GROUP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))

echo $PCI | sudo tee /sys/bus/pci/devices/$PCI/driver/unbind
echo vfio-pci | sudo tee /sys/bus/pci/devices/$PCI/driver_override
echo $PCI | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
```

### 3. Run Tests or Load Programs

```bash
# Run instruction tests
cd host && make
sudo ./riscv_host $PCI $GROUP

# Or load a custom program
sudo ./loader ../sw/sum2.bin $PCI $GROUP
```

## Simulation

```bash
module load verilator/5.024
cd tb
verilator --cc --top-module axi_core_hw -I../rtl ../rtl/*.sv \
    --exe tb_axi_core.cpp -CFLAGS "-std=c++17"
make -C obj_dir -f Vaxi_core_hw.mk
./obj_dir/Vaxi_core_hw
```

## Timing

Current build has minor timing violation (-0.305 ns) on cpu_logger debug path at worst-case corner. Does not affect functional operation.

| Clock Domain | Target | Achieved | Status |
|--------------|--------|----------|--------|
| PCIe pld_clkout | 250 MHz | 250 MHz | ⚠️ -0.3ns slack (debug path) |
| PCIe pld_clkout_slow | 125 MHz | 233 MHz | ✅ |

## Next Steps

- [ ] Add M extension (MUL, DIV)
- [ ] Add CSRs for interrupts (MTVEC, MEPC, MCAUSE)
- [ ] Boot Zephyr RTOS
- [ ] Add instruction cache
- [ ] DDR memory controller

## License

MIT
