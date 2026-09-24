#ifndef UART_H
#define UART_H

// UART circular buffer addresses
#define DMEM_BASE     0x10000000
#define TX_BUF    ((volatile int*)(DMEM_BASE + 0x100))
#define TX_HEAD   ((volatile int*)(DMEM_BASE + 0x140))
#define TX_TAIL   ((volatile int*)(DMEM_BASE + 0x144))
#define RX_BUF    ((volatile int*)(DMEM_BASE + 0x200))
#define RX_HEAD   ((volatile int*)(DMEM_BASE + 0x240))
#define RX_TAIL   ((volatile int*)(DMEM_BASE + 0x244))

static inline void putchar(char c) {
    int head = *TX_HEAD;
    int next = (head + 1) & 0xF;
    while (next == *TX_TAIL);
    TX_BUF[head] = (int)c;
    *TX_HEAD = next;
}

static inline char getchar(void) {
    int tail = *RX_TAIL;
    while (tail == *RX_HEAD);
    char c = (char)RX_BUF[tail];
    *RX_TAIL = (tail + 1) & 0xF;
    return c;
}

static inline void print(const char *s) {
    while (*s) putchar(*s++);
}

static inline void puts(const char *s) {
    print(s);
    putchar('\n');
}

static void print_int(int n) {
    char buf[12];
    int i = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) putchar('-');
    while (i > 0) putchar(buf[--i]);
}

static void print_hex(unsigned int n) {
    const char hex[] = "0123456789ABCDEF";
    putchar('0'); putchar('x');
    for (int i = 28; i >= 0; i -= 4) putchar(hex[(n >> i) & 0xF]);
}

static void printf(const char *fmt, ...) {
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
}

// Read integer - waits for Enter key
static int read_int(void) {
    int n = 0;
    int neg = 0;
    char c;
    
    // Skip leading whitespace
    do { c = getchar(); } while (c == ' ' || c == '\t');
    
    // Check sign
    if (c == '-') { neg = 1; c = getchar(); }
    else if (c == '+') { c = getchar(); }
    
    // Read digits until Enter or space
    while (c >= '0' && c <= '9') {
        n = n * 10 + (c - '0');
        c = getchar();
    }
    
    return neg ? -n : n;
}

// scanf - supports %d
static int scanf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int count = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'd' || *fmt == 'i') {
                int *p = __builtin_va_arg(args, int*);
                *p = read_int();
                count++;
            }
        }
        fmt++;
    }
    __builtin_va_end(args);
    return count;
}

#endif
