// Virtual UART Console
// ============================================================================
// Host-side terminal for RISC-V virtual UART over PCIe.
// Polls DMEM for CPU output, can send input to CPU.
//
// Usage: sudo ./uart_console <pci_addr> <iommu_group>
// ============================================================================

#include "pcie_vfio.h"
#include "riscv_lib.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// DMEM offsets for virtual UART (matches uart.h on CPU side)
#define DMEM_BASE       0x80000

#define TX_BUFFER_OFF   (DMEM_BASE + 0x100)
#define TX_LEN_OFF      (DMEM_BASE + 0x200)
#define TX_READY_OFF    (DMEM_BASE + 0x204)

#define RX_BUFFER_OFF   (DMEM_BASE + 0x300)
#define RX_LEN_OFF      (DMEM_BASE + 0x400)
#define RX_READY_OFF    (DMEM_BASE + 0x404)

#define UART_BUF_SIZE   256

// Terminal settings for raw mode
static struct termios orig_termios;
static int raw_mode = 0;

void disable_raw_mode(void) {
    if (raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode = 0;
    }
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);  // Disable echo and canonical mode
    raw.c_cc[VMIN] = 0;               // Non-blocking
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode = 1;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pci_addr> <iommu_group>\n", argv[0]);
        fprintf(stderr, "Example: %s 0000:b1:00.0 12\n", argv[0]);
        return 1;
    }

    const char *pci_addr = argv[1];
    int iommu_group = atoi(argv[2]);

    printf("RISC-V Virtual UART Console\n");
    printf("===========================\n");
    printf("PCI: %s, IOMMU group: %d\n", pci_addr, iommu_group);
    printf("Press Ctrl-C to exit\n\n");

    // Initialize VFIO
    if (vfio_init(pci_addr, iommu_group) < 0) {
        fprintf(stderr, "Failed to initialize VFIO\n");
        return 1;
    }

    // Clear mailbox registers and buffers
    write32(TX_READY_OFF, 0);
    write32(RX_READY_OFF, 0);
    write32(TX_LEN_OFF, 0);
    write32(RX_LEN_OFF, 0);
    // Clear RX buffer area to prevent garbage reads
    for (int i = 0; i < 256; i += 4) {
        write32(RX_BUFFER_OFF + i, 0);
    }

    // Just run the CPU (don't reset - loader already set up everything)
    cpu_run();

    // Enable raw terminal mode for character-by-character input
    enable_raw_mode();

    printf("Console ready. CPU output will appear below:\n");
    printf("--------------------------------------------\n");
    fflush(stdout);

    // Main polling loop
    char input_buf[UART_BUF_SIZE];
    int input_len = 0;

    while (1) {
        // Check for CPU output (TX_READY == 1)
        if (read32(TX_READY_OFF) == 1) {
            int len = read32(TX_LEN_OFF);
            if (len > 0 && len <= UART_BUF_SIZE) {
                // Read buffer word by word, extract bytes
                for (int i = 0; i < len; i++) {
                    uint32_t word_off = (i / 4) * 4;
                    uint32_t byte_pos = i % 4;
                    uint32_t word = read32(TX_BUFFER_OFF + word_off);
                    char c = (word >> (byte_pos * 8)) & 0xFF;
                    putchar(c);
                }
                fflush(stdout);
            }
            // Acknowledge: ready for more
            write32(TX_READY_OFF, 0);
        }

        // Check for keyboard input
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        if (poll(&pfd, 1, 1) > 0) {  // 1ms timeout
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                // Ctrl-C to exit
                if (c == 3) {
                    printf("\n[Console terminated]\n");
                    break;
                }

                // Echo locally
                putchar(c);
                if (c == '\r') putchar('\n');
                fflush(stdout);

                // Buffer input until newline
                if (c == '\r' || c == '\n') {
                    if (input_len > 0) {
                        // Wait for CPU to be ready
                        while (read32(RX_READY_OFF) == 1) {
                            usleep(100);
                        }

                        // Write to RX buffer
                        for (int i = 0; i < input_len; i++) {
                            uint32_t addr = RX_BUFFER_OFF + (i & ~3);
                            uint32_t shift = 8 * (i & 3);
                            uint32_t val = read32(addr);
                            val &= ~(0xFF << shift);
                            val |= ((uint32_t)input_buf[i] & 0xFF) << shift;
                            write32(addr, val);
                        }
                        write32(RX_LEN_OFF, input_len);
                        write32(RX_READY_OFF, 1);  // Signal CPU

                        input_len = 0;
                    }
                } else if (input_len < UART_BUF_SIZE - 1) {
                    input_buf[input_len++] = c;
                }
            }
        }

        usleep(1000);  // 1ms polling interval
    }

    disable_raw_mode();
    vfio_cleanup();
    return 0;
}
