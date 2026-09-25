// Simple CSR test testbench for Verilator
#include <verilated.h>
#include "Vriscv_soc.h"
#include "Vriscv_soc___024root.h"
#include <iostream>
#include <cstdio>
#include <cstring>

#define MAX_CYCLES 100000

// Host interface addresses
#define CTRL_REG     0x00000  // Control register
#define STATUS_REG   0x00008  // Status register: [0]=running, [1]=halted
#define PC_REG       0x00010  // PC register
#define IMEM_BASE    0x20000  // IMEM base (128KB)
#define DMEM_BASE    0x80000  // DMEM base (32KB)

// UART offsets in DMEM
#define TX_BUF_OFF   0x100
#define TX_HEAD_OFF  0x140
#define TX_TAIL_OFF  0x144

void host_write(Vriscv_soc* dut, uint32_t addr, uint32_t data) {
    dut->addr = addr;  // Full byte address (20 bits used)
    dut->wdata = data;
    dut->wen = 1;
    dut->ren = 0;
    // Rising edge - write happens here
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
    dut->wen = 0;
    // One more cycle to ensure write completes
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

uint32_t host_read(Vriscv_soc* dut, uint32_t addr) {
    dut->addr = addr;  // Full byte address
    dut->wen = 0;
    dut->ren = 1;
    // Rising edge - read is registered
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
    // Another cycle for the registered read
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
    dut->ren = 0;
    return dut->rdata;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <elf_file>" << std::endl;
        return 1;
    }
    
    Verilated::commandArgs(argc, argv);
    Vriscv_soc* dut = new Vriscv_soc;
    
    // Initialize
    dut->clk = 0;
    dut->rst_n = 0;
    dut->addr = 0;
    dut->wdata = 0;
    dut->wen = 0;
    dut->ren = 0;
    
    // Reset
    for (int i = 0; i < 10; i++) {
        dut->clk = !dut->clk;
        dut->eval();
    }
    dut->rst_n = 1;
    for (int i = 0; i < 10; i++) {
        dut->clk = !dut->clk;
        dut->eval();
    }
    
    // Load ELF file via host interface
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        std::cerr << "Cannot open " << argv[1] << std::endl;
        return 1;
    }
    
    uint8_t ehdr[52];
    fread(ehdr, 1, 52, f);
    
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') {
        std::cerr << "Not an ELF file" << std::endl;
        fclose(f);
        return 1;
    }
    
    uint32_t phoff = *(uint32_t*)&ehdr[28];
    uint16_t phnum = *(uint16_t*)&ehdr[44];
    uint16_t phentsize = *(uint16_t*)&ehdr[42];
    
    printf("Loading ELF: %d program headers\n", phnum);
    
    for (int i = 0; i < phnum; i++) {
        uint8_t phdr[32];
        fseek(f, phoff + i * phentsize, SEEK_SET);
        fread(phdr, 1, 32, f);
        
        uint32_t p_type = *(uint32_t*)&phdr[0];
        if (p_type != 1) continue;
        
        uint32_t p_offset = *(uint32_t*)&phdr[4];
        uint32_t p_vaddr = *(uint32_t*)&phdr[8];
        uint32_t p_filesz = *(uint32_t*)&phdr[16];
        
        printf("  Segment: vaddr=0x%08x, foff=0x%x, size=%d\n", p_vaddr, p_offset, p_filesz);
        
        uint8_t* data = new uint8_t[p_filesz + 4];
        memset(data, 0, p_filesz + 4);
        fseek(f, p_offset, SEEK_SET);
        fread(data, 1, p_filesz, f);
        
        if (p_vaddr < 0x10000000) {
            // IMEM - write via host interface
            printf("    Loading to IMEM at 0x%08x\n", IMEM_BASE + p_vaddr);
            for (uint32_t j = 0; j < p_filesz; j += 4) {
                uint32_t word = data[j] | (data[j+1]<<8) | (data[j+2]<<16) | (data[j+3]<<24);
                host_write(dut, IMEM_BASE + p_vaddr + j, word);
                if (j == 0) printf("    First word: 0x%08x\n", word);
            }
        } else {
            // DMEM
            uint32_t dmem_off = p_vaddr - 0x10000000;
            for (uint32_t j = 0; j < p_filesz; j += 4) {
                uint32_t word = data[j] | (data[j+1]<<8) | (data[j+2]<<16) | (data[j+3]<<24);
                host_write(dut, DMEM_BASE + dmem_off + j, word);
            }
        }
        delete[] data;
    }
    fclose(f);
    
    // Verify first instruction loaded
    uint32_t instr0 = host_read(dut, IMEM_BASE);
    printf("Verify IMEM[0]: 0x%08x\n", instr0);
    
    // Initialize UART pointers (TX_HEAD=TX_TAIL=0)
    host_write(dut, DMEM_BASE + TX_HEAD_OFF, 0);
    host_write(dut, DMEM_BASE + TX_TAIL_OFF, 0);
    
    // Start CPU: write 1 to control register
    printf("Starting CPU...\n");
    host_write(dut, CTRL_REG, 1);
    
    int cycles = 0;
    bool done = false;
    int tx_tail = 0;
    
    while (cycles < MAX_CYCLES && !done) {
        dut->clk = 1; dut->eval();
        dut->clk = 0; dut->eval();
        cycles++;
        
        // Check status register for halt
        uint32_t status = host_read(dut, STATUS_REG);
        if (status & 2) {  // Halted bit is [1]
            done = true;
            uint32_t pc = host_read(dut, PC_REG);
            printf("\n[CPU halted at cycle %d, PC=0x%08x]\n", cycles, pc);
        }
        
        // Poll UART TX buffer every cycle 
        int tx_head = host_read(dut, DMEM_BASE + TX_HEAD_OFF) & 0xF;
        while (tx_tail != tx_head) {
            uint32_t ch = host_read(dut, DMEM_BASE + TX_BUF_OFF + tx_tail * 4) & 0xFF;
            putchar(ch);
            fflush(stdout);
            tx_tail = (tx_tail + 1) & 0xF;
            host_write(dut, DMEM_BASE + TX_TAIL_OFF, tx_tail);
            tx_head = host_read(dut, DMEM_BASE + TX_HEAD_OFF) & 0xF;
        }
    }
    
    // Drain any remaining UART output
    int tx_head = host_read(dut, DMEM_BASE + TX_HEAD_OFF) & 0xF;
    while (tx_tail != tx_head) {
        uint32_t ch = host_read(dut, DMEM_BASE + TX_BUF_OFF + tx_tail * 4) & 0xFF;
        putchar(ch);
        tx_tail = (tx_tail + 1) & 0xF;
    }
    
    if (!done) {
        uint32_t pc = host_read(dut, PC_REG);
        printf("\n[Timeout after %d cycles, PC=0x%08x]\n", cycles, pc);
    }
    
    // Read CSRs from internal state
    printf("\n=== Final CSR State ===\n");
    printf("mstatus: 0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mstatus);
    printf("mie:     0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mie);
    printf("mtvec:   0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mtvec);
    printf("mepc:    0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mepc);
    printf("mcause:  0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mcause);
    printf("mip:     0x%08x\n", dut->rootp->riscv_soc__DOT__csr_mip);
    printf("mtime:   %lu cycles\n", (unsigned long)dut->rootp->riscv_soc__DOT__mtime);
    
    delete dut;
    return done ? 0 : 1;
}
