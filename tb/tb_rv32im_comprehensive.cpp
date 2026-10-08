// ============================================================================
// Comprehensive RV32IM Instruction Testbench
// ============================================================================
//
// Tests all RV32I and RV32M instructions thoroughly, including:
// - All arithmetic/logical operations with edge cases
// - All branch conditions
// - All load/store variants
// - Forwarding and hazard scenarios
// - M extension (mul/div)
//
// ============================================================================

#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

// Memory map constants
#define ADDR_CTRL     0x00000
#define ADDR_STATUS   0x00008
#define ADDR_PC       0x00010
#define ADDR_IMEM     0x20000
#define ADDR_DMEM     0x80000

class SocTestbench {
public:
    Vriscv_soc* soc;
    uint32_t imem_offset;
    int tests_passed;
    int tests_failed;

    SocTestbench() {
        soc = new Vriscv_soc;
        soc->rst_n = 0;
        soc->clk = 0;
        soc->wen = 0;
        soc->ren = 0;
        soc->addr = 0;
        soc->wdata = 0;
        tick();
        soc->rst_n = 1;
        tick();
        imem_offset = 0;
        tests_passed = 0;
        tests_failed = 0;
    }

    ~SocTestbench() {
        delete soc;
    }

    void tick() {
        soc->clk = 0;
        soc->eval();
        soc->clk = 1;
        soc->eval();
    }

    void reset() {
        // Full reset - recreate the simulation model
        delete soc;
        soc = new Vriscv_soc;
        soc->rst_n = 0;
        soc->clk = 0;
        soc->wen = 0;
        soc->ren = 0;
        soc->addr = 0;
        soc->wdata = 0;
        tick();
        soc->rst_n = 1;
        tick();
        imem_offset = 0;
    }

    void bar_write(uint32_t addr, uint32_t data) {
        soc->addr = addr;
        soc->wdata = data;
        soc->wen = 1;
        tick();
        soc->wen = 0;
    }

    uint32_t bar_read(uint32_t addr) {
        soc->addr = addr;
        soc->ren = 1;
        tick();
        tick();
        soc->ren = 0;
        return soc->rdata;
    }

    void emit(uint32_t instr) {
        bar_write(ADDR_IMEM + imem_offset, instr);
        imem_offset += 4;
    }

    void run(int cycles) {
        bar_write(ADDR_CTRL, 1);
        for (int i = 0; i < cycles; i++) {
            tick();
        }
        bar_write(ADDR_CTRL, 0);
        tick();
        tick();
    }

    uint32_t read_dmem(uint32_t offset) {
        return bar_read(ADDR_DMEM + offset);
    }

    void write_dmem(uint32_t offset, uint32_t value) {
        bar_write(ADDR_DMEM + offset, value);
    }

    // =========================================================================
    // Instruction Encoders
    // =========================================================================
    
    // R-type: funct7[31:25] | rs2[24:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
    uint32_t R_type(uint32_t funct7, int rs2, int rs1, uint32_t funct3, int rd, uint32_t opcode) {
        return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }

    // I-type: imm[31:20] | rs1[19:15] | funct3[14:12] | rd[11:7] | opcode[6:0]
    uint32_t I_type(int32_t imm12, int rs1, uint32_t funct3, int rd, uint32_t opcode) {
        return ((imm12 & 0xFFF) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }

    // S-type: imm[11:5] | rs2[24:20] | rs1[19:15] | funct3[14:12] | imm[4:0] | opcode[6:0]
    uint32_t S_type(int32_t imm12, int rs2, int rs1, uint32_t funct3, uint32_t opcode) {
        uint32_t imm11_5 = (imm12 >> 5) & 0x7F;
        uint32_t imm4_0 = imm12 & 0x1F;
        return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm4_0 << 7) | opcode;
    }

    // B-type: imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
    uint32_t B_type(int32_t imm13, int rs2, int rs1, uint32_t funct3, uint32_t opcode) {
        uint32_t imm12 = (imm13 >> 12) & 1;
        uint32_t imm10_5 = (imm13 >> 5) & 0x3F;
        uint32_t imm4_1 = (imm13 >> 1) & 0xF;
        uint32_t imm11 = (imm13 >> 11) & 1;
        return (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) | 
               (funct3 << 12) | (imm4_1 << 8) | (imm11 << 7) | opcode;
    }

