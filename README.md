# RISC-V on FPGA

A complete RV32I RISC-V CPU with PCIe BAR interface, targeting Intel Agilex 7 FPGA.

## Status: ✅ Working

All 37 RV32I base instructions implemented and verified on hardware.

- **Target**: Intel Agilex 7 (AGF014)
- **Clock**: 250 MHz (PCIe clock domain)
- **Pipeline**: 5-stage (IF → ID → EX → MEM → WB)
- **Memories**: 128KB IMEM, 32KB DMEM

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

## Files

```
rtl/
  riscv_soc.sv     # Top-level SoC (CPU + memories + control)
  axi_core_hw.sv   # AXI-Lite slave wrapper
  cpu_logger.sv    # Debug: CPU memory access logger

host/
  riscv_host.c     # VFIO test program (15 instruction tests)
  Makefile         # Build script

build/
  pcie_ed.qpf      # Quartus project
  *.sof            # FPGA bitstream (after build)
```

## Build

### FPGA Bitstream

Requires Quartus 25.x with Agilex 7 support:

```bash
./build_fpga.sh
```

Output: `build/riscv-soc-revid-*.sof`

### Host Program

```bash
cd host && make
```

## Run on FPGA

### 1. Program FPGA

```bash
quartus_pgm -c 1 -m jtag -o "p;build/riscv-soc-*.sof"
```

### 2. Setup VFIO

```bash
PCI=0000:b1:00.0  # Find with lspci
GROUP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))

echo $PCI | sudo tee /sys/bus/pci/devices/$PCI/driver/unbind
echo vfio-pci | sudo tee /sys/bus/pci/devices/$PCI/driver_override
echo $PCI | sudo tee /sys/bus/pci/drivers/vfio-pci/bind
```

### 3. Run Tests

```bash
sudo ./host/riscv_host $PCI $GROUP
```

## Test Output

```
RISC-V SoC Test
===============
PCI: 0000:b1:00.0, IOMMU group: 12

BAR0 mapped at 0x7aa00dc00000, size 8388608 bytes

=== Test 1: Basic ALU ===
Loading program...
Running CPU...
  DMEM[0] = 8 (expected 8)
  Test 1: PASSED

=== Test 2: BNE (count down) ===
  DMEM[4] = 0 (expected 0)
  Test 2: PASSED

... (13 more tests) ...

=== Test Summary ===
  Passed: 15/15

=== ALL TESTS PASSED ===
```

## Timing

Current build has minor timing violation (-0.305 ns) on cpu_logger debug path at worst-case corner. Does not affect functional operation.

| Clock Domain | Target | Achieved | Status |
|--------------|--------|----------|--------|
| PCIe pld_clkout | 250 MHz | 250 MHz | ⚠️ -0.3ns slack (debug path) |
| PCIe pld_clkout_slow | 125 MHz | 233 MHz | ✅ |

## Next Steps

- [ ] Add CSRs (MTVEC, MEPC, MCAUSE) for interrupts
- [ ] Implement M extension (MUL, DIV)
- [ ] Boot Zephyr RTOS
- [ ] Add instruction cache
- [ ] DDR memory controller

## License

MIT
