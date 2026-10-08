# RISC-V on FPGA

> **Note**: This project was developed with AI assistance (Claude/Kiro).

A complete RV32IM RISC-V CPU with PCIe BAR interface and virtual UART, targeting Intel Agilex 7 FPGA.

## Status

- **RV32IM CPU**: ✅ Working (all base + M extension instructions)
- **Machine Mode**: ✅ Working (CSRs, timer interrupts, traps, WFI)

## TODO

- [ ] Integrate riscv-arch-test compliance suite
- [ ] Fix multiplier timing (add pipeline stage for 250 MHz closure)
- [ ] Zephyr RTOS board support

## Project Structure

```
├── rtl/          # SystemVerilog RTL (CPU, SoC, memories)
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

## Running Tests

```bash
# Run all tests (builds host tools and SW, then runs tests)
./run_tests.py

# Skip build, just run tests
./run_tests.py --skip-build

# Run specific test
./run_tests.py -t ecall_test

# Verbose output
./run_tests.py -v -t ecall_test

# Multiple specific tests
./run_tests.py -t ecall_test -t wfi_test
```

Tests print `===END===` as end marker. The script reads UART output until it sees this marker.

## Specs

| Feature | Value |
|---------|-------|
| ISA | RV32IM |
| Pipeline | 6-stage (IF→ID→EX1→EX2→MEM→WB) |
| Clock | 250 MHz |
| IMEM | 128 KB |
| DMEM | 32 KB |
| Target | Intel Agilex 7 (AGIB027R29A1E1VB) |
| Interface | PCIe Gen4 x16 |

## License

MIT
