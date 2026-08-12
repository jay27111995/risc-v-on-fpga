# RISC-V Host Tools

Host-side programs for interacting with the RISC-V SoC over PCIe.

## Building

```bash
bash build.sh
```

Builds all programs with `-O2 -Wall` (no warnings allowed).

## Programs

### riscv_host

**RV32I instruction test suite.** Tests all 37 base instructions.

```bash
sudo ./riscv_host $PCI $GRP
```

Runs individual tests for each instruction type:
- Arithmetic (ADD, SUB, ADDI, etc.)
- Logical (AND, OR, XOR, shifts)
- Comparisons (SLT, SLTU, branches)
- Memory (LB, LH, LW, SB, SH, SW)
- Control flow (JAL, JALR, branches)
- Upper immediate (LUI, AUIPC)

### loader

**Binary loader.** Loads a `.bin` file to IMEM and runs it.

```bash
sudo ./loader <program.bin> [pci_addr] [iommu_group] [run_time_ms]
sudo ./loader ../sw/sum.bin 0000:b1:00.0 12 100
```

Features:
- Initializes IMEM with EBREAK (trap on uninitialized fetch)
- Clears DMEM to zero
- Shows first 8 instructions
- Verifies IMEM readback
- Displays DMEM results after run

### test_logger

**CPU logger test.** Tests the CPU memory access logger with IMEM tracing.

```bash
sudo ./test_logger $PCI $GRP
```

Tests:
1. **DMEM-only mode**: Logs data loads/stores only
2. **Full logging**: Logs instruction fetches + data accesses

Verifies that logged instruction opcodes match the program.

### test_sniffer

**Bus sniffer test.** Tests the host transaction logger.

```bash
sudo ./test_sniffer $PCI $GRP
```

Tests:
1. Write transactions captured
2. Read transactions captured
3. Clear functionality
4. Write data verification

Note: 64-bit BAR causes read-modify-write behavior (4 bus transactions per 32-bit write).

### test_programs

**Program runner.** Loads and runs C programs from `sw/` directory.

```bash
sudo ./test_programs $PCI $GRP
```

Runs:
- `sum.c` - Computes sum(1..10) = 55
- `factorial.c` - Computes factorial(5) = 120

Shows CPU log entries and performance counters.

## Library

### riscv_lib.h / riscv_lib.c

Common library used by all programs:

**Memory Functions:**
- `write_imem(idx, val)` / `read_imem(idx)` - IMEM access
- `write_dmem(idx, val)` / `read_dmem(idx)` - DMEM access
- `init_imem()` - Fill IMEM with EBREAK instructions
- `init_dmem()` - Clear DMEM to zero
- `init_memory()` - Both + clear loggers

**CPU Control:**
- `cpu_reset()` - Reset CPU
- `cpu_run()` / `cpu_stop()` - Start/stop CPU
- `cpu_is_halted()` - Check if EBREAK hit
- `cpu_wait_halt(timeout_ms)` - Wait for EBREAK

**Bus Sniffer:**
- `sniffer_clear()` - Clear and enable
- `sniffer_get_count()` - Get entry count
- `sniffer_read_entry(idx, &entry)` - Read entry
- `sniffer_dump(max)` - Print entries

**CPU Logger:**
- `cpulog_clear()` - Clear (DMEM only)
- `cpulog_clear_with_imem()` - Clear and enable IMEM tracing
- `cpulog_get_count()` - Get entry count
- `cpulog_read_entry(idx, &entry)` - Read entry
- `cpulog_dump(max)` - Print entries

**Utilities:**
- `common_init(argc, argv, name)` - VFIO setup
- `common_cleanup()` - Cleanup
- `print_perf_counters()` - Show cycle/instruction counts
- `load_program_file(filename)` - Load .bin to IMEM

## BAR Memory Map

```
0x00000 - 0x000FF : Control registers
0x20000 - 0x3FFFF : IMEM (128KB)
0x40000 - 0x40FFF : Bus sniffer
0x50000 - 0x50FFF : CPU logger
0x80000 - 0x87FFF : DMEM (32KB)
```

## Logger Entry Format (128 bits)

```
[127:96] - data      (32 bits)
[95:32]  - timestamp (64 bits, cycles since clear)
[31:20]  - reserved  (12 bits)
[19:2]   - address   (18 bits, word-aligned)
[1:0]    - type      (2 bits: 00=IFETCH, 01=DLOAD, 10=DSTORE)
```

## VFIO Setup

```bash
# Find FPGA
lspci | grep -i '1172\|beef'

# Bind to VFIO
PCI=0000:b1:00.0
echo $PCI | sudo tee /sys/bus/pci/devices/$PCI/driver/unbind 2>/dev/null
echo vfio-pci | sudo tee /sys/bus/pci/devices/$PCI/driver_override
echo $PCI | sudo tee /sys/bus/pci/drivers/vfio-pci/bind

# Get IOMMU group
GRP=$(basename $(readlink /sys/bus/pci/devices/$PCI/iommu_group))

# Run (needs root for VFIO)
sudo ./riscv_host $PCI $GRP
```
