# RTL - RISC-V CPU and SoC

## Architecture

6-stage pipelined RV32IM CPU with machine-mode support:
```
IF → ID → EX1 → EX2 → MEM → WB
```

## Files

| File | Description |
|------|-------------|
| `riscv_soc.sv` | Top-level SoC (CPU + memories + CSRs + timers) |
| `decoder.sv` | Instruction decoder (RV32IM + system instructions) |
| `alu.sv` | ALU (arithmetic, logic, shifts) |
| `multiplier.sv` | Pipelined 32x32 multiplier (2-cycle) |
| `divider.sv` | Multi-cycle divider (32 cycles) |
| `regfile.sv` | 32x32-bit register file |
| `dmem.sv` | Data memory (32KB BRAM) |
| `cpu_logger.sv` | Debug: instruction trace logger |
| `bus_sniffer.sv` | Debug: bus transaction logger |
| `bus64to32.sv` | AXI 64-bit to 32-bit adapter |
| `axi_core_hw.sv` | PCIe AXI-Lite to SoC bridge |

## Features

### ISA Support
- RV32I base integer instructions
- M extension (MUL, MULH, MULHSU, MULHU, DIV, DIVU, REM, REMU)
- System instructions: ECALL, EBREAK, MRET, WFI, FENCE, FENCE.I

### Machine Mode CSRs
| CSR | Address | Description |
|-----|---------|-------------|
| mstatus | 0x300 | Machine status (MIE, MPIE) |
| mie | 0x304 | Interrupt enable |
| mtvec | 0x305 | Trap vector base |
| mscratch | 0x340 | Scratch register |
| mepc | 0x341 | Exception PC |
| mcause | 0x342 | Trap cause |
| mtval | 0x343 | Trap value |
| mip | 0x344 | Interrupt pending |
| mcycle | 0xB00 | Cycle counter (low) |
| mcycleh | 0xB80 | Cycle counter (high) |
| minstret | 0xB02 | Instructions retired (low) |
| minstreth | 0xB82 | Instructions retired (high) |

### Timer
- 64-bit mtime counter (increments every cycle)
- 64-bit mtimecmp compare register
- Timer interrupt when mtime >= mtimecmp

## Memory Map (Host via PCIe BAR)

| Offset | Size | Description |
|--------|------|-------------|
| 0x00000 | 256B | Control registers |
| 0x20000 | 128KB | IMEM (instruction memory) |
| 0x50000 | 4KB | CPU Logger |
| 0x80000 | 32KB | DMEM (data memory) |

### Control Registers
| Offset | Name | Description |
|--------|------|-------------|
| 0x00 | CTRL | [0]=RUN, [1]=RESET |
| 0x08 | STATUS | [0]=RUNNING, [1]=HALTED |
| 0x10 | PC | Current program counter |
| 0x20 | CYCLES | Cycle count |
| 0x24 | INSTRS | Instructions retired |

## Building

Requires Intel Quartus Pro 25.1+ with Agilex 7 support.

```bash
cd ~/rtl_compile/risc-v-on-fpga
./build_fpga.sh
```
