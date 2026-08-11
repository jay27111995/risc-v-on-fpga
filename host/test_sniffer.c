// Bus Sniffer Test
// ============================================================================
// Tests the bus sniffer (host transaction logger).
// ============================================================================

#include "riscv_common.h"

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "Bus Sniffer Test") < 0)
    return 1;

  printf("=== Bus Sniffer Test ===\n\n");

  sniffer_clear();

  // Generate some transactions
  printf("Writing test patterns...\n");
  write32(BAR_DMEM + 0x00, 0xDEADBEEF);
  write32(BAR_DMEM + 0x04, 0xCAFEBABE);
  write32(BAR_DMEM + 0x08, 0x12345678);

  printf("Reading back...\n");
  uint32_t r0 = read32(BAR_DMEM + 0x00);
  uint32_t r1 = read32(BAR_DMEM + 0x04);
  uint32_t r2 = read32(BAR_DMEM + 0x08);

  printf("  Read: 0x%08X, 0x%08X, 0x%08X\n\n", r0, r1, r2);

  usleep(1000);

  uint32_t count = sniffer_get_count();
  printf("Log count: %u\n\n", count);

  if (count > 0) {
    printf("Transactions (newest first):\n");
    sniffer_dump(64);
  }

  printf("\n");
  int pass = (count >= 6); // 3 writes + 3 reads
  printf("=== %s ===\n", pass ? "PASS" : "FAIL");

  common_cleanup();
  return pass ? 0 : 1;
}
