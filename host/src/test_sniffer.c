// Bus Sniffer Test
// ============================================================================
// Tests the bus sniffer (host transaction logger).
// Verifies write/read transactions are captured with correct data.
// ============================================================================

#include "riscv_common.h"

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "Bus Sniffer Test") < 0)
    return 1;

  printf("=== Bus Sniffer Test ===\n\n");

  // Initialize memory and loggers
  init_memory();

  int errors = 0;

  // -------------------------------------------------------------------------
  // Test 1: Basic write transactions
  // -------------------------------------------------------------------------
  printf("Test 1: Write transactions\n");
  printf("--------------------------\n");

  sniffer_clear();

  printf("Writing test patterns to DMEM...\n");
  write32(BAR_DMEM + 0x00, 0xDEADBEEF);
  write32(BAR_DMEM + 0x04, 0xCAFEBABE);
  write32(BAR_DMEM + 0x08, 0x12345678);

  usleep(1000);

  uint32_t count = sniffer_get_count();
  printf("Log count: %u\n", count);

  // Note: 64-bit BAR access causes multiple transactions per write32
  // Each write32 = read-modify-write = 2 reads + 2 writes (64-bit each)
  // So 3 writes = 3 * 4 = 12 transactions (minimum)
  if (count >= 3) {
    printf("PASS: Transactions logged\n");
  } else {
    printf("ERROR: Expected at least 3 transactions\n");
    errors++;
  }

  printf("\nLog entries (newest first):\n");
  sniffer_dump(20);

  // Verify data values appear in log
  printf("\nVerifying logged data...\n");
  int found_deadbeef = 0, found_cafebabe = 0, found_12345678 = 0;

  for (uint32_t i = 0; i < count && i < 32; i++) {
    sniffer_entry_t e;
    sniffer_read_entry(i, &e);
    if (e.data == 0xDEADBEEF)
      found_deadbeef = 1;
    if (e.data == 0xCAFEBABE)
      found_cafebabe = 1;
    if (e.data == 0x12345678)
      found_12345678 = 1;
  }

  printf("  0xDEADBEEF: %s\n", found_deadbeef ? "FOUND" : "NOT FOUND");
  printf("  0xCAFEBABE: %s\n", found_cafebabe ? "FOUND" : "NOT FOUND");
  printf("  0x12345678: %s\n", found_12345678 ? "FOUND" : "NOT FOUND");

  if (!found_deadbeef || !found_cafebabe || !found_12345678) {
    printf("ERROR: Some write data not found in log!\n");
    errors++;
  } else {
    printf("PASS: All write data captured\n");
  }
  printf("\n");

  // -------------------------------------------------------------------------
  // Test 2: Read transactions
  // -------------------------------------------------------------------------
  printf("Test 2: Read transactions\n");
  printf("-------------------------\n");

  sniffer_clear();

  printf("Reading back...\n");
  uint32_t r0 = read32(BAR_DMEM + 0x00);
  uint32_t r1 = read32(BAR_DMEM + 0x04);
  uint32_t r2 = read32(BAR_DMEM + 0x08);

  printf("  Read: 0x%08X, 0x%08X, 0x%08X\n", r0, r1, r2);

  // Verify readback values
  if (r0 == 0xDEADBEEF && r1 == 0xCAFEBABE && r2 == 0x12345678) {
    printf("PASS: Readback data correct\n");
  } else {
    printf("ERROR: Readback mismatch!\n");
    errors++;
  }

  usleep(1000);

  count = sniffer_get_count();
  printf("Log count: %u\n\n", count);

  printf("Log entries:\n");
  sniffer_dump(20);
  printf("\n");

  // -------------------------------------------------------------------------
  // Test 3: Clear functionality
  // -------------------------------------------------------------------------
  printf("Test 3: Clear functionality\n");
  printf("---------------------------\n");

  // Log should have entries from Test 2
  uint32_t before = sniffer_get_count();
  printf("Count before clear: %u\n", before);

  sniffer_clear();

  uint32_t after = sniffer_get_count();
  printf("Count after clear: %u (expected 0)\n", after);

  if (after != 0) {
    printf("ERROR: Clear didn't reset count!\n");
    errors++;
  } else {
    printf("PASS: Clear works\n");
  }

  // Verify logging resumes
  write32(BAR_DMEM + 0x00, 0x11111111);
  usleep(100);

  if (sniffer_get_count() > 0) {
    printf("PASS: Logging resumed after clear\n");
  } else {
    printf("ERROR: Logging didn't resume!\n");
    errors++;
  }
  printf("\n");

  // -------------------------------------------------------------------------
  // Test 4: Write data verification in log
  // -------------------------------------------------------------------------
  printf("Test 4: Write data in log\n");
  printf("-------------------------\n");

  sniffer_clear();

  // Write to specific addresses
  write32(BAR_DMEM + 0x100, 0xAAAAAAAA);
  write32(BAR_DMEM + 0x200, 0xBBBBBBBB);

  usleep(1000);

  // Check that the WRITE transactions have correct data
  // (ignore the RMW reads which show old data)
  count = sniffer_get_count();
  int found_aaaa = 0, found_bbbb = 0;

  for (uint32_t i = 0; i < count && i < 20; i++) {
    sniffer_entry_t e;
    sniffer_read_entry(i, &e);
    if (e.is_write && e.data == 0xAAAAAAAA)
      found_aaaa = 1;
    if (e.is_write && e.data == 0xBBBBBBBB)
      found_bbbb = 1;
  }

  printf("Write 0xAAAAAAAA: %s\n", found_aaaa ? "FOUND" : "NOT FOUND");
  printf("Write 0xBBBBBBBB: %s\n", found_bbbb ? "FOUND" : "NOT FOUND");

  if (found_aaaa && found_bbbb) {
    printf("PASS: Write data captured correctly\n");
  } else {
    printf("ERROR: Write data not found in log!\n");
    errors++;
  }
  printf("\n");

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
