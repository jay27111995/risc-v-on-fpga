// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    volatile unsigned int *rx_ready_ptr = (volatile unsigned int *)0x7FF0;
    
    uart_putc('A');
    
    *rx_ready_ptr = 0;
    
    uart_putc('B');
    
    // Read back 10 times and print each
    uart_putc('[');
    for (int i = 0; i < 10; i++) {
        unsigned int v = *rx_ready_ptr;
        uart_putc(v == 0 ? '0' : '1');
    }
    uart_putc(']');
    uart_putc('\n');
    
    // Now spin and count consecutive zeros vs non-zeros
    int zeros = 0;
    int ones = 0;
    for (int i = 0; i < 100000; i++) {
        if (*rx_ready_ptr == 0) zeros++;
        else ones++;
    }
    
    // Print stats
    uart_putc('Z');
    uart_putc('=');
    // Print zeros as decimal (rough)
    if (zeros >= 10000) uart_putc('0' + (zeros / 10000) % 10);
    if (zeros >= 1000) uart_putc('0' + (zeros / 1000) % 10);
    if (zeros >= 100) uart_putc('0' + (zeros / 100) % 10);
    if (zeros >= 10) uart_putc('0' + (zeros / 10) % 10);
    uart_putc('0' + zeros % 10);
    uart_putc('\n');
    
    uart_putc('O');
    uart_putc('=');
    if (ones >= 10000) uart_putc('0' + (ones / 10000) % 10);
    if (ones >= 1000) uart_putc('0' + (ones / 1000) % 10);
    if (ones >= 100) uart_putc('0' + (ones / 100) % 10);
    if (ones >= 10) uart_putc('0' + (ones / 10) % 10);
    uart_putc('0' + ones % 10);
    uart_putc('\n');
    
    while(1);
    return 0;
}
