// RISC-V RV32I Instruction Tests
// ============================================================================
// Tests complete RV32I instruction set (37 instructions).
// ============================================================================

#include "riscv_common.h"

// ----------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------

static void write_dmem64(uint32_t idx, uint64_t val) {
  write_dmem(idx * 2, (uint32_t)val);
  write_dmem(idx * 2 + 1, (uint32_t)(val >> 32));
}

static void load_program(const uint32_t *prog, int n) {
  for (int i = 0; i < n; i++) {
    write_imem(i, prog[i]);
  }
}

static int run_test(const char *name, const uint32_t *prog, int prog_len,
                    int dmem_word, uint32_t expected, const char *desc) {
  printf("=== %s ===\n", name);
  if (desc)
    printf("  %s\n", desc);

  cpu_reset();
  init_memory();
  load_program(prog, prog_len);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t result = read_dmem(dmem_word);
  int pass = (result == expected);
  printf("  Result: %u (expected %u) - %s\n\n", result, expected,
         pass ? "PASS" : "FAIL");
  return pass;
}

static int run_test_hex(const char *name, const uint32_t *prog, int prog_len,
                        int dmem_word, uint32_t expected, const char *desc) {
  printf("=== %s ===\n", name);
  if (desc)
    printf("  %s\n", desc);

  cpu_reset();
  load_program(prog, prog_len);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t result = read_dmem(dmem_word);
  int pass = (result == expected);
  printf("  Result: 0x%08X (expected 0x%08X) - %s\n\n", result, expected,
         pass ? "PASS" : "FAIL");
  return pass;
}

// ----------------------------------------------------------------------------
// RV32I Tests
// ----------------------------------------------------------------------------

static int test_basic_alu(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0x00300113, // ADDI x2, x0, 3
      0x002081B3, // ADD  x3, x1, x2
      0x00302023, // SW   x3, 0(x0)
      0x00000063, // BEQ  x0, x0, 0 (loop)
  };
  return run_test("Test 1: Basic ALU", prog, 5, 0, 8, "5 + 3 = 8");
}

static int test_bne(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFF08093, // ADDI x1, x1, -1
      0xFE1010E3, // BNE  x1, x0, -4
      0x00102223, // SW   x1, 4(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 2: BNE (count down)", prog, 5, 1, 0, "Count 5->0");
}

static int test_blt(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5
      0x00300113, // ADDI x2, x0, 3
      0x00100193, // ADDI x3, x0, 1
      0x0020C463, // BLT  x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302423, // SW   x3, 8(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 3: BLT (signed)", prog, 7, 2, 1, "-5 < 3 = true");
}

static int test_bltu(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5 (= 0xFFFFFFFB)
      0x00300113, // ADDI x2, x0, 3
      0x00100193, // ADDI x3, x0, 1
      0x0020E463, // BLTU x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302623, // SW   x3, 12(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 4: BLTU (unsigned)", prog, 7, 3, 2, "0xFFFFFFFB > 3");
}

static int test_bge(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFD00113, // ADDI x2, x0, -3
      0x00100193, // ADDI x3, x0, 1
      0x0020D463, // BGE  x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302823, // SW   x3, 16(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 5: BGE (signed)", prog, 7, 4, 1, "5 >= -3 = true");
}

static int test_bgeu(void) {
  uint32_t prog[] = {
      0x00500093, // ADDI x1, x0, 5
      0xFFD00113, // ADDI x2, x0, -3 (= 0xFFFFFFFD)
      0x00100193, // ADDI x3, x0, 1
      0x0020F463, // BGEU x1, x2, 8
      0x00200193, // ADDI x3, x0, 2
      0x00302A23, // SW   x3, 20(x0)
      0x00000063, // BEQ  x0, x0, 0
  };
  return run_test("Test 6: BGEU (unsigned)", prog, 7, 5, 2, "5 < 0xFFFFFFFD");
}

