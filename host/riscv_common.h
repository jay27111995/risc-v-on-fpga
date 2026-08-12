// RISC-V SoC Common Definitions
// ============================================================================
// Shared between all host test programs.
// This header just includes riscv_lib.h for convenience.
//
// BAR Memory Map:
//   0x00000 - 0x000FF : Control registers (CTRL, STATUS, PC, CYCLES, etc.)
//   0x20000 - 0x3FFFF : IMEM - 128KB instruction memory
//   0x40000 - 0x40FFF : Bus Sniffer (host transaction logger)
//   0x50000 - 0x50FFF : CPU Logger (CPU memory access logger)
//   0x80000 - 0x87FFF : DMEM - 32KB data memory
// ============================================================================

#ifndef RISCV_COMMON_H
#define RISCV_COMMON_H

#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#endif // RISCV_COMMON_H
