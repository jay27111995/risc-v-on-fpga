// ============================================================================
// Comprehensive LR.W/SC.W Testbench
// ============================================================================
//
// Extensive tests for the A extension atomic instructions (LR.W and SC.W)
//
// Tests:
//   1. Basic LR.W load functionality
//   2. Basic SC.W after LR.W (should succeed)
//   3. SC.W without LR.W (should fail)
//   4. SC.W to different address than LR.W (should fail)
//   5. SC.W after intervening store to same address (should fail)
//   6. SC.W after intervening store to different address (should succeed)
//   7. Multiple LR.W in sequence (only last one counts)
//   8. Back-to-back LR.W/SC.W (pipeline stress test)
//   9. LR.W with data hazard (dependent instruction)
//  10. SC.W result used immediately (forwarding test)
//
// ============================================================================

#include "Vriscv_soc.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

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
        soc->rst_n = 0;
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

    // Instruction encoding helpers
    // LUI rd, imm20
    uint32_t LUI(int rd, uint32_t imm20) {
        return (imm20 << 12) | (rd << 7) | 0x37;
    }

    // ADDI rd, rs1, imm12
    uint32_t ADDI(int rd, int rs1, int32_t imm12) {
        return ((imm12 & 0xFFF) << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0x13;
    }

    // LW rd, offset(rs1)
    uint32_t LW(int rd, int rs1, int32_t offset) {
        return ((offset & 0xFFF) << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0x03;
    }

    // SW rs2, offset(rs1)
    uint32_t SW(int rs2, int rs1, int32_t offset) {
        uint32_t imm11_5 = (offset >> 5) & 0x7F;
        uint32_t imm4_0 = offset & 0x1F;
        return (imm11_5 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (imm4_0 << 7) | 0x23;
    }

    // LR.W rd, (rs1)
    uint32_t LR_W(int rd, int rs1) {
        // funct5=00010, aq=0, rl=0, rs2=00000
        return (0b00010 << 27) | (0b00 << 25) | (0 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0x2F;
    }

    // SC.W rd, rs2, (rs1)
    uint32_t SC_W(int rd, int rs2, int rs1) {
        // funct5=00011, aq=0, rl=0
        return (0b00011 << 27) | (0b00 << 25) | (rs2 << 20) | (rs1 << 15) | (0b010 << 12) | (rd << 7) | 0x2F;
    }

    // ADD rd, rs1, rs2
    uint32_t ADD(int rd, int rs1, int rs2) {
        return (0b0000000 << 25) | (rs2 << 20) | (rs1 << 15) | (0b000 << 12) | (rd << 7) | 0x33;
    }

    // BNE rs1, rs2, offset
    uint32_t BNE(int rs1, int rs2, int32_t offset) {
        uint32_t imm12 = (offset >> 12) & 1;
        uint32_t imm10_5 = (offset >> 5) & 0x3F;
        uint32_t imm4_1 = (offset >> 1) & 0xF;
        uint32_t imm11 = (offset >> 11) & 1;
        return (imm12 << 31) | (imm10_5 << 25) | (rs2 << 20) | (rs1 << 15) | 
               (0b001 << 12) | (imm4_1 << 8) | (imm11 << 7) | 0x63;
    }

    // EBREAK
    uint32_t EBREAK() {
        return 0x00100073;
    }

    // NOP (ADDI x0, x0, 0)
    uint32_t NOP() {
        return 0x00000013;
    }
};

int tests_passed = 0;
int tests_failed = 0;

void check(const char* name, bool condition) {
    if (condition) {
        printf("  [PASS] %s\n", name);
        tests_passed++;
    } else {
        printf("  [FAIL] %s\n", name);
        tests_failed++;
    }
}

// ============================================================================
// Test 1: Basic LR.W load functionality
// ============================================================================
void test1_basic_lr(SocTestbench& tb) {
    printf("\nTest 1: Basic LR.W load functionality\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000       # x1 = 0x10000000
    //   ADDI x1, x1, 0x400    # x1 = 0x10000400
    //   LR.W x2, (x1)         # x2 = mem[0x10000400]
    //   SW x2, 4(x1)          # store loaded value to verify
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.SW(2, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 0xDEADBEEF);
    tb.write_dmem(0x404, 0);
    
    tb.run(100);
    
    uint32_t result = tb.read_dmem(0x404);
    check("LR.W loads correct value", result == 0xDEADBEEF);
}

// ============================================================================
// Test 2: Basic SC.W after LR.W (should succeed)
// ============================================================================
void test2_basic_sc_success(SocTestbench& tb) {
    printf("\nTest 2: Basic SC.W after LR.W (should succeed)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   LR.W x2, (x1)         # Load and reserve
    //   ADDI x3, x2, 1        # Increment
    //   SC.W x4, x3, (x1)     # Store conditional
    //   SW x4, 4(x1)          # Store SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(3, 2, 1));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 100);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    
    tb.run(100);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc_result = tb.read_dmem(0x404);
    
    check("SC.W stored new value", mem_val == 101);
    check("SC.W returned 0 (success)", sc_result == 0);
}

// ============================================================================
// Test 3: SC.W without LR.W (should fail)
// ============================================================================
void test3_sc_without_lr(SocTestbench& tb) {
    printf("\nTest 3: SC.W without LR.W (should fail)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   ADDI x3, x0, 999      # Value to store
    //   SC.W x4, x3, (x1)     # SC without LR - should fail
    //   SW x4, 4(x1)          # Store SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.ADDI(3, 0, 999));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 42);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    
    tb.run(100);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc_result = tb.read_dmem(0x404);
    
    check("Memory unchanged (no store)", mem_val == 42);
    check("SC.W returned 1 (failure)", sc_result == 1);
}

// ============================================================================
// Test 4: SC.W to different address than LR.W (should fail)
// ============================================================================
void test4_sc_different_addr(SocTestbench& tb) {
    printf("\nTest 4: SC.W to different address than LR.W (should fail)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400    # addr1 = 0x10000400
    //   ADDI x5, x1, 16       # addr2 = 0x10000410
    //   LR.W x2, (x1)         # Reserve addr1
    //   ADDI x3, x0, 999
    //   SC.W x4, x3, (x5)     # SC to addr2 - should fail
    //   SW x4, 4(x1)          # Store SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.ADDI(5, 1, 16));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(3, 0, 999));
    tb.emit(tb.SC_W(4, 3, 5));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 42);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    tb.write_dmem(0x410, 77);
    
    tb.run(100);
    
    uint32_t mem_val_addr2 = tb.read_dmem(0x410);
    uint32_t sc_result = tb.read_dmem(0x404);
    
    check("addr2 unchanged (no store)", mem_val_addr2 == 77);
    check("SC.W returned 1 (failure)", sc_result == 1);
}

// ============================================================================
// Test 5: SC.W after intervening store to SAME address (should fail)
// ============================================================================
void test5_sc_after_store_same_addr(SocTestbench& tb) {
    printf("\nTest 5: SC.W after intervening store to same address (should fail)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   LR.W x2, (x1)         # Reserve
    //   ADDI x6, x0, 200
    //   SW x6, 0(x1)          # Store to same address - invalidates reservation
    //   ADDI x3, x0, 300
    //   SC.W x4, x3, (x1)     # SC - should fail
    //   SW x4, 4(x1)          # Store SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(6, 0, 200));
    tb.emit(tb.SW(6, 1, 0));
    tb.emit(tb.ADDI(3, 0, 300));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 100);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    
    tb.run(150);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc_result = tb.read_dmem(0x404);
    
    check("Memory has intervening store value (200)", mem_val == 200);
    check("SC.W returned 1 (failure)", sc_result == 1);
}

// ============================================================================
// Test 6: SC.W after intervening store to DIFFERENT address (should succeed)
// ============================================================================
void test6_sc_after_store_diff_addr(SocTestbench& tb) {
    printf("\nTest 6: SC.W after intervening store to different address (should succeed)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   ADDI x5, x1, 16       # Different address
    //   LR.W x2, (x1)         # Reserve addr1
    //   ADDI x6, x0, 200
    //   SW x6, 0(x5)          # Store to different address - reservation intact
    //   ADDI x3, x2, 1
    //   SC.W x4, x3, (x1)     # SC - should succeed
    //   SW x4, 4(x1)          # Store SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.ADDI(5, 1, 16));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(6, 0, 200));
    tb.emit(tb.SW(6, 5, 0));
    tb.emit(tb.ADDI(3, 2, 1));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 100);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    tb.write_dmem(0x410, 0);
    
    tb.run(150);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc_result = tb.read_dmem(0x404);
    uint32_t other_val = tb.read_dmem(0x410);
    
    check("SC.W stored new value (101)", mem_val == 101);
    check("SC.W returned 0 (success)", sc_result == 0);
    check("Other address has intervening store (200)", other_val == 200);
}

// ============================================================================
// Test 7: Multiple LR.W in sequence (only last one counts)
// ============================================================================
void test7_multiple_lr(SocTestbench& tb) {
    printf("\nTest 7: Multiple LR.W in sequence (only last reservation counts)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400    # addr1
    //   ADDI x5, x1, 16       # addr2
    //   LR.W x2, (x1)         # Reserve addr1
    //   LR.W x2, (x5)         # Reserve addr2 (overwrites addr1 reservation)
    //   ADDI x3, x0, 999
    //   SC.W x4, x3, (x5)     # SC to addr2 - should succeed (matches last LR)
    //   SW x4, 4(x1)          # Store SC result
    //   SC.W x6, x3, (x1)     # SC to addr1 - should fail (reservation cleared by prev SC)
    //   SW x6, 8(x1)          # Store second SC result
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.ADDI(5, 1, 16));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.LR_W(2, 5));
    tb.emit(tb.ADDI(3, 0, 999));
    tb.emit(tb.SC_W(4, 3, 5));      // SC to addr2 first (should succeed)
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.SC_W(6, 3, 1));      // SC to addr1 second (should fail - no reservation)
    tb.emit(tb.SW(6, 1, 8));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 42);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    tb.write_dmem(0x408, 0xFFFFFFFF);
    tb.write_dmem(0x410, 77);
    
    tb.run(200);
    
    uint32_t mem_val1 = tb.read_dmem(0x400);
    uint32_t sc1_result = tb.read_dmem(0x404);
    uint32_t sc2_result = tb.read_dmem(0x408);
    uint32_t mem_val2 = tb.read_dmem(0x410);
    
    check("addr2 updated by first SC (999)", mem_val2 == 999);
    check("First SC.W returned 0 (success)", sc1_result == 0);
    check("addr1 unchanged (second SC failed)", mem_val1 == 42);
    check("Second SC.W returned 1 (failure)", sc2_result == 1);
}

// ============================================================================
// Test 8: Back-to-back LR.W/SC.W (pipeline stress test)
// ============================================================================
void test8_back_to_back(SocTestbench& tb) {
    printf("\nTest 8: Back-to-back LR.W/SC.W (pipeline stress)\n");
    tb.reset();
    
    // Program: LR immediately followed by SC (no instructions between)
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   ADDI x3, x0, 123      # Prepare value first
    //   LR.W x2, (x1)         # Reserve
    //   SC.W x4, x3, (x1)     # Immediately SC (tests pipeline)
    //   SW x4, 4(x1)
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.ADDI(3, 0, 123));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 50);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    
    tb.run(100);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc_result = tb.read_dmem(0x404);
    
    check("SC.W stored value (123)", mem_val == 123);
    check("SC.W returned 0 (success)", sc_result == 0);
}

// ============================================================================
// Test 9: LR.W with data hazard (use loaded value immediately)
// ============================================================================
void test9_lr_data_hazard(SocTestbench& tb) {
    printf("\nTest 9: LR.W with data hazard (immediate use of loaded value)\n");
    tb.reset();
    
    // Program: Use LR.W result in next instruction (tests forwarding/stall)
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   LR.W x2, (x1)         # x2 = mem value
    //   ADD x3, x2, x2        # x3 = 2 * x2 (immediate use - hazard!)
    //   SW x3, 4(x1)
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADD(3, 2, 2));
    tb.emit(tb.SW(3, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 25);
    tb.write_dmem(0x404, 0);
    
    tb.run(100);
    
    uint32_t result = tb.read_dmem(0x404);
    check("Correct result with LR.W hazard (2*25=50)", result == 50);
}

// ============================================================================
// Test 10: SC.W result used immediately (forwarding test)
// ============================================================================
void test10_sc_result_hazard(SocTestbench& tb) {
    printf("\nTest 10: SC.W result used immediately (forwarding test)\n");
    tb.reset();
    
    // Program: Use SC.W result in next instruction
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   LR.W x2, (x1)
    //   ADDI x3, x2, 1
    //   SC.W x4, x3, (x1)     # x4 = 0 (success)
    //   ADDI x5, x4, 100      # x5 = x4 + 100 = 100 (immediate use of SC result)
    //   SW x5, 4(x1)
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(3, 2, 1));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.ADDI(5, 4, 100));
    tb.emit(tb.SW(5, 1, 4));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 50);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    
    tb.run(150);
    
    uint32_t result = tb.read_dmem(0x404);
    check("SC result forwarded correctly (0+100=100)", result == 100);
}

