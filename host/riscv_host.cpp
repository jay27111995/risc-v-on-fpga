// RISC-V SoC Host Controller
// Loads program, runs CPU, reads results via PCIe BAR

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

// BAR Memory Map
#define BAR_CTRL      0x0000   // [0] RUN, [1] RESET
#define BAR_STATUS    0x0008   // [0] RUNNING
#define BAR_PC        0x0010   // Current PC
#define BAR_RESULT    0x0018   // CPU result
#define BAR_IMEM      0x1000   // Instruction memory (4KB)
#define BAR_DMEM      0x2000   // Data memory (8KB)

// Control bits
#define CTRL_RUN      (1 << 0)
#define CTRL_RESET    (1 << 1)

class RiscvSoc {
private:
    volatile uint32_t* bar;
    size_t bar_size;
    int fd;

public:
    RiscvSoc(const char* device, size_t size = 0x10000) : bar_size(size) {
        fd = open(device, O_RDWR | O_SYNC);
        if (fd < 0) {
            perror("Failed to open device");
            exit(1);
        }
        
        bar = (volatile uint32_t*)mmap(NULL, bar_size, 
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, 0);
        if (bar == MAP_FAILED) {
            perror("Failed to mmap BAR");
            close(fd);
            exit(1);
        }
        printf("Mapped BAR at %p\n", bar);
    }
    
    ~RiscvSoc() {
        munmap((void*)bar, bar_size);
        close(fd);
    }
    
    // Register access
    void write32(uint32_t offset, uint32_t value) {
        bar[offset / 4] = value;
    }
    
    uint32_t read32(uint32_t offset) {
        return bar[offset / 4];
    }
    
    // Control
    void reset() {
        write32(BAR_CTRL, CTRL_RESET);
        usleep(1000);  // Wait for reset
    }
    
    void run() {
        write32(BAR_CTRL, CTRL_RUN);
    }
    
    void stop() {
        write32(BAR_CTRL, 0);
    }
    
    bool is_running() {
        return read32(BAR_STATUS) & 1;
    }
    
    uint32_t get_pc() {
        return read32(BAR_PC);
    }
    
    uint32_t get_result() {
        return read32(BAR_RESULT);
    }
    
    // Memory access
    void write_imem(uint32_t word_idx, uint32_t instr) {
        write32(BAR_IMEM + word_idx * 4, instr);
    }
    
    void write_dmem(uint32_t word_idx, uint32_t data) {
        write32(BAR_DMEM + word_idx * 4, data);
    }
    
    uint32_t read_dmem(uint32_t word_idx) {
        return read32(BAR_DMEM + word_idx * 4);
    }
    
    // Load program from array
    void load_program(const uint32_t* program, size_t count) {
        printf("Loading %zu instructions...\n", count);
        for (size_t i = 0; i < count; i++) {
            write_imem(i, program[i]);
        }
    }
    
    // Run and wait
    void run_cycles(int cycles) {
        run();
        usleep(cycles * 10);  // Rough timing
        stop();
    }
};

// Test program: compute 5 + 3, store to DMEM[0]
uint32_t test_program[] = {
    0x00500093,  // ADDI x1, x0, 5     ; x1 = 5
    0x00300113,  // ADDI x2, x0, 3     ; x2 = 3
    0x002081b3,  // ADD  x3, x1, x2    ; x3 = 8
    0x00302023,  // SW   x3, 0(x0)     ; dmem[0] = 8
    0x00000063,  // BEQ  x0, x0, 0     ; loop forever
};

int main(int argc, char* argv[]) {
    const char* device = "/sys/bus/pci/devices/0000:01:00.0/resource0";
    
    if (argc > 1) {
        device = argv[1];
    }
    
    printf("RISC-V SoC Host Controller\n");
    printf("==========================\n\n");
    printf("Using device: %s\n\n", device);
    
    RiscvSoc soc(device);
    
    // Reset CPU
    printf("Resetting CPU...\n");
    soc.reset();
    
    // Load program
    soc.load_program(test_program, sizeof(test_program) / sizeof(test_program[0]));
    
    // Clear DMEM[0]
    soc.write_dmem(0, 0);
    printf("DMEM[0] before: %d\n", soc.read_dmem(0));
    
    // Run CPU
    printf("Running CPU...\n");
    soc.run();
    
    // Wait a bit
    usleep(100000);  // 100ms
    
    // Check results
    printf("\nResults:\n");
    printf("  PC:      0x%X\n", soc.get_pc());
    printf("  DMEM[0]: %d (expected 8)\n", soc.read_dmem(0));
    
    // Stop
    soc.stop();
    
    // Verify
    if (soc.read_dmem(0) == 8) {
        printf("\n✓ TEST PASSED!\n");
        return 0;
    } else {
        printf("\n✗ TEST FAILED!\n");
        return 1;
    }
}
