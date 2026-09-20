// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Greeting
    uart_putc('H');
    uart_putc('e');
    uart_putc('l');
    uart_putc('l');
    uart_putc('o');
    uart_putc('!');
    uart_putc('\n');
    uart_putc('>');
    
    char buf[64];
    int pos = 0;
    
    while (1) {
        // Wait for char
        while (!RX_READY);
        char c = RX_BUF & 0xFF;
        RX_READY = 0;
        
        if (c == ';') {
            // Echo back the line
            uart_putc('\n');
            uart_putc(':');
            for (int i = 0; i < pos; i++) {
                uart_putc(buf[i]);
            }
            uart_putc('\n');
            uart_putc('>');
            pos = 0;
        } else if (c >= 32 && c <= 126 && pos < 63) {
            buf[pos++] = c;
        }
    }
    
    return 0;
}
