// RISC-V SoC Library - Implementation
// ============================================================================

#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

const char *cpulog_type_names[] = {"IFETCH", "DLOAD ", "DSTORE", "???   "};

// ----------------------------------------------------------------------------
// Memory Access
// ----------------------------------------------------------------------------

void write_imem(uint32_t word_idx, uint32_t value) {
    write32(BAR_IMEM + word_idx * 4, value);
}

uint32_t read_imem(uint32_t word_idx) {
    return read32(BAR_IMEM + word_idx * 4);
}

void write_dmem(uint32_t word_idx, uint32_t value) {
    write32(BAR_DMEM + word_idx * 4, value);
}

uint32_t read_dmem(uint32_t word_idx) {
    return read32(BAR_DMEM + word_idx * 4);
}

void init_imem(void) {
    uint64_t ebreak_pair = ((uint64_t)EBREAK_INSTR << 32) | EBREAK_INSTR;
    for (int i = 0; i < IMEM_SIZE_WORDS / 2; i++) {
        write64(BAR_IMEM + i * 8, ebreak_pair);
    }
}

void init_dmem(void) {
    for (int i = 0; i < DMEM_SIZE_WORDS / 2; i++) {
        write64(BAR_DMEM + i * 8, 0);
    }
}

void init_memory(void) {
    init_imem();
    init_dmem();
    sniffer_clear();
    cpulog_clear();
}

// ----------------------------------------------------------------------------
// CPU Control
// ----------------------------------------------------------------------------

void cpu_reset(void) {
    write32(BAR_CTRL, CTRL_RESET);
    usleep(1000);
    write32(BAR_CTRL, 0);
    usleep(1000);
}

void cpu_run(void) {
    write32(BAR_CTRL, CTRL_RUN);
}

void cpu_stop(void) {
    write32(BAR_CTRL, 0);
}

int cpu_is_halted(void) {
    return (read32(BAR_STATUS) & 0x2) != 0;
}

