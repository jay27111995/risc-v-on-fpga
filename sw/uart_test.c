// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // TX and RX_READY work - now test reading data
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Wait for input
    while (!RX_READY);
    
    // Read length
    int len = RX_LEN;
    uart_putc('L');
    uart_putc('=');
    uart_putc('0' + len);
    uart_putc('\n');
    
    // Read first byte (word-aligned)
    volatile unsigned int *rx_words = (volatile unsigned int *)0x300;
    unsigned int word0 = rx_words[0];
    char c0 = word0 & 0xFF;
    
    // Echo it
    uart_putc('C');
    uart_putc('=');
    uart_putc(c0);
    uart_putc('\n');
    
    // Clear RX_READY
    RX_READY = 0;
    
    uart_putc('D');
    uart_putc('o');
    uart_putc('n');
    uart_putc('e');
    uart_putc('\n');
    
    while (1);
    return 0;
}
