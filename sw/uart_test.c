// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Send greeting
    uart_putc('H');
    uart_putc('i');
    uart_putc('!');
    uart_putc('\n');
    
    // Echo loop - receive chars, send them back
    while (1) {
        // Wait for RX_READY
        while (!RX_READY);
        
        // Get length and acknowledge
        int len = RX_LEN;
        RX_READY = 0;
        
        // Echo back all received characters (read word by word)
        for (int i = 0; i < len; i++) {
            // Read word-aligned, extract byte
            volatile unsigned int *rx_words = (volatile unsigned int *)0x300;
            unsigned int word = rx_words[i / 4];
            char c = (word >> ((i % 4) * 8)) & 0xFF;
            uart_putc(c);
        }
        uart_putc('\n');
    }

    return 0;
}
