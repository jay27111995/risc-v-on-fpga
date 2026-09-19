// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // TX works - now test RX_READY polling
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Print what CPU sees for RX_READY (as hex digit)
    uart_putc('R');
    uart_putc('X');
    uart_putc('=');
    uart_putc('0' + (RX_READY & 0xF));
    uart_putc('\n');
    
    // Now wait for RX_READY to become 1
    uart_putc('W');
    uart_putc('a');
    uart_putc('i');
    uart_putc('t');
    uart_putc('\n');
    
    while (!RX_READY);  // Block here
    
    // If we get here, RX_READY was set
    uart_putc('G');
    uart_putc('o');
    uart_putc('t');
    uart_putc('!');
    uart_putc('\n');
    
    while (1);
    return 0;
}
