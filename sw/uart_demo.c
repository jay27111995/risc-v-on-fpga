// UART Demo - printf/scanf style functions
#include "uart.h"

int main(void) {
    uart_puts("UART Demo\n");
    uart_puts("---------\n");
    
    // Demo print functions
    uart_puts("Int: ");
    uart_print_int(-42);
    uart_putc('\n');
    
    uart_puts("Uint: ");
    uart_print_uint(12345);
    uart_putc('\n');
    
    uart_puts("Hex: ");
    uart_print_hex(0xDEADBEEF);
    uart_putc('\n');
    
    // Demo input
    uart_puts("\nEnter a number (end with ;): ");
    int num = uart_read_int();
    uart_puts("You entered: ");
    uart_print_int(num);
    uart_puts("\nDouble: ");
    uart_print_int(num * 2);
    uart_putc('\n');
    
    uart_puts("\nDone!\n");
    
    while(1);
    return 0;
}
