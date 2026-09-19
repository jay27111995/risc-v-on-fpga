// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x404;
    
    uart_putc('A');
    
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    
    // Read in loop and print each time
    for (int i = 0; i < 5; i++) {
        unsigned int val = *rx_ready_ptr;
        uart_putc('0' + (val & 0xF));
    }
    uart_putc('\n');
    
    uart_putc('W');
    
    // Manual polling loop
    unsigned int ready;
    do {
        ready = *rx_ready_ptr;
    } while (ready == 0);
    
    uart_putc('X');
    uart_putc('=');
    uart_putc('0' + (ready & 0xF));
    uart_putc('\n');
    
    while(1);
    return 0;
}
