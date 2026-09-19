// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('H');
    uart_putc('i');
    uart_putc('\n');
    
    // EXPLICITLY clear RX_READY from CPU side
    RX_READY = 0;
    
    // Small delay
    for (volatile int i = 0; i < 10000; i++);
    
    // Check what we see now
    uart_putc('R');
    uart_putc('=');
    uart_putc('0' + (RX_READY & 0xF));
    uart_putc('\n');
    
    // Now wait for input
    while (!RX_READY);
    
    uart_putc('G');
    uart_putc('o');
    uart_putc('t');
    uart_putc('\n');
    
    // Read length and data
    unsigned int len = RX_LEN;
    uart_putc('L');
    uart_putc('=');
    uart_putc('0' + (len & 0xF));
    uart_putc('\n');
    
    // Read first byte
    volatile unsigned int *dmem = (volatile unsigned int *)0;
    unsigned int word = dmem[0x300 / 4];
    char c = word & 0xFF;
    uart_putc('[');
    uart_putc(c);
    uart_putc(']');
    uart_putc('\n');
    
    RX_READY = 0;
    
    while (1);
    return 0;
}
