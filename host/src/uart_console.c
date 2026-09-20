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

// DMEM word indices (matches uart.h byte offsets / 4)
#define TX_BUF    (0x100 / 4)   // 64
#define TX_LEN    (0x200 / 4)   // 128
#define TX_READY  (0x204 / 4)   // 129
#define RX_BUF    (0x300 / 4)   // 192
#define RX_LEN    (0x400 / 4)   // 256
#define RX_READY  (0x404 / 4)   // 257

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
    write_dmem(TX_READY, 0);
    write_dmem(TX_LEN, 0);
    write_dmem(RX_READY, 0);
    write_dmem(RX_LEN, 0);

    // Start CPU
    cpu_run();
    raw_mode();

    char input[256];
    int input_len = 0;

    while (1) {
        // Check CPU TX
        if (read_dmem(TX_READY)) {
            int len = read_dmem(TX_LEN);
            if (len > 0 && len < 256) {
                uint32_t word = read_dmem(TX_BUF);
                putchar(word & 0xFF);
                fflush(stdout);
            }
            write_dmem(TX_READY, 0);
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
                        while (read_dmem(RX_READY)) usleep(100);
                        write_dmem(RX_BUF, input[0]);
                        write_dmem(RX_LEN, 1);
                        write_dmem(RX_READY, 1);
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
