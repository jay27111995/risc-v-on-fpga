// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Debug: print raw values
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Wait for input
    while (!RX_READY);
    
    // Read RX_LEN raw value and print as hex
    unsigned int len_raw = RX_LEN;
    uart_putc('L');
    uart_putc(':');
    // Print as 8 hex digits
    for (int i = 7; i >= 0; i--) {
        int nibble = (len_raw >> (i * 4)) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    uart_putc('\n');
    
    // Read RX_BUFFER[0] raw word
    volatile unsigned int *rx_words = (volatile unsigned int *)0x300;
    unsigned int buf_raw = rx_words[0];
    uart_putc('B');
    uart_putc(':');
    for (int i = 7; i >= 0; i--) {
        int nibble = (buf_raw >> (i * 4)) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    uart_putc('\n');
    
    RX_READY = 0;
    
    while (1);
    return 0;
}
