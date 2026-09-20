// UART Library for RISC-V CPU
// Virtual UART over shared DMEM - 1 char at a time
//
// Memory map (DMEM offsets):
//   0x100: TX_BUF   - CPU writes char, host reads
//   0x204: TX_READY - 1 = data ready for host
//   0x300: RX_BUF   - Host writes char, CPU reads
//   0x404: RX_READY - 1 = data ready for CPU

#ifndef UART_H
#define UART_H

#define TX_BUF   (*(volatile unsigned int *)0x100)
#define TX_LEN   (*(volatile unsigned int *)0x200)
#define TX_READY (*(volatile unsigned int *)0x204)
#define RX_BUF   (*(volatile unsigned int *)0x300)
#define RX_READY (*(volatile unsigned int *)0x404)

// Send single char (blocking)
static inline void uart_putc(char c) {
    while (TX_READY);
    TX_BUF = c;
    TX_LEN = 1;
    TX_READY = 1;
}

// Get single char (blocking)
static inline char uart_getc(void) {
    while (!RX_READY);
    char c = RX_BUF & 0xFF;
    RX_READY = 0;
    return c;
}

#endif
