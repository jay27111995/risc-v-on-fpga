// UART Library for RISC-V CPU
// Virtual UART over shared DMEM - no hardware UART needed
//
// Memory map (DMEM offsets):
//   0x100-0x1FF: TX_BUFFER (256 bytes) - CPU writes, host reads
//   0x200:       TX_LEN                - Bytes to send
//   0x204:       TX_READY              - 1 = data ready for host
//   0x300-0x3FF: RX_BUFFER (256 bytes) - Host writes, CPU reads
//   0x400:       RX_LEN                - Bytes received
//   0x404:       RX_READY              - 1 = data ready for CPU

#ifndef UART_H
#define UART_H

// Memory-mapped registers (word-aligned for reliable access)
#define TX_BUF   (*(volatile unsigned int *)0x100)
#define TX_LEN   (*(volatile unsigned int *)0x200)
#define TX_READY (*(volatile unsigned int *)0x204)

#define RX_BUF   (*(volatile unsigned int *)0x300)
#define RX_LEN   (*(volatile unsigned int *)0x400)
#define RX_READY (*(volatile unsigned int *)0x404)

// Send a single character
static inline void uart_putc(char c) {
    while (TX_READY);  // Wait for host to consume previous
    TX_BUF = c;
    TX_LEN = 1;
    TX_READY = 1;
}

// Send a null-terminated string
static inline void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

// Check if RX data available
static inline int uart_rx_ready(void) {
    return RX_READY && RX_LEN > 0;
}

// Read single character (blocking)
static inline char uart_getc(void) {
    while (!uart_rx_ready());
    char c = RX_BUF & 0xFF;
    RX_READY = 0;
    return c;
}

// Print integer
static inline void uart_put_int(int val) {
    if (val < 0) {
        uart_putc('-');
        val = -val;
    }
    if (val == 0) {
        uart_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// Print hex value
static inline void uart_put_hex(unsigned int val) {
    uart_putc('0');
    uart_putc('x');
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (val >> i) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

#endif
