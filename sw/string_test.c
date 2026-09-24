#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    char buf[32];
    
    printf("String Test\n");
    
    // strlen
    const char *hello = "Hello";
    printf("strlen(\"%s\") = %d\n", hello, strlen(hello));
    
    // strcpy
    strcpy(buf, "World");
    printf("strcpy: %s\n", buf);
    
    // strcat
    strcat(buf, "!");
    printf("strcat: %s\n", buf);
    
    // strcmp
    printf("strcmp(\"abc\", \"abd\") = %d\n", strcmp("abc", "abd"));
    
    // atoi
    printf("atoi(\"123\") = %d\n", atoi("123"));
    
    // itoa
    char num[16];
    itoa(42, num, 10);
    printf("itoa(42) = %s\n", num);
    
    printf("\nDone!\n");
    return 0;
}
