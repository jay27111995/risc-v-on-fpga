// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');
    
    // Wait for RX_READY
    while (!RX_READY);
    
    // Read single char
    char c = RX_BUF & 0xFF;
    
    // Clear
    RX_READY = 0;
    
    // Echo
    uart_putc(c);
    uart_putc('\n');
    
    while(1);
    return 0;
}
