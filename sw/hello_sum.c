#include "uart.h"

// Print string from inline chars (no .rodata)
void print_str(const char *s) {
    while (*s) putchar(*s++);
}

int main() {
    int a, b;
    
    // Hello World - inline
    putchar('H'); putchar('e'); putchar('l'); putchar('l'); putchar('o');
    putchar(','); putchar(' ');
    putchar('R'); putchar('I'); putchar('S'); putchar('C'); putchar('-'); putchar('V');
    putchar('!'); putchar('\n');
    
    // Prompt
    putchar('a'); putchar('=');
    a = read_int();
    
    putchar('\n'); putchar('b'); putchar('=');
    b = read_int();
    
    // Result
    putchar('\n');
    print_int(a);
    putchar('+');
    print_int(b);
    putchar('=');
    print_int(a + b);
    putchar('\n');
    
    return 0;
}
