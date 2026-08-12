// CPU Logger Test
// ============================================================================
// Tests the CPU logger with IMEM tracing enabled.
// Runs a small program and verifies the complete instruction + data trace.
// ============================================================================

#include "riscv_common.h"

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "CPU Logger Test") < 0)
    return 1;

  printf("=== CPU Logger Test (with IMEM tracing) ===\n\n");

  int errors = 0;

  // -------------------------------------------------------------------------
  // Test 1: DMEM-only logging (default)
  // -------------------------------------------------------------------------
  printf("Test 1: DMEM-only mode\n");
  printf("----------------------\n");

  cpu_reset();
  cpulog_clear(); // DMEM only (log_imem=0)

  // Initialize memory
  init_memory();

  // Simple program: store 42 to dmem[0], load it back
  uint32_t prog1[] = {
      0x02A00093, // ADDI x1, x0, 42
      0x00102023, // SW x1, 0(x0)
      0x00002103, // LW x2, 0(x0)
      0x00100073, // EBREAK
  };

  for (size_t i = 0; i < sizeof(prog1) / sizeof(prog1[0]); i++) {
    write_imem(i, prog1[i]);
  }

  cpu_run();
  cpu_wait_halt(100);
  cpu_stop();

  uint32_t count = cpulog_get_count();
  printf("Log count: %u\n", count);

  if (count < 2) {
    printf("ERROR: Expected at least 2 entries (DSTORE + DLOAD)\n");
    errors++;
  } else {
    printf("PASS: DMEM accesses logged\n");
  }

  printf("Log entries:\n");
  cpulog_dump(10);
  printf("\n");

  // -------------------------------------------------------------------------
  // Test 2: Full logging with IMEM
  // -------------------------------------------------------------------------
  printf("Test 2: Full logging (IMEM + DMEM)\n");
  printf("----------------------------------\n");

  cpu_reset();

  // Initialize memory (clears logger with DMEM-only)
  init_memory();

  // Enable IMEM logging after init
  cpulog_clear_with_imem();

  // Simple test program: compute 5+3, store to dmem[0]
  uint32_t prog2[] = {
      0x00500093, // 0x00: ADDI x1, x0, 5
      0x00300113, // 0x04: ADDI x2, x0, 3
      0x002081B3, // 0x08: ADD  x3, x1, x2
      0x00302023, // 0x0C: SW   x3, 0(x0)
      0x00100073, // 0x10: EBREAK
  };

  printf("Loading program:\n");
  printf("  0x00: 0x%08X  ADDI x1, x0, 5\n", prog2[0]);
  printf("  0x04: 0x%08X  ADDI x2, x0, 3\n", prog2[1]);
  printf("  0x08: 0x%08X  ADD  x3, x1, x2\n", prog2[2]);
  printf("  0x0C: 0x%08X  SW   x3, 0(x0)\n", prog2[3]);
  printf("  0x10: 0x%08X  EBREAK\n\n", prog2[4]);

  for (size_t i = 0; i < sizeof(prog2) / sizeof(prog2[0]); i++) {
    write_imem(i, prog2[i]);
  }

  cpu_run();
  int halted = cpu_wait_halt(100);
  cpu_stop();

  if (halted) {
    printf("CPU halted (EBREAK) - PASS\n");
  } else {
    printf("CPU timed out - FAIL\n");
    errors++;
  }

  // Verify result
  uint32_t result = read_dmem(0);
  printf("DMEM[0] = %u (expected 8)\n", result);
  if (result != 8) {
    printf("ERROR: Result mismatch!\n");
    errors++;
  }

  count = cpulog_get_count();
  printf("Log count: %u\n\n", count);

  printf("CPU Log (newest first):\n");
  cpulog_dump(20);

  // Verify log entries contain correct instruction data
  printf("\nVerifying logged instruction data...\n");

  // Read entries and check
  // Note: Order is newest first, so entry 0 = EBREAK, entry 5 = first ADDI
  int log_errors = 0;

  // We expect something like:
  // [0] IFETCH 0x10 = EBREAK
  // [1] DSTORE 0x00 = 8
  // [2] IFETCH 0x0C = SW
  // [3] IFETCH 0x08 = ADD
  // [4] IFETCH 0x04 = ADDI x2
  // [5] IFETCH 0x00 = ADDI x1

  if (count >= 6) {
    cpulog_entry_t e;

    // Check DSTORE entry (should be entry 1 or nearby)
    int found_store = 0;
    for (uint32_t i = 0; i < count && i < 10; i++) {
      cpulog_read_entry(i, &e);
      if (e.type == CPULOG_TYPE_DSTORE && e.address == 0 && e.data == 8) {
        found_store = 1;
        printf("  Found DSTORE: addr=0x%05X data=%u - OK\n", e.address, e.data);
        break;
      }
    }
    if (!found_store) {
      printf("  ERROR: DSTORE entry not found or wrong data!\n");
      log_errors++;
    }

    // Check for IFETCH entries with correct instruction data
    int found_addi1 = 0, found_addi2 = 0, found_add = 0, found_sw = 0,
        found_ebreak = 0;
    for (uint32_t i = 0; i < count && i < 10; i++) {
      cpulog_read_entry(i, &e);
      if (e.type == CPULOG_TYPE_IFETCH) {
        if (e.data == 0x00500093)
          found_addi1 = 1;
        if (e.data == 0x00300113)
          found_addi2 = 1;
        if (e.data == 0x002081B3)
          found_add = 1;
        if (e.data == 0x00302023)
          found_sw = 1;
        if (e.data == 0x00100073)
          found_ebreak = 1;
      }
    }

    printf("  IFETCH 0x00500093 (ADDI x1): %s\n", found_addi1 ? "OK" : "NOT FOUND");
    printf("  IFETCH 0x00300113 (ADDI x2): %s\n", found_addi2 ? "OK" : "NOT FOUND");
    printf("  IFETCH 0x002081B3 (ADD):     %s\n", found_add ? "OK" : "NOT FOUND");
    printf("  IFETCH 0x00302023 (SW):      %s\n", found_sw ? "OK" : "NOT FOUND");
    printf("  IFETCH 0x00100073 (EBREAK):  %s\n", found_ebreak ? "OK" : "NOT FOUND");

    if (!found_addi1 || !found_addi2 || !found_add || !found_sw || !found_ebreak) {
      printf("  ERROR: Some instructions not logged correctly!\n");
      log_errors++;
    }
  } else {
    printf("  ERROR: Not enough log entries!\n");
    log_errors++;
  }

  errors += log_errors;

  printf("\n");
  print_perf_counters();

  // -------------------------------------------------------------------------
  // Summary
  // -------------------------------------------------------------------------
  printf("================================================================================\n");
  if (errors == 0) {
    printf("=== ALL TESTS PASSED ===\n");
  } else {
    printf("=== FAILED: %d errors ===\n", errors);
  }
  printf("================================================================================\n");

  common_cleanup();
  return errors ? 1 : 0;
}
