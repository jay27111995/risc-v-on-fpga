// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

int main(void) {
    uart_puts("Hello from RISC-V!\n");
    uart_puts("Type something and press Enter:\n");

    char buf[64];
    while (1) {
        int len = uart_read(buf, sizeof(buf) - 1);
        buf[len] = '\0';

        uart_puts("You said: ");
        uart_puts(buf);
        uart_puts("\n");

        // Echo the length too
        uart_puts("Length: ");
        uart_put_int(len);
        uart_puts(" bytes\n");
    }

    return 0;
}
