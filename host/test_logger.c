// CPU Logger Test
// ============================================================================
// Tests the CPU logger with IMEM tracing enabled.
// Runs a small program and shows complete instruction + data trace.
// ============================================================================

#include "riscv_common.h"

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "CPU Logger Test") < 0)
    return 1;

  printf("=== CPU Logger Test (with IMEM tracing) ===\n\n");

  cpu_reset();
  cpulog_clear_with_imem(); // Enable IMEM logging

  // Simple test program: compute 5+3, store to dmem[0]
  // This lets us see every instruction fetch and the store
  printf("Loading test program...\n");
  uint32_t prog[] = {
      0x00500093, // 0x00: ADDI x1, x0, 5     ; x1 = 5
      0x00300113, // 0x04: ADDI x2, x0, 3     ; x2 = 3
      0x002081B3, // 0x08: ADD  x3, x1, x2    ; x3 = x1 + x2 = 8
      0x00302023, // 0x0C: SW   x3, 0(x0)     ; dmem[0] = 8
      0x00100073, // 0x10: EBREAK             ; halt
  };

  for (size_t i = 0; i < sizeof(prog) / sizeof(prog[0]); i++) {
    write_imem(i, prog[i]);
  }

  printf("Program loaded (%zu instructions)\n\n", sizeof(prog) / sizeof(prog[0]));

  printf("Expected trace:\n");
  printf("  IFETCH 0x00: ADDI x1, x0, 5\n");
  printf("  IFETCH 0x04: ADDI x2, x0, 3\n");
  printf("  IFETCH 0x08: ADD  x3, x1, x2\n");
  printf("  IFETCH 0x0C: SW   x3, 0(x0)\n");
  printf("  DSTORE 0x00: data = 8\n");
  printf("  IFETCH 0x10: EBREAK\n\n");

  cpu_run();
  int halted = cpu_wait_halt(100);
  cpu_stop();

  if (halted) {
    printf("CPU halted (EBREAK)\n");
  } else {
    printf("CPU timed out!\n");
  }

  // Verify result
  uint32_t result = read_dmem(0);
  printf("DMEM[0] = %u (expected 8)\n\n", result);

  uint32_t count = cpulog_get_count();
  printf("Log count: %u\n\n", count);

  if (count > 0) {
    printf("CPU Log (newest first):\n");
    cpulog_dump(256);
  }

  printf("\n");
  print_perf_counters();

  int pass = (result == 8 && halted);
  printf("=== %s ===\n", pass ? "PASS" : "FAIL");

  common_cleanup();
  return pass ? 0 : 1;
}