// ============================================================================
// Test 11: Repeated LR/SC loop (spinlock pattern)
// ============================================================================
void test11_spinlock_pattern(SocTestbench& tb) {
    printf("\nTest 11: Spinlock acquire pattern (CAS loop)\n");
    tb.reset();
    
    // Program: Try to atomically change 0 -> 1
    // loop:
    //   LR.W x2, (x1)
    //   BNE x2, x0, fail      # If not 0, fail
    //   ADDI x3, x0, 1
    //   SC.W x4, x3, (x1)
    //   BNE x4, x0, loop      # If SC failed, retry
    //   # Success: store result
    //   ADDI x5, x0, 42
    //   SW x5, 4(x1)
    //   EBREAK
    // fail:
    //   ADDI x5, x0, 99
    //   SW x5, 4(x1)
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));      // 0x00
    tb.emit(tb.ADDI(1, 1, 0x400));    // 0x04
    // loop:
    tb.emit(tb.LR_W(2, 1));           // 0x08
    tb.emit(tb.BNE(2, 0, 28));        // 0x0C: if x2 != 0, jump to fail (0x28)
    tb.emit(tb.ADDI(3, 0, 1));        // 0x10
    tb.emit(tb.SC_W(4, 3, 1));        // 0x14
    tb.emit(tb.BNE(4, 0, -12));       // 0x18: if x4 != 0, jump to loop (0x08)
    // success:
    tb.emit(tb.ADDI(5, 0, 42));       // 0x1C
    tb.emit(tb.SW(5, 1, 4));          // 0x20
    tb.emit(tb.EBREAK());             // 0x24
    // fail:
    tb.emit(tb.ADDI(5, 0, 99));       // 0x28
    tb.emit(tb.SW(5, 1, 4));          // 0x2C
    tb.emit(tb.EBREAK());             // 0x30
    
    tb.write_dmem(0x400, 0);  // Lock is free
    tb.write_dmem(0x404, 0);
    
    tb.run(200);
    
    uint32_t lock_val = tb.read_dmem(0x400);
    uint32_t result = tb.read_dmem(0x404);
    
    check("Lock acquired (value = 1)", lock_val == 1);
    check("Success path taken (result = 42)", result == 42);
}