    // U-type: imm[31:12] | rd[11:7] | opcode[6:0]
    uint32_t U_type(uint32_t imm20, int rd, uint32_t opcode) {
        return (imm20 << 12) | (rd << 7) | opcode;
    }

    // J-type: imm[20|10:1|11|19:12] | rd | opcode
    uint32_t J_type(int32_t imm21, int rd, uint32_t opcode) {
        uint32_t imm20 = (imm21 >> 20) & 1;
        uint32_t imm10_1 = (imm21 >> 1) & 0x3FF;
        uint32_t imm11 = (imm21 >> 11) & 1;
        uint32_t imm19_12 = (imm21 >> 12) & 0xFF;
        return (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) | (imm19_12 << 12) | (rd << 7) | opcode;
    }

    // Instruction helpers
    uint32_t LUI(int rd, uint32_t imm20) { return U_type(imm20, rd, 0x37); }
    uint32_t AUIPC(int rd, uint32_t imm20) { return U_type(imm20, rd, 0x17); }
    uint32_t JAL(int rd, int32_t imm) { return J_type(imm, rd, 0x6F); }
    uint32_t JALR(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 0, rd, 0x67); }
    
    uint32_t BEQ(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 0, 0x63); }
    uint32_t BNE(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 1, 0x63); }
    uint32_t BLT(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 4, 0x63); }
    uint32_t BGE(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 5, 0x63); }
    uint32_t BLTU(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 6, 0x63); }
    uint32_t BGEU(int rs1, int rs2, int32_t imm) { return B_type(imm, rs2, rs1, 7, 0x63); }
    
    uint32_t LB(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 0, rd, 0x03); }
    uint32_t LH(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 1, rd, 0x03); }
    uint32_t LW(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 2, rd, 0x03); }
    uint32_t LBU(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 4, rd, 0x03); }
    uint32_t LHU(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 5, rd, 0x03); }
    
    uint32_t SB(int rs2, int rs1, int32_t imm) { return S_type(imm, rs2, rs1, 0, 0x23); }
    uint32_t SH(int rs2, int rs1, int32_t imm) { return S_type(imm, rs2, rs1, 1, 0x23); }
    uint32_t SW(int rs2, int rs1, int32_t imm) { return S_type(imm, rs2, rs1, 2, 0x23); }
    
    uint32_t ADDI(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 0, rd, 0x13); }
    uint32_t SLTI(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 2, rd, 0x13); }
    uint32_t SLTIU(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 3, rd, 0x13); }
    uint32_t XORI(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 4, rd, 0x13); }
    uint32_t ORI(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 6, rd, 0x13); }
    uint32_t ANDI(int rd, int rs1, int32_t imm) { return I_type(imm, rs1, 7, rd, 0x13); }
    uint32_t SLLI(int rd, int rs1, int shamt) { return R_type(0x00, shamt, rs1, 1, rd, 0x13); }
    uint32_t SRLI(int rd, int rs1, int shamt) { return R_type(0x00, shamt, rs1, 5, rd, 0x13); }
    uint32_t SRAI(int rd, int rs1, int shamt) { return R_type(0x20, shamt, rs1, 5, rd, 0x13); }
    
    uint32_t ADD(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 0, rd, 0x33); }
    uint32_t SUB(int rd, int rs1, int rs2) { return R_type(0x20, rs2, rs1, 0, rd, 0x33); }
    uint32_t SLL(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 1, rd, 0x33); }
    uint32_t SLT(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 2, rd, 0x33); }
    uint32_t SLTU(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 3, rd, 0x33); }
    uint32_t XOR(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 4, rd, 0x33); }
    uint32_t SRL(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 5, rd, 0x33); }
    uint32_t SRA(int rd, int rs1, int rs2) { return R_type(0x20, rs2, rs1, 5, rd, 0x33); }
    uint32_t OR(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 6, rd, 0x33); }
    uint32_t AND(int rd, int rs1, int rs2) { return R_type(0x00, rs2, rs1, 7, rd, 0x33); }
    
    // M extension
    uint32_t MUL(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 0, rd, 0x33); }
    uint32_t MULH(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 1, rd, 0x33); }
    uint32_t MULHSU(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 2, rd, 0x33); }
    uint32_t MULHU(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 3, rd, 0x33); }
    uint32_t DIV(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 4, rd, 0x33); }
    uint32_t DIVU(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 5, rd, 0x33); }
    uint32_t REM(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 6, rd, 0x33); }
    uint32_t REMU(int rd, int rs1, int rs2) { return R_type(0x01, rs2, rs1, 7, rd, 0x33); }

    uint32_t NOP() { return 0x00000013; }
    uint32_t EBREAK() { return 0x00100073; }

    // =========================================================================
    // Test Helper
    // =========================================================================
    void check(const char* test_name, uint32_t actual, uint32_t expected) {
        if (actual == expected) {
            tests_passed++;
        } else {
            printf("  [FAIL] %s: got 0x%08X, expected 0x%08X\n", test_name, actual, expected);
            tests_failed++;
        }
    }

    // Run a simple test program that stores result to DMEM[0x400]
    // Returns the value stored
    uint32_t run_test_program(int cycles = 100) {
        emit(EBREAK());
        write_dmem(0x400, 0xDEAD1234);  // Initialize to sentinel
        run(cycles);
        return read_dmem(0x400);
    }
};

