// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_putc('H');
    uart_putc('i');
    uart_putc('\n');
    
    // Wait for input
    while (!RX_READY);
    
    uart_putc('G');
    uart_putc('o');
    uart_putc('t');
    uart_putc('\n');
    
    // Read RX_LEN - just print low nibble
    unsigned int len = RX_LEN;
    int n = len & 0xF;
    uart_putc(n < 10 ? '0' + n : 'A' + n - 10);
    uart_putc('\n');
    
    // Read RX_BUFFER word at 0x300
    volatile unsigned int *dmem = (volatile unsigned int *)0;
    unsigned int word = dmem[0x300 / 4];  // Address 0x300 / 4 = word index 0xC0
    
    // Print first byte
    char c = word & 0xFF;
    uart_putc('[');
    uart_putc(c);
    uart_putc(']');
    uart_putc('\n');
    
    RX_READY = 0;
    
    while (1);
    return 0;
}
