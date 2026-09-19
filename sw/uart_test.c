// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Explicitly clear RX_READY before starting (in case of garbage)
    RX_READY = 0;
    
    // Send characters one at a time
    uart_putc('H');
    uart_putc('e');
    uart_putc('l');
    uart_putc('l');
    uart_putc('o');
    uart_putc(' ');
    uart_putc('f');
    uart_putc('r');
    uart_putc('o');
    uart_putc('m');
    uart_putc(' ');
    uart_putc('R');
    uart_putc('I');
    uart_putc('S');
    uart_putc('C');
    uart_putc('-');
    uart_putc('V');
    uart_putc('!');
    uart_putc('\n');
    
    // Echo loop
    while (1) {
        char c = uart_getc();
        uart_putc(c);
    }

    return 0;
}