// ============================================================================
// Test 12: Second SC.W after first SC.W (reservation cleared)
// ============================================================================
void test12_double_sc(SocTestbench& tb) {
    printf("\nTest 12: Two SC.W after one LR.W (second should fail)\n");
    tb.reset();
    
    // Program:
    //   LUI x1, 0x10000
    //   ADDI x1, x1, 0x400
    //   LR.W x2, (x1)
    //   ADDI x3, x2, 1
    //   SC.W x4, x3, (x1)     # First SC - should succeed
    //   SW x4, 4(x1)
    //   ADDI x3, x3, 1
    //   SC.W x5, x3, (x1)     # Second SC - should fail (reservation cleared)
    //   SW x5, 8(x1)
    //   EBREAK
    
    tb.emit(tb.LUI(1, 0x10000));
    tb.emit(tb.ADDI(1, 1, 0x400));
    tb.emit(tb.LR_W(2, 1));
    tb.emit(tb.ADDI(3, 2, 1));
    tb.emit(tb.SC_W(4, 3, 1));
    tb.emit(tb.SW(4, 1, 4));
    tb.emit(tb.ADDI(3, 3, 1));
    tb.emit(tb.SC_W(5, 3, 1));
    tb.emit(tb.SW(5, 1, 8));
    tb.emit(tb.EBREAK());
    
    tb.write_dmem(0x400, 100);
    tb.write_dmem(0x404, 0xFFFFFFFF);
    tb.write_dmem(0x408, 0xFFFFFFFF);
    
    tb.run(200);
    
    uint32_t mem_val = tb.read_dmem(0x400);
    uint32_t sc1_result = tb.read_dmem(0x404);
    uint32_t sc2_result = tb.read_dmem(0x408);
    
    check("Memory has first SC value (101)", mem_val == 101);
    check("First SC.W returned 0 (success)", sc1_result == 0);
    check("Second SC.W returned 1 (failure)", sc2_result == 1);
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    SocTestbench tb;

    printf("==============================================\n");
    printf("Comprehensive LR.W/SC.W Testbench\n");
    printf("==============================================\n");

    test1_basic_lr(tb);
    test2_basic_sc_success(tb);
    test3_sc_without_lr(tb);
    test4_sc_different_addr(tb);
    test5_sc_after_store_same_addr(tb);
    test6_sc_after_store_diff_addr(tb);
    test7_multiple_lr(tb);
    test8_back_to_back(tb);
    test9_lr_data_hazard(tb);
    test10_sc_result_hazard(tb);
    test11_spinlock_pattern(tb);
    test12_double_sc(tb);

    printf("\n==============================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("==============================================\n");

    return (tests_failed > 0) ? 1 : 0;
}
