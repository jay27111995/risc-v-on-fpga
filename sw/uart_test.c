// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    // Debug: write markers to DMEM to trace execution
    volatile unsigned int *debug = (volatile unsigned int *)0x500;
    debug[0] = 0xAAAA0001;  // Marker: entered main
    
    // Send "Hi" first as simple test
    uart_putc('H');
    uart_putc('i');
    uart_putc('\n');
    debug[0] = 0xAAAA0002;  // Marker: after Hi
    
    // Record what we see BEFORE waiting
    debug[1] = RX_READY;    // What does CPU see for RX_READY?
    debug[2] = RX_LEN;      // What does CPU see for RX_LEN?
    debug[3] = 0xBBBB;      // Marker: about to block
    
    // Block waiting for input
    while (!RX_READY) {
        // Spin - do nothing
    }
    
    debug[3] = 0xCCCC;      // Marker: unblocked!
    debug[4] = RX_READY;    // What's RX_READY now?
    debug[5] = RX_LEN;      // What's RX_LEN now?
    debug[6] = RX_BUFFER[0]; // First byte
    
    // Echo it
    uart_putc(RX_BUFFER[0]);
    uart_putc('\n');
    RX_READY = 0;
    
    debug[0] = 0xAAAA0008;  // Marker: done
    
    while (1);
    return 0;
}
