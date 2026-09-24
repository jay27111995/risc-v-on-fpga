#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    printf("Factorial Calculator\n");
    printf("Enter a number: ");
    
    int n;
    scanf("%d", &n);
    
    int result = factorial(n);
    printf("\n%d! = %d\n", n, result);
    
    return 0;
}
