# RISC-V on FPGA

> **Note**: This project was developed with AI assistance (Claude/Kiro).

A complete RV32IM RISC-V CPU with PCIe BAR interface and virtual UART, targeting Intel Agilex 7 FPGA.

## Status

- **RV32IM CPU**: ✅ Working (all base + M extension instructions)

## Project Structure

```
├── rtl/          # Verilog RTL (CPU, SoC, memories)
├── sw/           # Software (libc, examples, build tools)
├── host/         # Host tools (elf_loader, uart_console)
├── tb/           # Testbench (Verilator)
└── *.sof         # FPGA bitstream
```

See README in each folder for details.

## Quick Start

```bash
# Build a program
cd sw
./build.sh examples/hello_sum.c

# Load and run
cd ..
sudo host/bin/elf_loader sw/build/hello_sum.elf 0000:b1:00.0 12
sudo host/bin/uart_console 0000:b1:00.0 12
```

## Specs

| Feature | Value |
|---------|-------|
| ISA | RV32IM |
| Pipeline | 6-stage (IF→ID→EX1→EX2→MEM→WB) |
| Clock | 250 MHz |
| IMEM | 128 KB |
| DMEM | 32 KB |
| Target | Intel Agilex 7 (AGF014) |
| Interface | PCIe Gen3 x4 |

## License

MIT
