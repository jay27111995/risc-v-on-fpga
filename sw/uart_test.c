// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Try a different address - use 0x800 instead of 0x404
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x800;
    
    uart_putc('A');
    
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    
    // Read back once
    unsigned int v = *rx_ready_ptr;
    uart_putc('0' + (v & 0xF));
    uart_putc('\n');
    
    // Poll
    while (*rx_ready_ptr == 0) {
        // spin
    }
    
    uart_putc('X');
    uart_putc('\n');
    
    while(1);
    return 0;
}