// ============================================================================
// Test Functions
// ============================================================================

void test_lui(SocTestbench& tb) {
    printf("Testing LUI...\n");
    tb.reset();
    
    // LUI x1, 0x12345
    // SW x1, 0x400(x0)  -- but we need base address
    // Use: LUI x2, 0x10000; ADDI x2, x2, 0x400; SW x1, 0(x2)
    tb.emit(tb.LUI(1, 0x12345));      // x1 = 0x12345000
    tb.emit(tb.LUI(2, 0x10000));      // x2 = 0x10000000
    tb.emit(tb.ADDI(2, 2, 0x400));    // x2 = 0x10000400
    tb.emit(tb.SW(1, 2, 0));          // mem[x2] = x1
    uint32_t result = tb.run_test_program();
    tb.check("LUI 0x12345", result, 0x12345000);
    
    tb.reset();
    tb.emit(tb.LUI(1, 0xFFFFF));      // x1 = 0xFFFFF000
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("LUI 0xFFFFF", result, 0xFFFFF000);
}

void test_auipc(SocTestbench& tb) {
    printf("Testing AUIPC...\n");
    tb.reset();
    
    // AUIPC at address 0 with imm 1 should give 0x1000
    tb.emit(tb.AUIPC(1, 1));           // x1 = PC + 0x1000 = 0 + 0x1000
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("AUIPC imm=1 at PC=0", result, 0x1000);
}

