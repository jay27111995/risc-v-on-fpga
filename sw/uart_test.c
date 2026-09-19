// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Use address near end of DMEM (32KB = 0x8000, so use 0x7FF0)
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x7FF0;
    
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
