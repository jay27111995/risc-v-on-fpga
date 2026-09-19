// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x404;
    
    uart_putc('A');
    
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    
    // Simple counter-based polling
    int loops = 0;
    while (1) {
        unsigned int val = *rx_ready_ptr;
        if (val != 0) {
            uart_putc('!');
            break;
        }
        loops++;
        if (loops >= 100000) {
            uart_putc('.');
            loops = 0;
        }
    }
    
    uart_putc('X');
    uart_putc('\n');
    
    // Now read the data
    volatile unsigned int *rx_len_ptr = (volatile unsigned int *)0x400;
    volatile unsigned int *rx_buf_ptr = (volatile unsigned int *)0x300;
    
    unsigned int len = *rx_len_ptr;
    uart_putc('L');
    uart_putc('=');
    uart_putc('0' + (len & 0xF));
    uart_putc('\n');
    
    unsigned int word = *rx_buf_ptr;
    char c = word & 0xFF;
    uart_putc('[');
    uart_putc(c);
    uart_putc(']');
    uart_putc('\n');
    
    *rx_ready_ptr = 0;
    
    while(1);
    return 0;
}
