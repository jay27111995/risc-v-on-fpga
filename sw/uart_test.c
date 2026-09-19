// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Send greeting
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Echo loop - receive char, send it back
    while (1) {
        // Wait for RX_READY
        while (!RX_READY);
        
        // Read the character
        char c = RX_BUFFER[0];
        int len = RX_LEN;
        RX_READY = 0;  // Acknowledge
        
        // Echo back all received characters
        for (int i = 0; i < len; i++) {
            uart_putc(RX_BUFFER[i]);
        }
        uart_putc('\n');
    }

    return 0;
}
