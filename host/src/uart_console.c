// Virtual UART Console - Circular Buffer Implementation
#include "pcie_vfio.h"
#include "riscv_lib.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// TX circular buffer (CPU writes, Host reads)
#define TX_BUF   (0x100 / 4)
#define TX_HEAD  (0x140 / 4)
#define TX_TAIL  (0x144 / 4)

// RX circular buffer (Host writes, CPU reads)
#define RX_BUF   (0x200 / 4)
#define RX_HEAD  (0x240 / 4)
#define RX_TAIL  (0x244 / 4)

#define BUF_MASK 15

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

    // Clear circular buffer pointers
    write_dmem(TX_HEAD, 0);
    write_dmem(TX_TAIL, 0);
    write_dmem(RX_HEAD, 0);
    write_dmem(RX_TAIL, 0);

    // Start CPU
    cpu_run();
    
    // Debug: wait and check TX pointers
    usleep(100000);
    printf("TX_HEAD=%u TX_TAIL=%u\n", read_dmem(TX_HEAD), read_dmem(TX_TAIL));
    
    raw_mode();

    while (1) {
        // Check CPU TX (read from circular buffer)
        unsigned int tx_head = read_dmem(TX_HEAD);
        unsigned int tx_tail = read_dmem(TX_TAIL);
        
        if (tx_tail != tx_head) {
            unsigned int word = read_dmem(TX_BUF + tx_tail);
            char c = word & 0xFF;
            printf("[%c]", c);
            fflush(stdout);
            tx_tail = (tx_tail + 1) & BUF_MASK;
            write_dmem(TX_TAIL, tx_tail);
        }

        // Check keyboard
        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 1) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                if (c == 3) break;  // Ctrl-C

                if (c >= 32 && c <= 126) {
                    putchar(c);
                    fflush(stdout);

                    // Write to RX circular buffer
                    unsigned int rx_head = read_dmem(RX_HEAD);
                    unsigned int rx_next = (rx_head + 1) & BUF_MASK;
                    unsigned int rx_tail = read_dmem(RX_TAIL);
                    
                    if (rx_next != rx_tail) {
                        write_dmem(RX_BUF + rx_head, c);
                        write_dmem(RX_HEAD, rx_next);
                    }
                }
            }
        }
        usleep(1000);
    }

    printf("\n[Exit]\n");
    vfio_cleanup();
    return 0;
}
