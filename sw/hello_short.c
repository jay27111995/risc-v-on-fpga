#include "uart.h"

int main() {
    // Inline characters to avoid .rodata section
    putchar('H');
    putchar('i');
    putchar('!');
    return 0;
}
