// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');  // Prompt
    
    // Wait for RX_READY
    while (!RX_READY);
    
    // Read first 2 chars manually (no loop)
    volatile unsigned int *rx_buf = (volatile unsigned int *)0x300;
    char c0 = rx_buf[0] & 0xFF;
    char c1 = rx_buf[1] & 0xFF;
    
    // Clear
    RX_READY = 0;
    RX_LEN = 0;
    
    // Echo them
    uart_putc(c0);
    uart_putc(c1);
    uart_putc('\n');
    
    while(1);
    return 0;
}
