// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Greeting
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Simple echo loop
    while (1) {
        char c = uart_getc();  // Blocking read
        uart_putc(c);          // Echo back
    }
    
    return 0;
}
