// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');  // Prompt
    
    // Wait for RX_READY
    while (!RX_READY);
    
    // Read first char
    char c = RX_BUF & 0xFF;
    
    // Clear
    RX_READY = 0;
    RX_LEN = 0;
    
    // Echo it
    uart_putc(c);
    uart_putc('\n');
    
    while(1);
    return 0;
}
