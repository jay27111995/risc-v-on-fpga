#include <stdio.h>

int main(void) {
    printf("Sum Calculator\n");
    printf("Enter N: ");
    
    int n;
    scanf("%d", &n);
    
    int sum = 0;
    for (int i = 1; i <= n; i++) {
        sum += i;
    }
    
    printf("\nSum(1..%d) = %d\n", n, sum);
    
    return 0;
}
