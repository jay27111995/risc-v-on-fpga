// UART Library for RISC-V CPU
// Virtual UART over shared DMEM - no hardware UART needed
//
// Memory map (DMEM offsets):
//   0x100: TX_BUF   - CPU writes char, host reads
//   0x200: TX_LEN   - (unused, always 1)
//   0x204: TX_READY - 1 = data ready for host
//   0x300: RX_BUF   - Host writes char, CPU reads
//   0x404: RX_READY - 1 = data ready for CPU

#ifndef UART_H
#define UART_H

// Memory-mapped registers
#define TX_BUF   (*(volatile unsigned int *)0x100)
#define TX_LEN   (*(volatile unsigned int *)0x200)
#define TX_READY (*(volatile unsigned int *)0x204)

#define RX_BUF   (*(volatile unsigned int *)0x300)
#define RX_READY (*(volatile unsigned int *)0x404)

// ===================
// Output functions
// ===================

// Send a single character
static inline void uart_putc(char c) {
    while (TX_READY);
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

// Print integer (decimal)
static inline void uart_print_int(int val) {
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

// Print unsigned integer (decimal)
static inline void uart_print_uint(unsigned int val) {
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

// Print hex value (with 0x prefix)
static inline void uart_print_hex(unsigned int val) {
    uart_putc('0');
    uart_putc('x');
    for (int i = 28; i >= 0; i -= 4) {
        int nibble = (val >> i) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

// ===================
// Input functions
// ===================

// Get single character (blocking)
static inline char uart_getc(void) {
    while (!RX_READY);
    char c = RX_BUF & 0xFF;
    RX_READY = 0;
    return c;
}

// Read string until ';' (blocking)
// Returns number of chars read (not including terminator)
static inline int uart_gets(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = uart_getc();
        if (c == ';') break;
        if (c >= 32 && c <= 126) {
            buf[i++] = c;
        }
    }
    buf[i] = '\0';
    return i;
}

// Read integer (blocking, until ';')
static inline int uart_read_int(void) {
    int val = 0;
    int neg = 0;
    char c;
    
    // Skip leading spaces
    do {
        c = uart_getc();
    } while (c == ' ');
    
    // Check for negative
    if (c == '-') {
        neg = 1;
        c = uart_getc();
    }
    
    // Read digits until ';'
    while (c != ';') {
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
        }
        c = uart_getc();
    }
    
    return neg ? -val : val;
}

#endif
