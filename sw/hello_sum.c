#include "uart.h"

int main() {
    int a, b;
    
    // Hello World
    puts("Hello, RISC-V World!");
    puts("");
    
    // Sum of two numbers using scanf
    printf("Enter two numbers: ");
    scanf("%d %d", &a, &b);
    
    int sum = a + b;
    
    printf("\n%d + %d = %d\n", a, b, sum);
    
    puts("\nDone!");
    
    return 0;
}
