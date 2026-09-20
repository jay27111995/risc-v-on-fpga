// Virtual UART Console
// Host-side terminal for RISC-V virtual UART over PCIe.
//
// Usage: sudo ./uart_console <pci_addr> <iommu_group>

#include "pcie_vfio.h"
#include "riscv_lib.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// DMEM offsets (matches uart.h)
#define DMEM      0x80000
#define TX_BUF    (DMEM + 0x100)
#define TX_LEN    (DMEM + 0x200)
#define TX_READY  (DMEM + 0x204)
#define RX_BUF    (DMEM + 0x300)
#define RX_LEN    (DMEM + 0x400)
#define RX_READY  (DMEM + 0x404)

static struct termios orig_termios;

void cleanup(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(cleanup);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pci_addr> <iommu_group>\n", argv[0]);
        return 1;
    }

    printf("UART Console - %s (group %s)\n", argv[1], argv[2]);
    printf("Ctrl-C to exit\n\n");

    if (vfio_init(argv[1], atoi(argv[2])) < 0) {
        fprintf(stderr, "VFIO init failed\n");
        return 1;
    }

    // Clear UART registers
    write32(TX_READY, 0);
    write32(RX_READY, 0);
    write32(RX_LEN, 0);

    // Start CPU
    cpu_run();
    raw_mode();

    char input[256];
    int input_len = 0;

    while (1) {
        // Check CPU TX
        if (read32(TX_READY)) {
            int len = read32(TX_LEN);
            if (len > 0 && len < 256) {
                // Read char (first byte of word)
                uint32_t word = read32(TX_BUF);
                putchar(word & 0xFF);
                fflush(stdout);
            }
            write32(TX_READY, 0);
        }

        // Check keyboard
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 1) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                if (c == 3) break;  // Ctrl-C

                putchar(c);
                if (c == '\r') putchar('\n');
                fflush(stdout);

                if (c == '\r' || c == '\n') {
                    if (input_len > 0) {
                        while (read32(RX_READY)) usleep(100);
                        write32(RX_BUF, input[0]);  // Single char for now
                        write32(RX_LEN, 1);
                        write32(RX_READY, 1);
                        input_len = 0;
                    }
                } else if (input_len < 255) {
                    input[input_len++] = c;
                }
            }
        }
        usleep(1000);
    }

    printf("\n[Exit]\n");
    vfio_cleanup();
    return 0;
}
