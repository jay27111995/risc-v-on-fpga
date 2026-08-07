# RISC-V Software Build Environment

This directory contains the toolchain setup for compiling C programs for the RISC-V SoC.

## Requirements

- RISC-V GCC toolchain (RV32I)
- Available from Quartus RiscFree: `/opt/ALTERA/quartuspro/25.3.1/riscfree/toolchain/riscv32-unknown-elf/bin/`

## Quick Start

```bash
./build.sh sum2.c
```

This produces:
- `sum2.elf` - ELF executable
- `sum2.bin` - Raw binary (load to FPGA)
- `sum2.hex` - Verilog hex format
- `sum2.dis` - Disassembly listing

## Files

| File | Description |
|------|-------------|
| `build.sh` | Build script - compiles and links programs |
| `link.ld` | Linker script - memory layout |
| `start.S` | Startup code - jumps to `_start` |
| `sum2.c` | Example: sum(1..10) = 55 |
| `sum.c` | Example (original, has issues) |
| `factorial.c` | Example (needs M extension) |

## Memory Map (CPU's view)

```
0x00000000 - 0x0001FFFF : IMEM (128KB) - Code executes here
0x00000000 - 0x00007FFF : DMEM (32KB)  - Data memory (overlapped address space)
```

The hardware routes accesses based on type:
- Instruction fetch → IMEM
- Load/store → DMEM

## Writing Programs

### Basic Structure

```c
// Use inline assembly for direct DMEM access
#define DMEM_WRITE(addr, val) \
    __asm__ volatile ("sw %0, %1(zero)" : : "r"(val), "i"(addr))

void _start(void) {
    // Your code here
    
    DMEM_WRITE(0, result);  // Store to DMEM[0]
    
    while(1);  // Halt
}
```

### Important Notes

1. **Entry point**: `_start` is called from `start.S`
2. **No standard library**: Use `-nostdlib -ffreestanding`
3. **No multiply/divide**: RV32I only (no M extension)
4. **Direct memory access**: Use inline asm or volatile pointers

### Compiler Flags

```
-march=rv32i      # RV32I base instruction set only
-mabi=ilp32       # 32-bit ABI
-O2               # Optimization
-nostdlib         # No standard library
-nostartfiles     # No default startup
-ffreestanding    # Freestanding environment
```

## Example: sum2.c

```c
#define DMEM_WRITE(addr, val) \
    __asm__ volatile ("sw %0, %1(zero)" : : "r"(val), "i"(addr))

int sum_to_n(int n) {
    int result = 0;
    for (int i = 1; i <= n; i++) {
        result += i;
    }
    return result;
}

void _start(void) {
    int result = sum_to_n(10);  // 55
    
    DMEM_WRITE(0, result);      // DMEM[0] = 55
    DMEM_WRITE(4, 10);          // DMEM[1] = 10
    DMEM_WRITE(8, 0xDEAD);      // DMEM[2] = marker
    
    while(1);
}
```

## Running on FPGA

```bash
# Build
./build.sh sum2.c

# Load and run (from host/ directory)
cd ../host
sudo ./loader ../sw/sum2.bin 0000:b1:00.0 12
```

Expected output:
```
=== DMEM Contents ===
  DMEM[ 0] =         55 (0x00000037)
  DMEM[ 1] =         10 (0x0000000A)
  DMEM[ 2] =      57005 (0x0000DEAD)

sum.bin: PASSED (sum(1..10) = 55)
```

## Debugging Tips

1. **Check disassembly**: Look at `.dis` file to verify code layout
2. **Verify _start at 0x0**: First instruction should be `j _start`
3. **Use markers**: Write known values (0xDEAD) to verify execution path
4. **Check PC**: Loader shows final PC - should be in your halt loop

## Limitations

- No multiply/divide (add M extension to CPU for this)
- No floating point
- No interrupts (no CSR support yet)
- No standard library functions
