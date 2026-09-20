// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');
    
    // Print RX_READY before waiting
    unsigned int ready = RX_READY;
    uart_putc('R');
    uart_putc('=');
    uart_putc('0' + (ready & 0xF));
    uart_putc('\n');
    
    // Wait for RX_READY with explicit loop
    uart_putc('W');  // Waiting...
    volatile unsigned int *rx_ready = (volatile unsigned int *)0x404;
    while (*rx_ready == 0) {
        // spin
    }
    uart_putc('!');  // Got it
    uart_putc('\n');
    
    // Print RX_LEN
    unsigned int len = RX_LEN;
    uart_putc('L');
    uart_putc('=');
    uart_putc('0' + ((len >> 4) & 0xF));
    uart_putc('0' + (len & 0xF));
    uart_putc('\n');
    
    // First char
    char c = RX_BUF & 0xFF;
    uart_putc(c);
    uart_putc('\n');
    
    // Clear
    RX_READY = 0;
    RX_LEN = 0;
    
    while(1);
    return 0;
}
