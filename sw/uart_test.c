// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_puts("Hello! Type something:\n");
    
    // Wait for input
    while (!uart_rx_ready());
    
    // Read and echo
    int len = RX_LEN;
    uart_puts("You typed: ");
    volatile unsigned int *rx_buf = (volatile unsigned int *)0x300;
    for (int i = 0; i < len; i++) {
        uart_putc(rx_buf[i] & 0xFF);
    }
    uart_putc('\n');
    
    // Clear RX
    RX_READY = 0;
    RX_LEN = 0;
    
    uart_puts("Done!\n");
    
    while(1);
    return 0;
}
