// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Just send Hi and halt - no RX at all
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Halt
    while (1);
    return 0;
}
