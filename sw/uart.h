// UART Library for RISC-V CPU
// Virtual UART over shared DMEM - circular buffer implementation
//
// Memory map (DMEM offsets):
//   TX (CPU → Host):
//     0x100-0x13F: TX_BUF[16]  - 16-word circular buffer
//     0x140:       TX_HEAD     - CPU writes here (0-15)
//     0x144:       TX_TAIL     - Host reads from here (0-15)
//
//   RX (Host → CPU):
//     0x200-0x23F: RX_BUF[16]  - 16-word circular buffer
//     0x240:       RX_HEAD     - Host writes here (0-15)
//     0x244:       RX_TAIL     - CPU reads from here (0-15)

#ifndef UART_H
#define UART_H

// TX circular buffer (CPU writes, Host reads)
#define TX_BUF   ((volatile unsigned int *)0x100)
#define TX_HEAD  (*(volatile unsigned int *)0x140)
#define TX_TAIL  (*(volatile unsigned int *)0x144)

// RX circular buffer (Host writes, CPU reads)
#define RX_BUF   ((volatile unsigned int *)0x200)
#define RX_HEAD  (*(volatile unsigned int *)0x240)
#define RX_TAIL  (*(volatile unsigned int *)0x244)

#define BUF_SIZE 16
#define BUF_MASK 15

// Send single char (blocking if buffer full)
static inline void uart_putc(char c) {
    unsigned int head = TX_HEAD;
    unsigned int next = (head + 1) & BUF_MASK;
    
    // Wait if buffer full
    while (next == TX_TAIL);
    
    TX_BUF[head] = c;
    TX_HEAD = next;
}

// Check if RX data available
static inline int uart_rx_ready(void) {
    return RX_HEAD != RX_TAIL;
}

// Get single char (blocking if buffer empty)
static inline char uart_getc(void) {
    unsigned int tail = RX_TAIL;
    
    // Wait if buffer empty
    while (RX_HEAD == tail);
    
    char c = RX_BUF[tail] & 0xFF;
    RX_TAIL = (tail + 1) & BUF_MASK;
    return c;
}

#endif
