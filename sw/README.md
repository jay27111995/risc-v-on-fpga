# RISC-V Software

C programs for the RISC-V CPU.

## Building

```bash
./build.sh <program.c>
./build.sh uart_test.c
```

Produces: `.elf`, `.bin`, `.hex`, `.dis`

## Toolchain

Uses Quartus RiscFree GCC:
```
/opt/ALTERA/quartuspro/25.3.1/riscfree/toolchain/riscv32-unknown-elf/bin/
```

## Files

| File | Description |
|------|-------------|
| `uart.h` | UART library (putc/getc) |
| `uart_test.c` | Echo test - type chars, they echo back |
| `sum.c` | Sum 1..10 |
| `factorial.c` | Factorial (needs M extension) |
| `start.S` | Startup code |
| `link.ld` | Linker script |
| `build.sh` | Build script |

## UART API

```c
#include "uart.h"

uart_putc('H');           // Send char (blocking)
char c = uart_getc();     // Receive char (blocking)
```

## Running

```bash
# Build
./build.sh uart_test.c

# Run (from host/ directory)
cd ../host
sudo ./bin/loader --no-run ../sw/uart_test.bin $PCIE_EP $GRP
sudo ./bin/uart_console $PCIE_EP $GRP
```

## Memory Map (CPU view)

| Address | Description |
|---------|-------------|
| 0x00000-0x1FFFF | IMEM (code) |
| 0x00000-0x07FFF | DMEM (data) |
| 0x100 | TX_BUF |
| 0x204 | TX_READY |
| 0x300 | RX_BUF |
| 0x404 | RX_READY |

## Compiler Flags

```
-march=rv32im     # RV32I + M extension
-mabi=ilp32       # 32-bit ABI  
-O2               # Optimization
-nostdlib         # No standard library
-ffreestanding    # Freestanding environment
```
