// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Use direct address, not the macro
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x404;
    
    uart_putc('A');
    uart_putc('\n');
    
    // Write 0
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    uart_putc('\n');
    
    // Read back
    unsigned int val = *rx_ready_ptr;
    uart_putc('V');
    uart_putc('=');
    uart_putc('0' + (val & 0xF));
    uart_putc('\n');
    
    uart_putc('C');
    uart_putc('\n');
    
    // Spin forever
    while(1);
    
    return 0;
}
