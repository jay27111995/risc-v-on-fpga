// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');  // Prompt
    
    // Wait for RX_READY
    while (!RX_READY);
    
    // Print RX_LEN as hex
    unsigned int len = RX_LEN;
    uart_putc('L');
    uart_putc('=');
    uart_putc('0' + ((len >> 4) & 0xF));
    uart_putc('0' + (len & 0xF));
    uart_putc('\n');
    
    // Just read first char
    char c = RX_BUF & 0xFF;
    uart_putc(c);
    uart_putc('\n');
    
    // Clear
    RX_READY = 0;
    RX_LEN = 0;
    
    while(1);
    return 0;
}
