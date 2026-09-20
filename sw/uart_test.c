// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Greeting
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Simple echo loop - no buffering
    while (1) {
        // Wait for char
        while (!RX_READY);
        char c = RX_BUF & 0xFF;
        RX_READY = 0;
        
        // Echo it back
        uart_putc(c);
    }
    
    return 0;
}