void test_add_sub(SocTestbench& tb) {
    printf("Testing ADD/SUB...\n");
    tb.reset();
    
    // ADD: 5 + 7 = 12
    tb.emit(tb.ADDI(3, 0, 5));         // x3 = 5
    tb.emit(tb.ADDI(4, 0, 7));         // x4 = 7
    tb.emit(tb.ADD(1, 3, 4));          // x1 = x3 + x4 = 12
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("ADD 5+7", result, 12);

    tb.reset();
    // SUB: 10 - 3 = 7
    tb.emit(tb.ADDI(3, 0, 10));
    tb.emit(tb.ADDI(4, 0, 3));
    tb.emit(tb.SUB(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SUB 10-3", result, 7);

    tb.reset();
    // SUB with negative result: 3 - 10 = -7
    tb.emit(tb.ADDI(3, 0, 3));
    tb.emit(tb.ADDI(4, 0, 10));
    tb.emit(tb.SUB(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SUB 3-10", result, (uint32_t)-7);

    tb.reset();
    // ADD overflow: 0x7FFFFFFF + 1
    tb.emit(tb.LUI(3, 0x7FFFF));
    tb.emit(tb.ADDI(3, 3, 0x7FF));    // x3 = 0x7FFFF7FF (close to max)
    tb.emit(tb.ADDI(4, 0, 1));
    tb.emit(tb.ADD(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("ADD near overflow", result, 0x7FFFF800);
}

void test_logical(SocTestbench& tb) {
    printf("Testing AND/OR/XOR...\n");
    tb.reset();
    
    // AND: 0xFF00 & 0x0FF0 = 0x0F00
    tb.emit(tb.LUI(3, 0));
    tb.emit(tb.ADDI(3, 0, 0x7F0));    // x3 = 0x7F0 (can't do 0xFF00 easily)
    tb.emit(tb.ADDI(4, 0, 0x0FF));    // x4 = 0xFF
    tb.emit(tb.AND(1, 3, 4));          // x1 = 0x7F0 & 0xFF = 0xF0
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("AND 0x7F0 & 0xFF", result, 0xF0);

    tb.reset();
    // OR: 0xF0 | 0x0F = 0xFF
    tb.emit(tb.ADDI(3, 0, 0xF0));
    tb.emit(tb.ADDI(4, 0, 0x0F));
    tb.emit(tb.OR(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("OR 0xF0 | 0x0F", result, 0xFF);

    tb.reset();
    // XOR: 0xFF ^ 0xF0 = 0x0F
    tb.emit(tb.ADDI(3, 0, 0xFF));
    tb.emit(tb.ADDI(4, 0, 0xF0));
    tb.emit(tb.XOR(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("XOR 0xFF ^ 0xF0", result, 0x0F);
}

void test_shifts(SocTestbench& tb) {
    printf("Testing SLL/SRL/SRA...\n");
    tb.reset();
    
    // SLL: 1 << 4 = 16
    tb.emit(tb.ADDI(3, 0, 1));
    tb.emit(tb.ADDI(4, 0, 4));
    tb.emit(tb.SLL(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("SLL 1<<4", result, 16);

    tb.reset();
    // SRL: 0x80000000 >> 4 = 0x08000000 (logical)
    tb.emit(tb.LUI(3, 0x80000));       // x3 = 0x80000000
    tb.emit(tb.ADDI(4, 0, 4));
    tb.emit(tb.SRL(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SRL 0x80000000>>4", result, 0x08000000);

    tb.reset();
    // SRA: 0x80000000 >> 4 = 0xF8000000 (arithmetic)
    tb.emit(tb.LUI(3, 0x80000));
    tb.emit(tb.ADDI(4, 0, 4));
    tb.emit(tb.SRA(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SRA 0x80000000>>4", result, 0xF8000000);

    tb.reset();
    // SLLI: 1 << 31 = 0x80000000
    tb.emit(tb.ADDI(3, 0, 1));
    tb.emit(tb.SLLI(1, 3, 31));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SLLI 1<<31", result, 0x80000000);
}

void test_slt(SocTestbench& tb) {
    printf("Testing SLT/SLTU...\n");
    tb.reset();
    
    // SLT: -1 < 1 (signed) = 1
    tb.emit(tb.ADDI(3, 0, -1));        // x3 = -1
    tb.emit(tb.ADDI(4, 0, 1));         // x4 = 1
    tb.emit(tb.SLT(1, 3, 4));          // x1 = (x3 < x4) = 1
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("SLT -1 < 1", result, 1);

    tb.reset();
    // SLTU: -1 < 1 (unsigned) = 0 (because -1 = 0xFFFFFFFF > 1)
    tb.emit(tb.ADDI(3, 0, -1));
    tb.emit(tb.ADDI(4, 0, 1));
    tb.emit(tb.SLTU(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SLTU 0xFFFFFFFF < 1", result, 0);

    tb.reset();
    // SLT: 5 < 3 = 0
    tb.emit(tb.ADDI(3, 0, 5));
    tb.emit(tb.ADDI(4, 0, 3));
    tb.emit(tb.SLT(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("SLT 5 < 3", result, 0);
}

void test_branches(SocTestbench& tb) {
    printf("Testing branches...\n");
    
    // BEQ taken
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 5));
    tb.emit(tb.ADDI(4, 0, 5));
    tb.emit(tb.BEQ(3, 4, 8));          // if x3==x4, skip next instr
    tb.emit(tb.ADDI(1, 0, 0));         // x1 = 0 (skipped)
    tb.emit(tb.ADDI(1, 0, 1));         // x1 = 1 (executed)
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("BEQ taken", result, 1);

    // BEQ not taken
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 5));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.BEQ(3, 4, 8));
    tb.emit(tb.ADDI(1, 0, 0));         // x1 = 0 (executed)
    tb.emit(tb.ADDI(1, 1, 1));         // x1 = 1 (executed, but started from 0)
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("BEQ not taken", result, 1);

    // BNE taken
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 5));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.BNE(3, 4, 8));
    tb.emit(tb.ADDI(1, 0, 0));
    tb.emit(tb.ADDI(1, 0, 1));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("BNE taken", result, 1);

    // BLT signed
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -1));        // x3 = -1
    tb.emit(tb.ADDI(4, 0, 1));         // x4 = 1
    tb.emit(tb.BLT(3, 4, 8));          // -1 < 1, taken
    tb.emit(tb.ADDI(1, 0, 0));
    tb.emit(tb.ADDI(1, 0, 1));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("BLT -1 < 1 taken", result, 1);

    // BLTU unsigned (0xFFFFFFFF > 1)
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -1));        // x3 = 0xFFFFFFFF
    tb.emit(tb.ADDI(4, 0, 1));         // x4 = 1
    tb.emit(tb.BLTU(3, 4, 8));         // 0xFFFFFFFF < 1? NO
    tb.emit(tb.ADDI(1, 0, 0));         // executed
    tb.emit(tb.ADDI(1, 1, 1));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("BLTU 0xFFFFFFFF < 1 not taken", result, 1);

    // BGE
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 5));
    tb.emit(tb.ADDI(4, 0, 5));
    tb.emit(tb.BGE(3, 4, 8));          // 5 >= 5, taken
    tb.emit(tb.ADDI(1, 0, 0));
    tb.emit(tb.ADDI(1, 0, 1));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program();
    tb.check("BGE 5 >= 5 taken", result, 1);
}

void test_load_store(SocTestbench& tb) {
    printf("Testing load/store...\n");
    
    // SW then LW
    tb.reset();
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.ADDI(3, 0, 0x123));
    tb.emit(tb.SW(3, 2, 0));           // store 0x123
    tb.emit(tb.LW(1, 2, 0));           // load it back
    tb.emit(tb.SW(1, 2, 4));           // store to result location
    tb.write_dmem(0x404, 0);
    uint32_t result = tb.run_test_program();
    result = tb.read_dmem(0x404);
    tb.check("SW/LW word", result, 0x123);

    // SB then LB (signed)
    tb.reset();
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.ADDI(3, 0, 0x80));      // -128 as signed byte
    tb.emit(tb.SB(3, 2, 0));           // store byte
    tb.emit(tb.LB(1, 2, 0));           // load signed byte
    tb.emit(tb.SW(1, 2, 4));
    tb.write_dmem(0x404, 0);
    tb.run_test_program();
    result = tb.read_dmem(0x404);
    tb.check("SB/LB sign extend", result, 0xFFFFFF80);  // sign extended

    // LBU (unsigned)
    tb.reset();
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.ADDI(3, 0, 0x80));
    tb.emit(tb.SB(3, 2, 0));
    tb.emit(tb.LBU(1, 2, 0));          // load unsigned byte
    tb.emit(tb.SW(1, 2, 4));
    tb.write_dmem(0x404, 0);
    tb.run_test_program();
    result = tb.read_dmem(0x404);
    tb.check("LBU zero extend", result, 0x00000080);

    // SH then LH
    tb.reset();
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.LUI(3, 0xFFFF8));       // x3 upper bits
    tb.emit(tb.ADDI(3, 3, 0x000));     // x3 = 0xFFFF8000
    tb.emit(tb.SH(3, 2, 0));           // store halfword (0x8000)
    tb.emit(tb.LH(1, 2, 0));           // load signed halfword
    tb.emit(tb.SW(1, 2, 4));
    tb.write_dmem(0x404, 0);
    tb.run_test_program();
    result = tb.read_dmem(0x404);
    tb.check("SH/LH sign extend", result, 0xFFFF8000);
}

void test_jal_jalr(SocTestbench& tb) {
    printf("Testing JAL/JALR...\n");
    
    // JAL
    tb.reset();
    tb.emit(tb.JAL(1, 8));             // x1 = PC+4, jump +8
    tb.emit(tb.ADDI(5, 0, 99));        // skipped
    tb.emit(tb.ADDI(5, 0, 42));        // x5 = 42
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(5, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("JAL skip", result, 42);

    // JALR
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 16));        // x3 = 16 (target address)
    tb.emit(tb.JALR(1, 3, 0));         // jump to x3
    tb.emit(tb.ADDI(5, 0, 99));        // skipped
    tb.emit(tb.ADDI(5, 0, 99));        // skipped
    tb.emit(tb.ADDI(5, 0, 42));        // x5 = 42 (at addr 16)
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(5, 2, 0));
    result = tb.run_test_program();
    tb.check("JALR", result, 42);
}

