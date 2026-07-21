# RISC-V on FPGA

A simple RISC-V CPU (RV32I subset) implemented in SystemVerilog, targeting FPGA deployment.

## Architecture

```
         ┌─────────────────────────────────────────────────────────────┐
         │                                                             │
         ▼                                                             │
      ┌────┐      ┌──────┐      ┌─────────┐      ┌─────────┐          │
      │ PC │─────►│ IMEM │─────►│ Decoder │─────►│ Regfile │          │
      └────┘      └──────┘      └────┬────┘      └────┬────┘          │
         ▲                           │                │               │
         │                           │          rs1_data  rs2_data    │
         │                           ▼                ▼       │       │
         │                      ┌─────────┐       ┌───────┐   │       │
         │            imm ─────►│   MUX   │──────►│  ALU  │   │       │
         │                      └─────────┘       └───┬───┘   │       │
         │                                            │       │       │
         │                                            ▼       ▼       │
         │                                       ┌──────────────┐     │
         │                                       │     DMEM     │     │
         │                                       └──────┬───────┘     │
         │                                              │             │
         │                                       ┌──────┴──────┐      │
         │                                       │     MUX     ├──────┘
         │                                       └─────────────┘
         │                                           rd_data
         └──────────────── branch ───────────────────────┘
```

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
├── alu.sv       # Arithmetic Logic Unit
├── regfile.sv   # 32x32-bit Register File (x0 = 0)
├── pc.sv        # Program Counter
├── imem.sv      # Instruction Memory (ROM)
├── dmem.sv      # Data Memory (RAM)
├── decoder.sv   # Instruction Decoder
└── cpu.sv       # Top-level CPU

tb/
├── tb_cpu.cpp   # CPU testbench
└── tb_dmem.cpp  # Data memory testbench
```

## Build & Test

Requires Verilator:

```bash
cd tb
verilator --cc ../src/alu.sv ../src/regfile.sv ../src/pc.sv \
          ../src/imem.sv ../src/decoder.sv ../src/dmem.sv \
          ../src/cpu.sv --top-module cpu --exe tb_cpu.cpp --build
./obj_dir/Vcpu
```

## Roadmap

- [ ] Add more branch instructions (BNE, BLT, BGE)
- [ ] Add jump instructions (JAL, JALR)
- [ ] Add shift instructions (SLL, SRL, SRA)
- [ ] Add UART peripheral
- [ ] Synthesize on Cyclone V FPGA
- [ ] Run bare-metal C code
