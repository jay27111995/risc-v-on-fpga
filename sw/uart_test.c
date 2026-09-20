// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('?');  // Prompt
    
    // Wait for RX_READY
    while (!RX_READY);
    
    // Get length and echo all chars
    volatile int len = RX_LEN;
    volatile unsigned int *rx_buf = (volatile unsigned int *)0x300;
    for (volatile int i = 0; i < len; i++) {
        uart_putc(rx_buf[i] & 0xFF);
    }
    uart_putc('\n');
    
    // Clear
    RX_READY = 0;
    RX_LEN = 0;
    
    while(1);
    return 0;
}
