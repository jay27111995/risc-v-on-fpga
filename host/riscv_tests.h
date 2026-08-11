// RISC-V Test Programs and Expected Results
// ============================================================================
// Shared between hardware test (riscv_host.c) and simulation (tb_host_sim.cpp)
// ============================================================================

#ifndef RISCV_TESTS_H
#define RISCV_TESTS_H

#include <stdint.h>
#include <stddef.h>

// ----------------------------------------------------------------------------
// Test Program Structure
// ----------------------------------------------------------------------------

typedef struct {
    const char *name;
    const uint32_t *program;
    size_t prog_size;
    uint32_t dmem_word;      // Which DMEM word to check
    uint32_t expected;       // Expected value
    int expect_hex;          // Print as hex (for LUI, AUIPC, etc.)
    const char *desc;        // Optional description
} riscv_test_t;

// ----------------------------------------------------------------------------
// Test Programs
// ----------------------------------------------------------------------------

// Test 1: Basic ALU (5 + 3 = 8)
static const uint32_t prog_basic_alu[] = {
    0x00500093,  // ADDI x1, x0, 5
    0x00300113,  // ADDI x2, x0, 3
    0x002081B3,  // ADD  x3, x1, x2
    0x00302023,  // SW   x3, 0(x0)
    0x00000063,  // BEQ  x0, x0, 0 (loop)
};

// Test 2: BNE count down (5 -> 0)
static const uint32_t prog_bne[] = {
    0x00500093,  // ADDI x1, x0, 5
    0xFFF08093,  // ADDI x1, x1, -1
    0xFE1010E3,  // BNE  x1, x0, -4
    0x00102223,  // SW   x1, 4(x0)
    0x00000063,  // BEQ  x0, x0, 0
};

// Test 3: BLT signed (-5 < 3)
static const uint32_t prog_blt[] = {
    0xFFB00093,  // ADDI x1, x0, -5
    0x00300113,  // ADDI x2, x0, 3
    0x00100193,  // ADDI x3, x0, 1
    0x0020C463,  // BLT  x1, x2, 8
    0x00200193,  // ADDI x3, x0, 2
    0x00302423,  // SW   x3, 8(x0)
    0x00000063,  // BEQ  x0, x0, 0
};

// Test 4: BLTU unsigned (0xFFFFFFFB > 3)
static const uint32_t prog_bltu[] = {
    0xFFB00093,  // ADDI x1, x0, -5 (= 0xFFFFFFFB)
    0x00300113,  // ADDI x2, x0, 3
    0x00100193,  // ADDI x3, x0, 1
    0x0020E463,  // BLTU x1, x2, 8
    0x00200193,  // ADDI x3, x0, 2
    0x00302623,  // SW   x3, 12(x0)
    0x00000063,  // BEQ  x0, x0, 0
};

// Test 5: BGE signed (5 >= -3)
static const uint32_t prog_bge[] = {
    0x00500093,  // ADDI x1, x0, 5
    0xFFD00113,  // ADDI x2, x0, -3
    0x00100193,  // ADDI x3, x0, 1
    0x0020D463,  // BGE  x1, x2, 8
    0x00200193,  // ADDI x3, x0, 2
    0x00302823,  // SW   x3, 16(x0)
    0x00000063,  // BEQ  x0, x0, 0
};

// Test 6: BGEU unsigned (5 < 0xFFFFFFFD)
static const uint32_t prog_bgeu[] = {
    0x00500093,  // ADDI x1, x0, 5
    0xFFD00113,  // ADDI x2, x0, -3 (= 0xFFFFFFFD)
    0x00100193,  // ADDI x3, x0, 1
    0x0020F463,  // BGEU x1, x2, 8
    0x00200193,  // ADDI x3, x0, 2
    0x00302A23,  // SW   x3, 20(x0)
    0x00000063,  // BEQ  x0, x0, 0
};