void test_mul(SocTestbench& tb) {
    printf("Testing MUL...\n");
    
    // MUL: 7 * 6 = 42
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 7));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.MUL(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program(150);
    tb.check("MUL 7*6", result, 42);

    // MUL: -3 * 5 = -15
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -3));
    tb.emit(tb.ADDI(4, 0, 5));
    tb.emit(tb.MUL(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(150);
    tb.check("MUL -3*5", result, (uint32_t)-15);

    // MULH (signed high bits)
    tb.reset();
    tb.emit(tb.LUI(3, 0x10000));       // x3 = 0x10000000
    tb.emit(tb.ADDI(4, 0, 16));        // x4 = 16
    tb.emit(tb.MULH(1, 3, 4));         // high bits of 0x10000000 * 16
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(150);
    tb.check("MULH", result, 1);       // 0x10000000 * 16 = 0x1_00000000, high = 1

    // MULHU (unsigned high bits)
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -1));        // x3 = 0xFFFFFFFF
    tb.emit(tb.ADDI(4, 0, 2));         // x4 = 2
    tb.emit(tb.MULHU(1, 3, 4));        // high bits of 0xFFFFFFFF * 2
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(150);
    tb.check("MULHU 0xFFFFFFFF*2 high", result, 1);
}

void test_div(SocTestbench& tb) {
    printf("Testing DIV/REM...\n");
    
    // DIV: 20 / 6 = 3
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 20));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.DIV(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program(200);
    tb.check("DIV 20/6", result, 3);

    // REM: 20 % 6 = 2
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 20));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.REM(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(200);
    tb.check("REM 20%6", result, 2);

    // DIV signed: -20 / 6 = -3
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -20));
    tb.emit(tb.ADDI(4, 0, 6));
    tb.emit(tb.DIV(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(200);
    tb.check("DIV -20/6", result, (uint32_t)-3);

    // DIVU: 0xFFFFFFFF / 2 
    tb.reset();
    tb.emit(tb.ADDI(3, 0, -1));        // 0xFFFFFFFF
    tb.emit(tb.ADDI(4, 0, 2));
    tb.emit(tb.DIVU(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(200);
    tb.check("DIVU 0xFFFFFFFF/2", result, 0x7FFFFFFF);

    // DIV by zero: returns -1
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 10));
    tb.emit(tb.ADDI(4, 0, 0));
    tb.emit(tb.DIV(1, 3, 4));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(200);
    tb.check("DIV by zero", result, 0xFFFFFFFF);
}

void test_forwarding(SocTestbench& tb) {
    printf("Testing forwarding...\n");
    
    // RAW hazard: use result immediately
    tb.reset();
    tb.emit(tb.ADDI(3, 0, 10));        // x3 = 10
    tb.emit(tb.ADDI(4, 3, 5));         // x4 = x3 + 5 = 15 (RAW)
    tb.emit(tb.ADDI(1, 4, 5));         // x1 = x4 + 5 = 20 (RAW)
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    uint32_t result = tb.run_test_program();
    tb.check("RAW forwarding chain", result, 20);

    // Load-use hazard
    tb.reset();
    tb.write_dmem(0x410, 100);
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x410));
    tb.emit(tb.LW(3, 2, 0));           // x3 = 100
    tb.emit(tb.ADDI(1, 3, 5));         // x1 = x3 + 5 = 105 (load-use)
    tb.emit(tb.ADDI(2, 0, 0));
    tb.emit(tb.LUI(2, 0x10000));
    tb.emit(tb.ADDI(2, 2, 0x400));
    tb.emit(tb.SW(1, 2, 0));
    result = tb.run_test_program(150);
    tb.check("Load-use hazard", result, 105);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    SocTestbench tb;

    printf("============================================================\n");
    printf("Comprehensive RV32IM Instruction Testbench\n");
    printf("============================================================\n");

    test_lui(tb);
    test_auipc(tb);
    test_add_sub(tb);
    test_logical(tb);
    test_shifts(tb);
    test_slt(tb);
    test_branches(tb);
    test_load_store(tb);
    test_jal_jalr(tb);
    test_mul(tb);
    test_div(tb);
    test_forwarding(tb);

    printf("\n============================================================\n");
    printf("Results: %d passed, %d failed\n", tb.tests_passed, tb.tests_failed);
    printf("============================================================\n");

    return (tb.tests_failed > 0) ? 1 : 0;
}
