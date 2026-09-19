// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

// Can't use string literals directly - they're in IMEM but loads read from DMEM
// So we write characters directly
void print_hello(void) {
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
}

int main(void) {
    print_hello();
    
    // Simple echo loop - just echo single characters
    while (1) {
        char c = uart_getc();
        uart_putc(c);
    }

    return 0;
}