int cpu_wait_halt(int timeout_ms) {
    for (int i = 0; i < timeout_ms * 10; i++) {
        if (cpu_is_halted())
            return 1;
        usleep(100);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Bus Sniffer
// ----------------------------------------------------------------------------

uint32_t sniffer_get_count(void) {
    return read32(BAR_SNIFFER + SNIFF_COUNT);
}

void sniffer_clear(void) {
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_SNIFFER + SNIFF_CTRL, 0x01);  // enable only
}

void sniffer_read_entry(int idx, sniffer_entry_t *entry) {
    uint32_t base = BAR_SNIFFER + SNIFF_ENTRY0 + idx * 0x10;
    uint32_t w0 = read32(base + 0x0);
    uint32_t w1 = read32(base + 0x4);
    uint32_t w2 = read32(base + 0x8);
    uint32_t w3 = read32(base + 0xC);

    entry->is_write = w0 & 1;
    entry->address = ((w0 >> 1) & 0x7FFFF) << 1;  // Restore full address
    entry->timestamp = ((uint64_t)w2 << 32) | w1;
    entry->data = w3;
}

static void sniffer_print_entry(int idx, const sniffer_entry_t *entry,
                                uint64_t base_cycle) {
    uint64_t offset = entry->timestamp - base_cycle;
    uint64_t offset_ns = CYCLES_TO_NS(offset);
    printf("    [%2d] +%6lu cycles (+%6lu ns) %s addr=0x%05X data=0x%08X\n",
           idx, offset, offset_ns, entry->is_write ? "WR" : "RD",
           entry->address, entry->data);
}

void sniffer_dump(int max_entries) {
    uint32_t count = sniffer_get_count();
    if (count == 0) {
        printf("  (no entries)\n");
        return;
    }
    int n = (count < (uint32_t)max_entries) ? count : max_entries;

    sniffer_entry_t entries[64];
    for (int i = 0; i < n; i++) {
        sniffer_read_entry(i, &entries[i]);
    }

    // Find minimum timestamp as base
    uint64_t base_cycle = entries[0].timestamp;
    for (int i = 1; i < n; i++) {
        if (entries[i].timestamp < base_cycle)
            base_cycle = entries[i].timestamp;
    }

    for (int i = 0; i < n; i++) {
        sniffer_print_entry(i, &entries[i], base_cycle);
    }
}

// ----------------------------------------------------------------------------
// CPU Logger
// ----------------------------------------------------------------------------

uint32_t cpulog_get_count(void) {
    return read32(BAR_CPULOG + CPULOG_COUNT);
}

void cpulog_clear(void) {
    write32(BAR_CPULOG + CPULOG_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_CPULOG + CPULOG_CTRL, 0x01);  // enable, log_imem=0
}

void cpulog_clear_with_imem(void) {
    write32(BAR_CPULOG + CPULOG_CTRL, 0x03);  // clear + enable
    usleep(1000);
    write32(BAR_CPULOG + CPULOG_CTRL, 0x05);  // enable + log_imem
}

void cpulog_read_entry(int idx, cpulog_entry_t *entry) {
    uint32_t base = BAR_CPULOG + CPULOG_ENTRY0 + idx * 0x10;
    uint32_t w0 = read32(base + 0x0);
    uint32_t w1 = read32(base + 0x4);
    uint32_t w2 = read32(base + 0x8);
    uint32_t w3 = read32(base + 0xC);

    entry->type = w0 & 0x3;
    entry->address = ((w0 >> 2) & 0x3FFFF) << 2;
    entry->timestamp = ((uint64_t)w2 << 32) | w1;
    entry->data = w3;
}

static void cpulog_print_entry(int idx, const cpulog_entry_t *entry,
                               uint64_t base_cycle) {
    uint64_t offset = entry->timestamp - base_cycle;
    uint64_t offset_ns = CYCLES_TO_NS(offset);
    if (entry->type == CPULOG_TYPE_IFETCH) {
        // For instruction fetches, decode the instruction
        printf("    [%3d] +%6lu cycles (+%6lu ns) %s 0x%05X  %s\n",
               idx, offset, offset_ns, cpulog_type_names[entry->type & 3],
               entry->address, riscv_decode(entry->data));
    } else {
        printf("    [%3d] +%6lu cycles (+%6lu ns) %s 0x%05X  data=0x%08X\n",
               idx, offset, offset_ns, cpulog_type_names[entry->type & 3],
               entry->address, entry->data);
    }
}

void cpulog_dump(int max_entries) {
    uint32_t count = cpulog_get_count();
    if (count == 0) {
        printf("  (no entries)\n");
        return;
    }
    int n = (count < (uint32_t)max_entries) ? count : max_entries;

    cpulog_entry_t entries[256];
    for (int i = 0; i < n; i++) {
        cpulog_read_entry(i, &entries[i]);
    }

    // Find minimum timestamp as base
    uint64_t base_cycle = entries[0].timestamp;
    for (int i = 1; i < n; i++) {
        if (entries[i].timestamp < base_cycle)
            base_cycle = entries[i].timestamp;
    }

    for (int i = 0; i < n; i++) {
        cpulog_print_entry(i, &entries[i], base_cycle);
    }
}

// ----------------------------------------------------------------------------
// Performance Counters
// ----------------------------------------------------------------------------

void print_perf_counters(void) {
    printf("=== Performance Counters ===\n");

    uint32_t cycles   = read32(BAR_CYCLES);
    uint32_t instrs   = read32(BAR_INSTRS);
    uint32_t stalls   = read32(BAR_STALLS);
    uint32_t branches = read32(BAR_BRANCHES);
    uint32_t br_taken = read32(BAR_BR_TAKEN);
    uint32_t loads    = read32(BAR_LOADS);
    uint32_t stores   = read32(BAR_STORES);

    printf("  Cycles:         %u\n", cycles);
    printf("  Instructions:   %u\n", instrs);
    printf("  Stalls:         %u\n", stalls);
    printf("  Branches:       %u (taken: %u)\n", branches, br_taken);
    printf("  Loads:          %u\n", loads);
    printf("  Stores:         %u\n", stores);

    if (instrs > 0) {
        printf("  CPI: %.2f\n", (float)cycles / instrs);
        printf("  IPC: %.2f\n", (float)instrs / cycles);
    }
    printf("\n");
}

// ----------------------------------------------------------------------------
// Program Loading
// ----------------------------------------------------------------------------

int load_program_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("  ERROR: Cannot open %s\n", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("  Loading %s (%ld bytes, %ld instructions)\n", filename, size, size / 4);

    uint32_t *prog = malloc(size);
    if (!prog) {
        fclose(f);
        return -1;
    }

    if (fread(prog, 1, size, f) != (size_t)size) {
        printf("  ERROR: Failed to read %s\n", filename);
        free(prog);
        fclose(f);
        return -1;
    }
    fclose(f);

    for (long i = 0; i < size / 4; i++) {
        write_imem(i, prog[i]);
    }
    free(prog);

    return size / 4;
}

// ----------------------------------------------------------------------------
// Init/Cleanup
// ----------------------------------------------------------------------------

int common_init(int argc, char *argv[], const char *prog_name) {
    const char *pci_addr = "0000:b1:00.0";
    int iommu_group = 52;

    if (argc >= 2)
        pci_addr = argv[1];
    if (argc >= 3)
        iommu_group = atoi(argv[2]);

    printf("%s\n", prog_name);
    printf("================================================================================\n");
    printf("PCI: %s, IOMMU group: %d\n\n", pci_addr, iommu_group);

    if (vfio_init(pci_addr, iommu_group) < 0) {
        fprintf(stderr, "Failed to initialize VFIO\n");
        return -1;
    }

    return 0;
}

void common_cleanup(void) {
    vfio_cleanup();
}

// ----------------------------------------------------------------------------
// Instruction Decoder
// ----------------------------------------------------------------------------

// Register names
static const char *reg_names[] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

// Extract instruction fields
#define OPCODE(i)  ((i) & 0x7F)
#define RD(i)      (((i) >> 7) & 0x1F)
#define FUNCT3(i)  (((i) >> 12) & 0x7)
#define RS1(i)     (((i) >> 15) & 0x1F)
#define RS2(i)     (((i) >> 20) & 0x1F)
#define FUNCT7(i)  (((i) >> 25) & 0x7F)

// Sign extend
static int32_t sign_extend(uint32_t val, int bits) {
    uint32_t sign_bit = 1U << (bits - 1);
    return (val ^ sign_bit) - sign_bit;
}

// Immediate extraction
static int32_t imm_i(uint32_t i) {
    return sign_extend(i >> 20, 12);
}

static int32_t imm_s(uint32_t i) {
    uint32_t imm = ((i >> 7) & 0x1F) | (((i >> 25) & 0x7F) << 5);
    return sign_extend(imm, 12);
}

static int32_t imm_b(uint32_t i) {
    uint32_t imm = (((i >> 8) & 0xF) << 1) | (((i >> 25) & 0x3F) << 5) |
                   (((i >> 7) & 0x1) << 11) | (((i >> 31) & 0x1) << 12);
    return sign_extend(imm, 13);
}

static int32_t imm_u(uint32_t i) {
    return i & 0xFFFFF000;
}

static int32_t imm_j(uint32_t i) {
    uint32_t imm = (((i >> 21) & 0x3FF) << 1) | (((i >> 20) & 0x1) << 11) |
                   (((i >> 12) & 0xFF) << 12) | (((i >> 31) & 0x1) << 20);
    return sign_extend(imm, 21);
}

const char *riscv_decode(uint32_t instr) {
    static char buf[64];

    uint32_t opcode = OPCODE(instr);
    uint32_t rd = RD(instr);
    uint32_t rs1 = RS1(instr);
    uint32_t rs2 = RS2(instr);
    uint32_t funct3 = FUNCT3(instr);
    uint32_t funct7 = FUNCT7(instr);

    switch (opcode) {
    case 0x37:  // LUI
        snprintf(buf, sizeof(buf), "lui %s, 0x%X", reg_names[rd], (uint32_t)imm_u(instr) >> 12);
        break;
    case 0x17:  // AUIPC
        snprintf(buf, sizeof(buf), "auipc %s, 0x%X", reg_names[rd], (uint32_t)imm_u(instr) >> 12);
        break;
    case 0x6F:  // JAL
        snprintf(buf, sizeof(buf), "jal %s, %d", reg_names[rd], imm_j(instr));
        break;
    case 0x67:  // JALR
        snprintf(buf, sizeof(buf), "jalr %s, %s, %d", reg_names[rd], reg_names[rs1], imm_i(instr));
        break;
    case 0x63:  // Branch
        {
            const char *op;
            switch (funct3) {
            case 0: op = "beq"; break;
            case 1: op = "bne"; break;
            case 4: op = "blt"; break;
            case 5: op = "bge"; break;
            case 6: op = "bltu"; break;
            case 7: op = "bgeu"; break;
            default: op = "b???"; break;
            }
            snprintf(buf, sizeof(buf), "%s %s, %s, %d", op, reg_names[rs1], reg_names[rs2], imm_b(instr));
        }
        break;
    case 0x03:  // Load
        {
            const char *op;
            switch (funct3) {
            case 0: op = "lb"; break;
            case 1: op = "lh"; break;
            case 2: op = "lw"; break;
            case 4: op = "lbu"; break;
            case 5: op = "lhu"; break;
            default: op = "l???"; break;
            }
            snprintf(buf, sizeof(buf), "%s %s, %d(%s)", op, reg_names[rd], imm_i(instr), reg_names[rs1]);
        }
        break;
    case 0x23:  // Store
        {
            const char *op;
            switch (funct3) {
            case 0: op = "sb"; break;
            case 1: op = "sh"; break;
            case 2: op = "sw"; break;
            default: op = "s???"; break;
            }
            snprintf(buf, sizeof(buf), "%s %s, %d(%s)", op, reg_names[rs2], imm_s(instr), reg_names[rs1]);
        }
        break;
    case 0x13:  // I-type ALU
        {
            int32_t imm = imm_i(instr);
            switch (funct3) {
            case 0:
                if (rd == 0 && rs1 == 0 && imm == 0)
                    snprintf(buf, sizeof(buf), "nop");
                else
                    snprintf(buf, sizeof(buf), "addi %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            case 1:
                snprintf(buf, sizeof(buf), "slli %s, %s, %d", reg_names[rd], reg_names[rs1], rs2);
                break;
            case 2:
                snprintf(buf, sizeof(buf), "slti %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            case 3:
                snprintf(buf, sizeof(buf), "sltiu %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            case 4:
                snprintf(buf, sizeof(buf), "xori %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            case 5:
                if (funct7 & 0x20)
                    snprintf(buf, sizeof(buf), "srai %s, %s, %d", reg_names[rd], reg_names[rs1], rs2);
                else
                    snprintf(buf, sizeof(buf), "srli %s, %s, %d", reg_names[rd], reg_names[rs1], rs2);
                break;
            case 6:
                snprintf(buf, sizeof(buf), "ori %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            case 7:
                snprintf(buf, sizeof(buf), "andi %s, %s, %d", reg_names[rd], reg_names[rs1], imm);
                break;
            }
        }
        break;
    case 0x33:  // R-type ALU
        if (funct7 == 0x01) {
            // M extension
            const char *op;
            switch (funct3) {
            case 0: op = "mul"; break;
            case 1: op = "mulh"; break;
            case 2: op = "mulhsu"; break;
            case 3: op = "mulhu"; break;
            case 4: op = "div"; break;
            case 5: op = "divu"; break;
            case 6: op = "rem"; break;
            case 7: op = "remu"; break;
            default: op = "m???"; break;
            }
            snprintf(buf, sizeof(buf), "%s %s, %s, %s", op, reg_names[rd], reg_names[rs1], reg_names[rs2]);
        } else {
            const char *op;
            switch (funct3) {
            case 0: op = (funct7 & 0x20) ? "sub" : "add"; break;
            case 1: op = "sll"; break;
            case 2: op = "slt"; break;
            case 3: op = "sltu"; break;
            case 4: op = "xor"; break;
            case 5: op = (funct7 & 0x20) ? "sra" : "srl"; break;
            case 6: op = "or"; break;
            case 7: op = "and"; break;
            default: op = "???"; break;
            }
            snprintf(buf, sizeof(buf), "%s %s, %s, %s", op, reg_names[rd], reg_names[rs1], reg_names[rs2]);
        }
        break;
    case 0x73:  // System
        if (instr == 0x00100073)
            snprintf(buf, sizeof(buf), "ebreak");
        else if (instr == 0x00000073)
            snprintf(buf, sizeof(buf), "ecall");
        else
            snprintf(buf, sizeof(buf), "system 0x%08X", instr);
        break;
    default:
        snprintf(buf, sizeof(buf), "??? 0x%08X", instr);
        break;
    }

    return buf;
}

void riscv_print_instr(uint32_t addr, uint32_t instr) {
    printf("0x%05X: %08X  %s\n", addr, instr, riscv_decode(instr));
}
