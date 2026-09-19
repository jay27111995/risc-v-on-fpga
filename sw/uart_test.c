// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Try address 0x600 (in middle of DMEM, away from TX/RX regions)
    volatile unsigned int *test_ptr = (volatile unsigned int *)0x600;
    
    uart_putc('A');
    
    *test_ptr = 0;
    
    uart_putc('B');
    
    // Read back 10 times and print each
    uart_putc('[');
    for (volatile int i = 0; i < 10; i++) {
        unsigned int v = *test_ptr;
        uart_putc(v == 0 ? '0' : '1');
    }
    uart_putc(']');
    uart_putc('\n');
    
    // Now spin and count
    volatile int zeros = 0;
    volatile int ones = 0;
    for (volatile int i = 0; i < 10000; i++) {
        if (*test_ptr == 0) zeros++;
        else ones++;
    }
    
    // Print stats
    uart_putc('Z');
    uart_putc('=');
    uart_putc('0' + (zeros / 1000) % 10);
    uart_putc('0' + (zeros / 100) % 10);
    uart_putc('0' + (zeros / 10) % 10);
    uart_putc('0' + zeros % 10);
    uart_putc('\n');
    
    uart_putc('O');
    uart_putc('=');
    uart_putc('0' + (ones / 1000) % 10);
    uart_putc('0' + (ones / 100) % 10);
    uart_putc('0' + (ones / 10) % 10);
    uart_putc('0' + ones % 10);
    uart_putc('\n');
    
    while(1);
    return 0;
}