static int test_shifts(void) {
  uint32_t prog[] = {
      0x00800093, // ADDI x1, x0, 8
      0x00200113, // ADDI x2, x0, 2
      0x002091B3, // SLL  x3, x1, x2  (8 << 2 = 32)
      0x0020D233, // SRL  x4, x1, x2  (8 >> 2 = 2)
      0x80000293, // ADDI x5, x0, -2048
      0x4022D333, // SRA  x6, x5, x2  (0xFFFFF800 >>> 2)
      0x00302C23, // SW   x3, 24(x0)
      0x00402E23, // SW   x4, 28(x0)
      0x02602023, // SW   x6, 32(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 7: Shifts (SLL, SRL, SRA) ===\n");
  cpu_reset();
  load_program(prog, 10);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t sll = read_dmem(6);
  uint32_t srl = read_dmem(7);
  uint32_t sra = read_dmem(8);

  printf("  SLL: %u (expected 32)\n", sll);
  printf("  SRL: %u (expected 2)\n", srl);
  printf("  SRA: 0x%08X (expected 0xFFFFFE00)\n", sra);

  int pass = (sll == 32 && srl == 2 && sra == 0xFFFFFE00);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_imm_shifts(void) {
  uint32_t prog[] = {
      0x00100093, // ADDI x1, x0, 1
      0x01009113, // SLLI x2, x1, 16  (1 << 16 = 65536)
      0x00815193, // SRLI x3, x2, 8   (65536 >> 8 = 256)
      0x80000213, // ADDI x4, x0, -2048
      0x40425293, // SRAI x5, x4, 4   (0xFFFFF800 >>> 4)
      0x02202223, // SW   x2, 36(x0)
      0x02302423, // SW   x3, 40(x0)
      0x02502623, // SW   x5, 44(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 8: Immediate Shifts ===\n");
  cpu_reset();
  load_program(prog, 9);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t slli = read_dmem(9);
  uint32_t srli = read_dmem(10);
  uint32_t srai = read_dmem(11);

  printf("  SLLI: %u (expected 65536)\n", slli);
  printf("  SRLI: %u (expected 256)\n", srli);
  printf("  SRAI: 0x%08X (expected 0xFFFFFF80)\n", srai);

  int pass = (slli == 65536 && srli == 256 && srai == 0xFFFFFF80);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_slt(void) {
  uint32_t prog[] = {
      0xFFB00093, // ADDI x1, x0, -5
      0x00300113, // ADDI x2, x0, 3
      0x0020A1B3, // SLT  x3, x1, x2  (-5 < 3 signed = 1)
      0x0020B233, // SLTU x4, x1, x2  (0xFFFFFFFB < 3 unsigned = 0)
      0x02302823, // SW   x3, 48(x0)
      0x02402A23, // SW   x4, 52(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  printf("=== Test 9: SLT/SLTU ===\n");
  cpu_reset();
  load_program(prog, 7);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t slt = read_dmem(12);
  uint32_t sltu = read_dmem(13);

  printf("  SLT:  %u (expected 1, -5 < 3 signed)\n", slt);
  printf("  SLTU: %u (expected 0, 0xFFFFFFFB > 3 unsigned)\n", sltu);

  int pass = (slt == 1 && sltu == 0);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_jal(void) {
  uint32_t prog[] = {
      0x00500093, // 0x00: ADDI x1, x0, 5
      0x00C000EF, // 0x04: JAL x1, 12 (jump to 0x10, x1 = 8)
      0x02102C23, // 0x08: SW x1, 56(x0)
      0x00000063, // 0x0C: BEQ x0, x0, 0
      0x00000013, // 0x10: NOP
      0xFF5FF06F, // 0x14: JAL x0, -12 (back to 0x08)
  };
  return run_test("Test 10: JAL", prog, 6, 14, 8, "Return addr = PC+4 = 8");
}

static int test_jalr(void) {
  uint32_t prog[] = {
      0x00300513, // 0x00: ADDI x10, x0, 3
      0x00C000EF, // 0x04: JAL x1, 12 (call 0x10)
      0x02A02E23, // 0x08: SW x10, 60(x0)
      0x00000063, // 0x0C: BEQ x0, x0, 0
      0x00550513, // 0x10: ADDI x10, x10, 5
      0x00008067, // 0x14: JALR x0, x1, 0 (return)
  };
  return run_test("Test 11: JALR", prog, 6, 15, 8, "3 + 5 = 8");
}

static int test_lui(void) {
  uint32_t prog[] = {
      0xDEADB0B7, // LUI x1, 0xDEADB
      0x04102023, // SW  x1, 64(x0)
      0x00000063, // BEQ x0, x0, 0
  };
  return run_test_hex("Test 12: LUI", prog, 3, 16, 0xDEADB000, NULL);
}

static int test_auipc(void) {
  uint32_t prog[] = {
      0x00001097, // AUIPC x1, 1 (x1 = PC + 0x1000 = 0x1000)
      0x04102223, // SW    x1, 68(x0)
      0x00000063, // BEQ   x0, x0, 0
  };
  return run_test_hex("Test 13: AUIPC", prog, 3, 17, 0x1000, "PC + 0x1000");
}

static int test_byte_ops(void) {
  printf("=== Test 14: Byte operations (SB, LB, LBU) ===\n");
  cpu_reset();

  uint32_t prog[] = {
      0x0FF00093, // ADDI x1, x0, 255
      0x04800113, // ADDI x2, x0, 72
      0x00110023, // SB   x1, 0(x2)
      0x00010183, // LB   x3, 0(x2) (sign extend)
      0x00014203, // LBU  x4, 0(x2) (zero extend)
      0x04302423, // SW   x3, 72(x0)
      0x04402623, // SW   x4, 76(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  load_program(prog, 8);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t lb = read_dmem(18);
  uint32_t lbu = read_dmem(19);

  printf("  LB:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lb);
  printf("  LBU: 0x%08X (expected 0x000000FF, zero extended)\n", lbu);

  int pass = (lb == 0xFFFFFFFF && lbu == 0x000000FF);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

static int test_halfword_ops(void) {
  printf("=== Test 15: Halfword operations (SH, LH, LHU) ===\n");
  cpu_reset();

  uint32_t prog[] = {
      0xFFFF0137, // LUI  x2, 0xFFFF0
      0xFFF10113, // ADDI x2, x2, -1  ; x2 = 0xFFFF_FFFF
      0x05000193, // ADDI x3, x0, 80
      0x00219023, // SH   x2, 0(x3)
      0x00019203, // LH   x4, 0(x3)  (sign extend)
      0x0001D283, // LHU  x5, 0(x3)  (zero extend)
      0x04402823, // SW   x4, 80(x0)
      0x04502A23, // SW   x5, 84(x0)
      0x00000063, // BEQ  x0, x0, 0
  };

  load_program(prog, 9);
  cpu_run();
  usleep(10000);
  cpu_stop();

  uint32_t lh = read_dmem(20);
  uint32_t lhu = read_dmem(21);

  printf("  LH:  0x%08X (expected 0xFFFFFFFF, sign extended)\n", lh);
  printf("  LHU: 0x%08X (expected 0x0000FFFF, zero extended)\n", lhu);

  int pass = (lh == 0xFFFFFFFF && lhu == 0x0000FFFF);
  printf("  %s\n\n", pass ? "PASS" : "FAIL");
  return pass;
}

// ----------------------------------------------------------------------------
// Main
// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  if (common_init(argc, argv, "RISC-V RV32I Instruction Tests") < 0)
    return 1;

  // Clear DMEM
  for (int i = 0; i < 32; i++)
    write_dmem64(i, 0);

  // Run RV32I tests
  int pass = 0;
  pass += test_basic_alu();
  pass += test_bne();
  pass += test_blt();
  pass += test_bltu();
  pass += test_bge();
  pass += test_bgeu();
  pass += test_shifts();
  pass += test_imm_shifts();
  pass += test_slt();
  pass += test_jal();
  pass += test_jalr();
  pass += test_lui();
  pass += test_auipc();
  pass += test_byte_ops();
  pass += test_halfword_ops();

  printf("================================================================================\n");
  printf("RV32I Summary: %d/15 tests passed\n", pass);
  printf("================================================================================\n");

  print_perf_counters();

  common_cleanup();
  return (pass == 15) ? 0 : 1;
}
