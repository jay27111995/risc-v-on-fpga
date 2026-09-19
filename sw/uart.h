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

// Memory-mapped registers
#define TX_BUFFER      ((volatile char*)0x100)
#define TX_LEN         (*(volatile unsigned int*)0x200)
#define TX_READY       (*(volatile unsigned int*)0x204)

#define RX_BUFFER      ((volatile char*)0x300)
#define RX_LEN         (*(volatile unsigned int*)0x400)
#define RX_READY       (*(volatile unsigned int*)0x404)

#define UART_BUF_SIZE  256

// Send a single character
static inline void uart_putc(char c) {
    while (TX_READY);  // Wait for host to consume previous
    // Write as word to avoid byte store issues
    volatile unsigned int *tx_buf = (volatile unsigned int *)0x100;
    *tx_buf = (unsigned int)(unsigned char)c;
    TX_LEN = 1;
    TX_READY = 1;
}

// Send a null-terminated string
static inline void uart_puts(const char *s) {
    while (TX_READY);  // Wait for host to consume previous
    
    int i = 0;
    while (s[i] && i < UART_BUF_SIZE) {
        TX_BUFFER[i] = s[i];
        i++;
    }
    TX_LEN = i;
    TX_READY = 1;
}

// Send a buffer of known length
static inline void uart_write(const char *buf, int len) {
    while (TX_READY);  // Wait for host to consume previous
    
    if (len > UART_BUF_SIZE) len = UART_BUF_SIZE;
    for (int i = 0; i < len; i++) {
        TX_BUFFER[i] = buf[i];
    }
    TX_LEN = len;
    TX_READY = 1;
}

// Check if data available from host
static inline int uart_rx_available(void) {
    return RX_READY;
}

// Blocking read - wait for host to send data
static inline int uart_read(char *buf, int max_len) {
    // Wait for host to signal data ready AND have non-zero length
    while (!RX_READY || RX_LEN == 0);
    
    int len = RX_LEN;
    if (len > max_len) len = max_len;
    for (int i = 0; i < len; i++) {
        buf[i] = RX_BUFFER[i];
    }
    RX_READY = 0;  // Ack: consumed
    return len;
}

// Non-blocking read - returns 0 if no data
static inline int uart_read_nonblock(char *buf, int max_len) {
    if (!RX_READY) return 0;
    return uart_read(buf, max_len);
}

// Read a single character (blocking)
static inline char uart_getc(void) {
    char c;
    uart_read(&c, 1);
    return c;
}

// Simple integer to string (for printf-like usage)
static inline void uart_put_int(int val) {
    char buf[12];
    int i = 0;
    int neg = 0;
    
    if (val < 0) {
        neg = 1;
        val = -val;
    }
    
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    
    if (neg) buf[i++] = '-';
    
    // Reverse
    char out[12];
    for (int j = 0; j < i; j++) {
        out[j] = buf[i - 1 - j];
    }
    out[i] = '\0';
    uart_puts(out);
}

// Print hex value
static inline void uart_put_hex(unsigned int val) {
    const char hex[] = "0123456789ABCDEF";
    char buf[11] = "0x00000000";
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    uart_puts(buf);
}

#endif // UART_H
