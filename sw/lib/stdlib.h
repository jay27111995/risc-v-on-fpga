#ifndef STDLIB_H
#define STDLIB_H

#include "stddef.h"

// String to integer
static int atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

static long atol(const char *s) {
    return atoi(s);
}

// Integer to string
static char *itoa(int n, char *buf, int base) {
    char *p = buf;
    char *q = buf;
    int neg = 0;
    char tmp;
    
    if (n == 0) { *p++ = '0'; *p = '\0'; return buf; }
    if (n < 0 && base == 10) { neg = 1; n = -n; }
    
    while (n) {
        int d = n % base;
        *p++ = d < 10 ? '0' + d : 'a' + d - 10;
        n /= base;
    }
    if (neg) *p++ = '-';
    *p-- = '\0';
    
    while (q < p) { tmp = *q; *q++ = *p; *p-- = tmp; }
    return buf;
}

// Absolute value
static int abs(int n) {
    return n < 0 ? -n : n;
}

static long labs(long n) {
    return n < 0 ? -n : n;
}

// Division result
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;

static div_t div(int num, int denom) {
    div_t r = { num / denom, num % denom };
    return r;
}

static ldiv_t ldiv(long num, long denom) {
    ldiv_t r = { num / denom, num % denom };
    return r;
}

// === Stubbed (no heap) ===

static void *malloc(unsigned int size) {
    (void)size;
    return (void*)0;  // No heap
}

static void *calloc(unsigned int n, unsigned int size) {
    (void)n; (void)size;
    return (void*)0;
}

static void *realloc(void *ptr, unsigned int size) {
    (void)ptr; (void)size;
    return (void*)0;
}

static void free(void *ptr) {
    (void)ptr;
}

// Exit (infinite loop)
static void exit(int status) {
    (void)status;
    while (1);
}

static void abort(void) {
    while (1);
}

#endif
