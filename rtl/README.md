# RTL - RISC-V CPU and SoC

## Architecture

6-stage pipelined RV32IM CPU:
```
IF → ID → EX1 → EX2 → MEM → WB
```

## Files

| File | Description |
|------|-------------|
| `riscv_core.sv` | CPU core (fetch, decode, execute, writeback) |
| `riscv_soc.sv` | Top-level SoC (CPU + memories + PCIe interface) |
| `imem.sv` | Instruction memory (128KB BRAM) |
| `dmem.sv` | Data memory (32KB BRAM) |
| `alu.sv` | ALU with M extension (mul/div) |
| `regfile.sv` | 32x32-bit register file |
| `hazard.sv` | Hazard detection and forwarding |
| `axi_*.sv` | PCIe AXI interface |

## Memory Map (PCIe BAR2)

| Offset | Size | Description |
|--------|------|-------------|
| 0x000000 | 128KB | IMEM |
| 0x100000 | 32KB | DMEM |
| 0x200000 | - | Control/Status registers |

## Building

Requires Intel Quartus Pro 25.1+ with Agilex 7 support.

```bash
cd ~/rtl_compile/risc-v-on-fpga
quartus_sh --flow compile riscv_soc
```
