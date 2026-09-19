// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Debug: write markers to DMEM to trace execution
    volatile unsigned int *debug = (volatile unsigned int *)0x500;
    debug[0] = 0xAAAA0001;  // Marker: entered main
    
    // Send "Hi" first as simple test
    uart_putc('H');
    debug[0] = 0xAAAA0002;  // Marker: after H
    
    uart_putc('i');
    debug[0] = 0xAAAA0003;  // Marker: after i
    
    uart_putc('\n');
    debug[0] = 0xAAAA0004;  // Marker: after newline
    
    // Now check RX_READY value
    debug[1] = RX_READY;    // What does CPU see for RX_READY?
    debug[2] = RX_LEN;      // What does CPU see for RX_LEN?
    debug[3] = RX_BUFFER[0]; // First byte of RX buffer
    
    debug[0] = 0xAAAA0005;  // Marker: checked RX state
    
    // Try to read one char (will block if RX_READY is 0)
    debug[0] = 0xAAAA0006;  // Marker: about to call uart_getc
    char c = uart_getc();
    debug[4] = c;           // What did we get?
    debug[0] = 0xAAAA0007;  // Marker: after uart_getc
    
    // Echo it back
    uart_putc(c);
    debug[0] = 0xAAAA0008;  // Marker: done
    
    while (1);
    return 0;
}
