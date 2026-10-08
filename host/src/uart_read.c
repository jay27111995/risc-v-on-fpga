// Read UART buffer once and exit (for automated testing)
#include "pcie_vfio.h"
#include "riscv_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// TX circular buffer (CPU writes, Host reads)
#define TX_BUF   (0x100 / 4)
#define TX_HEAD  (0x140 / 4)
#define TX_TAIL  (0x144 / 4)

#define BUF_MASK 15

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <pci_addr> <iommu_group>\n", argv[0]);
        return 1;
    }

    if (vfio_init(argv[1], atoi(argv[2])) < 0) {
        fprintf(stderr, "VFIO init failed\n");
        return 1;
    }

    // Read all available output from TX buffer
    unsigned int tx_head = read_dmem(TX_HEAD);
    unsigned int tx_tail = read_dmem(TX_TAIL);
    
    while (tx_tail != tx_head) {
        unsigned int word = read_dmem(TX_BUF + tx_tail);
        char c = word & 0xFF;
        putchar(c);
        tx_tail = (tx_tail + 1) & BUF_MASK;
    }
    
    // Update tail pointer
    write_dmem(TX_TAIL, tx_tail);
    
    fflush(stdout);
    vfio_cleanup();
    return 0;
}
