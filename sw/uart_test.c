// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x7FF0;
    
    uart_putc('A');
    
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    
    // Read back
    unsigned int v = *rx_ready_ptr;
    
    uart_putc('[');
    // Print just low byte as 2 hex digits
    int hi = (v >> 4) & 0xF;
    int lo = v & 0xF;
    uart_putc(hi < 10 ? '0' + hi : 'A' + hi - 10);
    uart_putc(lo < 10 ? '0' + lo : 'A' + lo - 10);
    uart_putc(']');
    
    uart_putc('W');  // About to enter wait
    
    // Poll
    int count = 0;
    while (*rx_ready_ptr == 0) {
        count++;
        if (count > 1000000) {
            uart_putc('.');
            count = 0;
        }
    }
    
    uart_putc('X');
    
    while(1);
    return 0;
}
