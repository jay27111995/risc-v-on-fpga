// Program Tests (sum, factorial)
// ============================================================================
// Loads and runs C programs compiled from sw/ directory.
// Verifies results and shows CPU log.
// ============================================================================

#include "riscv_common.h"

static int test_sum(void) {
  printf("=== Test: sum.c ===\n");
  printf("Computes sum(1..10) = 55\n\n");

  cpu_reset();
  init_memory();

  // Try to load sum.bin
  const char *paths[] = {"../sw/sum.bin", "sw/sum.bin", "sum.bin", NULL};
  int loaded = 0;
  for (int i = 0; paths[i]; i++) {
    if (load_program_file(paths[i]) > 0) {
      loaded = 1;
      break;
    }
  }

  if (!loaded) {
    printf("  ERROR: sum.bin not found. Build with: cd sw && ./build.sh sum.c\n");
    return 0;
  }

  cpu_run();
  usleep(10000); // sum.c uses while(1), not EBREAK
  cpu_stop();

  // Check results
  printf("\nDMEM results:\n");
  printf("  [0] sum(1..10) = %d (expected 55)\n", read_dmem(0));
  printf("  [1] input      = %d (expected 10)\n", read_dmem(1));
  printf("  [2] marker     = 0x%X (expected 0xDEAD)\n", read_dmem(2));
  printf("  [3] sum + 5    = %d (expected 60)\n", read_dmem(3));
  printf("  [4] sum - 5    = %d (expected 50)\n", read_dmem(4));
  printf("  [5] sum >> 1   = %d (expected 27)\n", read_dmem(5));
  printf("  [6] sum << 2   = %d (expected 220)\n", read_dmem(6));

  uint32_t count = cpulog_get_count();
  printf("\nCPU Log (%u entries, showing stores):\n", count);
  cpulog_dump(32);

  printf("\n");
  print_perf_counters();

  int pass = (read_dmem(0) == 55);
  printf("=== %s ===\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_factorial(void) {
  printf("=== Test: factorial.c ===\n");
  printf("Computes factorial(5) = 120\n\n");

  cpu_reset();
  init_memory();

  // Try to load factorial.bin
  const char *paths[] = {"../sw/factorial.bin", "sw/factorial.bin",
                         "factorial.bin", NULL};
  int loaded = 0;
  for (int i = 0; paths[i]; i++) {
    if (load_program_file(paths[i]) > 0) {
      loaded = 1;
      break;
    }
  }

  if (!loaded) {
    printf("  ERROR: factorial.bin not found. Build with: cd sw && ./build.sh "
           "factorial.c\n");
    return 0;
  }

  cpu_run();
  usleep(10000);
  cpu_stop();

  // Check result (factorial stores result in dmem[0])
  uint32_t result = read_dmem(0);
  printf("\nDMEM[0] = %d (expected 120 for 5!)\n", result);

  uint32_t count = cpulog_get_count();
  printf("\nCPU Log (%u entries):\n", count);
  cpulog_dump(32);

  printf("\n");
  print_perf_counters();

  int pass = (result == 120);
  printf("=== %s ===\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "Program Tests") < 0)
    return 1;

  int sum_pass = test_sum();
  int fact_pass = test_factorial();

  printf("================================================================================\n");
  printf("Summary:\n");
  printf("  sum.c:       %s\n", sum_pass ? "PASS" : "FAIL");
  printf("  factorial.c: %s\n", fact_pass ? "PASS" : "FAIL");
  printf("================================================================================\n");

  common_cleanup();
  return (sum_pass && fact_pass) ? 0 : 1;
}