// Test 7: JAL
static const uint32_t prog_jal[] = {
    0x00500093,  // 0x00: ADDI x1, x0, 5
    0x00C000EF,  // 0x04: JAL x1, 12 (jump to 0x10, x1 = 8)
    0x02102C23,  // 0x08: SW x1, 56(x0)
    0x00000063,  // 0x0C: BEQ x0, x0, 0
    0x00000013,  // 0x10: NOP
    0xFF5FF06F,  // 0x14: JAL x0, -12 (back to 0x08)
};

// Test 8: JALR
static const uint32_t prog_jalr[] = {
    0x00300513,  // 0x00: ADDI x10, x0, 3
    0x00C000EF,  // 0x04: JAL x1, 12 (call 0x10)
    0x02A02E23,  // 0x08: SW x10, 60(x0)
    0x00000063,  // 0x0C: BEQ x0, x0, 0
    0x00550513,  // 0x10: ADDI x10, x10, 5
    0x00008067,  // 0x14: JALR x0, x1, 0 (return)
};

// Test 9: LUI
static const uint32_t prog_lui[] = {
    0xDEADB0B7,  // LUI x1, 0xDEADB
    0x04102023,  // SW  x1, 64(x0)
    0x00000063,  // BEQ x0, x0, 0
};

// Test 10: AUIPC
static const uint32_t prog_auipc[] = {
    0x00001097,  // AUIPC x1, 1 (x1 = PC + 0x1000 = 0x1000)
    0x04102223,  // SW    x1, 68(x0)
    0x00000063,  // BEQ   x0, x0, 0
};

// ----------------------------------------------------------------------------
// Test Table
// ----------------------------------------------------------------------------

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const riscv_test_t riscv_tests[] = {
    {"Basic ALU",        prog_basic_alu, ARRAY_SIZE(prog_basic_alu), 0,  8,          0, "5 + 3 = 8"},
    {"BNE count down",   prog_bne,       ARRAY_SIZE(prog_bne),       1,  0,          0, "Count 5->0"},
    {"BLT signed",       prog_blt,       ARRAY_SIZE(prog_blt),       2,  1,          0, "-5 < 3 = true"},
    {"BLTU unsigned",    prog_bltu,      ARRAY_SIZE(prog_bltu),      3,  2,          0, "0xFFFFFFFB > 3"},
    {"BGE signed",       prog_bge,       ARRAY_SIZE(prog_bge),       4,  1,          0, "5 >= -3 = true"},
    {"BGEU unsigned",    prog_bgeu,      ARRAY_SIZE(prog_bgeu),      5,  2,          0, "5 < 0xFFFFFFFD"},
    {"JAL",              prog_jal,       ARRAY_SIZE(prog_jal),       14, 8,          0, "Return addr = 8"},
    {"JALR",             prog_jalr,      ARRAY_SIZE(prog_jalr),      15, 8,          0, "3 + 5 = 8"},
    {"LUI",              prog_lui,       ARRAY_SIZE(prog_lui),       16, 0xDEADB000, 1, NULL},
    {"AUIPC",            prog_auipc,     ARRAY_SIZE(prog_auipc),     17, 0x1000,     1, "PC + 0x1000"},
};

#define RISCV_TEST_COUNT ARRAY_SIZE(riscv_tests)

// ----------------------------------------------------------------------------
// Multi-result Tests (shifts, SLT, byte/halfword ops)
// These need special handling - multiple DMEM locations checked
// ----------------------------------------------------------------------------

// Test: Shifts (SLL, SRL, SRA)
static const uint32_t prog_shifts[] = {
    0x00800093,  // ADDI x1, x0, 8
    0x00200113,  // ADDI x2, x0, 2
    0x002091B3,  // SLL  x3, x1, x2  (8 << 2 = 32)
    0x0020D233,  // SRL  x4, x1, x2  (8 >> 2 = 2)
    0x80000293,  // ADDI x5, x0, -2048
    0x4022D333,  // SRA  x6, x5, x2  (0xFFFFF800 >>> 2)
    0x00302C23,  // SW   x3, 24(x0)
    0x00402E23,  // SW   x4, 28(x0)
    0x02602023,  // SW   x6, 32(x0)
    0x00000063,  // BEQ  x0, x0, 0
};
// Expected: dmem[6]=32, dmem[7]=2, dmem[8]=0xFFFFFE00

