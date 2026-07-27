# RISC-V on FPGA

A minimal RISC-V CPU (RV32I subset) with PCIe BAR interface, targeting Intel Agilex 7 FPGA.

## Current Status

- **Working**: Basic 5-stage pipeline, PCIe interface, debug infrastructure
- **Clock**: SoC runs at 125MHz (from PCIe clock divider)
- **Target**: 500MHz (future optimization)

## Architecture

5-stage pipeline:

```
┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐
│ IF  │──▶│ ID  │──▶│ EX  │──▶│ MEM │──▶│ WB  │
└─────┘   └─────┘   └─────┘   └─────┘   └─────┘
  │         │         │         │         │
Fetch    Decode    Execute   Memory    Write
 IMEM    RegFile    ALU      DMEM     RegFile
```

```
              PCIe (250MHz)
                  │
          ┌───────▼───────┐
          │   PCIe IP     │
          │  (AXI-Lite)   │
          └───────┬───────┘
                  │
          ┌───────▼───────┐
          │ axi_core_hw   │
          │  + bus64to32  │
          │  + bus_sniffer│
          └───────┬───────┘
                  │
          ┌───────▼───────┐
          │  riscv_soc    │
          │  + cpu_logger │
          │  + IMEM (4KB) │
          │  + DMEM (8KB) │
          │  + RV32I CPU  │
          └───────────────┘
```

## BAR Memory Map

| Offset | Size | Description |
|--------|------|-------------|
| 0x0000 | 256B | Control registers |
| 0x1000 | 4KB  | IMEM (instruction memory) |
| 0x2000 | 8KB  | DMEM (data memory) |
| 0x4000 | 4KB  | Bus sniffer logs |
| 0x5000 | 4KB  | CPU logger logs |

### Control Registers (0x0xxx)

| Offset | Name   | Description |
|--------|--------|-------------|
| 0x00   | CTRL   | [0] RUN, [1] RESET |
| 0x08   | STATUS | [0] RUNNING |
| 0x10   | PC     | Current program counter |

### Performance Counters (0x0xxx)

| Offset | Name      | Description |
|--------|-----------|-------------|
| 0x20   | CYCLES    | Total cycles (write to clear all) |
| 0x24   | INSTRS    | Instructions retired |
| 0x28   | STALLS    | Stall cycles (load-use hazards) |
| 0x2C   | BRANCHES  | Branch instructions |
| 0x30   | BR_TAKEN  | Branches actually taken |
| 0x34   | LOADS     | Load instructions |
| 0x38   | STORES    | Store instructions |

### Bus Sniffer (0x4xxx) - Host transaction logger

| Offset | Name      | Description |
|--------|-----------|-------------|
| 0x4000 | COUNT     | Total transactions logged |
| 0x4004 | CYCLE     | Current cycle counter |
| 0x4008 | CTRL      | [0] enable, [1] clear |
| 0x4010 | ENTRY[0]  | Newest log entry (16 bytes) |
| 0x4020 | ENTRY[1]  | Second newest, etc. |

### CPU Logger (0x5xxx) - CPU memory access logger

| Offset | Name      | Description |
|--------|-----------|-------------|
| 0x5000 | COUNT     | Total accesses logged |
| 0x5004 | CYCLE     | Current cycle counter |
| 0x5008 | CTRL      | [0] enable, [1] clear |
| 0x5010 | ENTRY[0]  | Newest log entry (16 bytes) |

## Supported Instructions (Currently)

| Type   | Instructions |
|--------|--------------|
| R-type | ADD |
| I-type | ADDI |
| S-type | SW |
| B-type | BEQ |

**TODO**: Complete RV32I instruction set (~35 more instructions)

## Files

```
rtl/
  axi_core_hw.sv   # AXI-Lite slave + bus64to32 + sniffer
  bus64to32.sv     # 64-bit to 32-bit bus adapter
  bus_sniffer.sv   # Host transaction logger
  cpu_logger.sv    # CPU memory access logger
  riscv_soc.sv     # SoC wrapper with CPU + memories
  alu.sv           # Arithmetic Logic Unit
  decoder.sv       # Instruction decoder
  regfile.sv       # 32x32-bit register file

tb/
  tb_axi_core.cpp  # Integration testbench

host/
  riscv_host.c     # VFIO host program
  build.sh         # Build script
```

## Build & Test

### Simulation

```bash
cd tb
verilator --cc --top-module axi_core_hw -I../rtl ../rtl/*.sv \
          --exe tb_axi_core.cpp -CFLAGS "-std=c++17" -Wno-CASEINCOMPLETE
make -C obj_dir -f Vaxi_core_hw.mk
./obj_dir/Vaxi_core_hw
```

### FPGA Build

Requires Quartus 25.x with Agilex 7 support:

```bash
./build_fpga.sh
```

### Host Program

```bash
cd host && ./build.sh

# Setup VFIO
PCI=0000:b1:00.0
echo $PCI | sudo tee /sys/bus/pci/devices/$PCI/driver/unbind
echo vfio-pci | sudo tee /sys/bus/pci/devices/$PCI/driver_override
echo $PCI | sudo tee /sys/bus/pci/drivers/vfio-pci/bind

# Run test (12 = IOMMU group)
sudo ./riscv_host $PCI 12
```

## Test Output

```
=== Bus Sniffer Log (host transactions) ===
Total transactions: 8, Current cycle: 87021052
  [7] cycle=19490 WR addr=0x0000 data=0x00000002   ← CPU Reset
  [6] cycle=36480 WR addr=0x1000 data=0x00500093   ← ADDI x1, x0, 5
  [5] cycle=36481 WR addr=0x1004 data=0x00300113   ← ADDI x2, x0, 3
  [4] cycle=36644 WR addr=0x1008 data=0x002081B3   ← ADD x3, x1, x2
  [3] cycle=36645 WR addr=0x100C data=0x00302023   ← SW x3, 0(x0)
  [2] cycle=36809 WR addr=0x1010 data=0x00000063   ← BEQ loop
  ...

=== CPU Logger (CPU memory accesses) ===
Total accesses: 6, Current cycle: 86515210
  [5] cycle=17066 IFETCH addr=0x00000000 data=0x00500093  ← Fetch ADDI x1
  [4] cycle=17067 IFETCH addr=0x00000004 data=0x00300113  ← Fetch ADDI x2
  [3] cycle=17068 IFETCH addr=0x00000008 data=0x002081B3  ← Fetch ADD x3
  [2] cycle=17069 IFETCH addr=0x0000000C data=0x00302023  ← Fetch SW
  [1] cycle=17070 IFETCH addr=0x00000010 data=0x00000063  ← Fetch BEQ
  [0] cycle=17072 DSTORE addr=0x00000000 data=0x00000008  ← Store 8 to DMEM

DMEM[0] = 8 (expected 8)
=== TEST PASSED ===
```

## Next Steps

1. Complete RV32I instruction set
2. Add CSRs and interrupts
3. Increase clock speed toward 500MHz
4. Add cache controller
5. DDR memory integration
