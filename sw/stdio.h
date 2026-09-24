#ifndef STDIO_H
#define STDIO_H

// RISC-V SoC stdio implementation
// Supports UART I/O, stubs unsupported file operations

#include "uart.h"

// Standard constants
#define EOF (-1)
#define NULL ((void*)0)

// File type (stub)
typedef struct { int fd; } FILE;

// Standard streams (all map to UART)
static FILE _stdin_file = {0};
static FILE _stdout_file = {1};
static FILE _stderr_file = {2};
#define stdin  (&_stdin_file)
#define stdout (&_stdout_file)
#define stderr (&_stderr_file)

// === Supported Functions ===

// printf/scanf from uart.h are already available

// fprintf - only works on stdout/stderr
static int fprintf(FILE *f, const char *fmt, ...) {
    (void)f;  // Ignore file, always use UART
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': case 'i': print_int(__builtin_va_arg(args, int)); break;
                case 'x': case 'X': print_hex(__builtin_va_arg(args, unsigned int)); break;
                case 's': print(__builtin_va_arg(args, char*)); break;
                case 'c': putchar(__builtin_va_arg(args, int)); break;
                case '%': putchar('%'); break;
                default: putchar('%'); putchar(*fmt);
            }
        } else {
            putchar(*fmt);
        }
        fmt++;
    }
    __builtin_va_end(args);
    return 0;
}

// fputs
static int fputs(const char *s, FILE *f) {
    (void)f;
    print(s);
    return 0;
}

// fputc
static int fputc(int c, FILE *f) {
    (void)f;
    putchar(c);
    return c;
}

// fgetc
static int fgetc(FILE *f) {
    (void)f;
    return getchar();
}

// fgets
static char *fgets(char *buf, int n, FILE *f) {
    (void)f;
    int i = 0;
    while (i < n - 1) {
        char c = getchar();
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

// fflush (no-op, UART is unbuffered)
static int fflush(FILE *f) {
    (void)f;
    return 0;
}

// === Stubbed Functions (unsupported - no filesystem) ===

static FILE *fopen(const char *path, const char *mode) {
    (void)path; (void)mode;
    return NULL;  // Always fails
}

static int fclose(FILE *f) {
    (void)f;
    return EOF;
}

static int fread(void *ptr, int size, int n, FILE *f) {
    (void)ptr; (void)size; (void)n; (void)f;
    return 0;
}

static int fwrite(const void *ptr, int size, int n, FILE *f) {
    (void)ptr; (void)size; (void)n; (void)f;
    return 0;
}

static int fseek(FILE *f, long offset, int whence) {
    (void)f; (void)offset; (void)whence;
    return -1;
}

static long ftell(FILE *f) {
    (void)f;
    return -1;
}

static int feof(FILE *f) {
    (void)f;
    return 0;
}

static int ferror(FILE *f) {
    (void)f;
    return 0;
}

static void clearerr(FILE *f) {
    (void)f;
}

static int remove(const char *path) {
    (void)path;
    return -1;
}

static int rename(const char *old, const char *new) {
    (void)old; (void)new;
    return -1;
}

#endif