// Test: Immediate shifts (SLLI, SRLI, SRAI)
static const uint32_t prog_imm_shifts[] = {
    0x00100093,  // ADDI x1, x0, 1
    0x01009113,  // SLLI x2, x1, 16  (1 << 16 = 65536)
    0x00815193,  // SRLI x3, x2, 8   (65536 >> 8 = 256)
    0x80000213,  // ADDI x4, x0, -2048
    0x40425293,  // SRAI x5, x4, 4   (0xFFFFF800 >>> 4)
    0x02202223,  // SW   x2, 36(x0)
    0x02302423,  // SW   x3, 40(x0)
    0x02502623,  // SW   x5, 44(x0)
    0x00000063,  // BEQ  x0, x0, 0
};
// Expected: dmem[9]=65536, dmem[10]=256, dmem[11]=0xFFFFFF80

// Test: SLT/SLTU
static const uint32_t prog_slt[] = {
    0xFFB00093,  // ADDI x1, x0, -5
    0x00300113,  // ADDI x2, x0, 3
    0x0020A1B3,  // SLT  x3, x1, x2  (-5 < 3 signed = 1)
    0x0020B233,  // SLTU x4, x1, x2  (0xFFFFFFFB < 3 unsigned = 0)
    0x02302823,  // SW   x3, 48(x0)
    0x02402A23,  // SW   x4, 52(x0)
    0x00000063,  // BEQ  x0, x0, 0
};
// Expected: dmem[12]=1, dmem[13]=0

// Test: SB/LB/LBU
static const uint32_t prog_byte_ops[] = {
    0x0FF00093,  // ADDI x1, x0, 255
    0x04800113,  // ADDI x2, x0, 72
    0x00110023,  // SB   x1, 0(x2)
    0x00010183,  // LB   x3, 0(x2) (sign extend)
    0x00014203,  // LBU  x4, 0(x2) (zero extend)
    0x04302423,  // SW   x3, 72(x0)
    0x04402623,  // SW   x4, 76(x0)
    0x00000063,  // BEQ  x0, x0, 0
};
// Expected: dmem[18]=0xFFFFFFFF, dmem[19]=0x000000FF

// Test: SH/LH/LHU
static const uint32_t prog_halfword_ops[] = {
    0xFFF00093,  // ADDI x1, x0, -1
    0x05000113,  // ADDI x2, x0, 80
    0x00111023,  // SH   x1, 0(x2)
    0x00011183,  // LH   x3, 0(x2) (sign extend)
    0x00015203,  // LHU  x4, 0(x2) (zero extend)
    0x04302823,  // SW   x3, 80(x0)
    0x04402A23,  // SW   x4, 84(x0)
    0x00000063,  // BEQ  x0, x0, 0
};
// Expected: dmem[20]=0xFFFFFFFF, dmem[21]=0x0000FFFF

// Test: CPU Logger test program
static const uint32_t prog_cpulog_test[] = {
    0x00A00093,  // ADDI x1, x0, 10
    0x00102023,  // SW   x1, 0(x0)      <- DSTORE
    0x00002103,  // LW   x2, 0(x0)      <- DLOAD
    0x00210133,  // ADD  x2, x2, x2
    0x00202223,  // SW   x2, 4(x0)      <- DSTORE
    0x00402183,  // LW   x3, 4(x0)      <- DLOAD
    0xDEA00213,  // ADDI x4, x0, 0xDEA
    0x00402423,  // SW   x4, 8(x0)      <- DSTORE
    0x00000013,  // NOP
    0x00000013,  // NOP
    0x00100073,  // EBREAK
};

#endif // RISCV_TESTS_H
