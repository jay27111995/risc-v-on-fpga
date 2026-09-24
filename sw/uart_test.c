// UART Test Program
// Sends "Hi!" and echoes back any input

#include "uart.h"

int main(void) {
    // Greeting
    puts("Hi!");
    
    // Simple echo loop
    while (1) {
        char c = getchar();
        putchar(c);
    }
    
    return 0;
}
