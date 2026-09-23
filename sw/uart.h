#ifndef UART_H
#define UART_H

// UART circular buffer addresses
// DMEM is mapped at 0x10000000 to avoid linker overlap with IMEM
// The hardware only uses lower bits, so 0x10000xxx maps to DMEM[xxx]
#define DMEM_BASE     0x10000000
#define TX_BUF    ((volatile int*)(DMEM_BASE + 0x100))
#define TX_HEAD   ((volatile int*)(DMEM_BASE + 0x140))
#define TX_TAIL   ((volatile int*)(DMEM_BASE + 0x144))
#define RX_BUF    ((volatile int*)(DMEM_BASE + 0x200))
#define RX_HEAD   ((volatile int*)(DMEM_BASE + 0x240))
#define RX_TAIL   ((volatile int*)(DMEM_BASE + 0x244))

// Send one character
static inline void putchar(char c) {
    int head = *TX_HEAD;
    int next = (head + 1) & 0xF;
    
    // Wait if buffer full
    while (next == *TX_TAIL);
    
    TX_BUF[head] = (int)c;  // Write char as word
    *TX_HEAD = next;
}

// Receive one character
static inline char getchar(void) {
    int tail = *RX_TAIL;
    
    // Wait if buffer empty
    while (tail == *RX_HEAD);
    
    char c = (char)RX_BUF[tail];  // Read word, take low byte
    *RX_TAIL = (tail + 1) & 0xF;
    return c;
}

// Print string
static inline void puts(const char *s) {
    while (*s) putchar(*s++);
    putchar('\n');
}

// Print string without newline
static inline void print(const char *s) {
    while (*s) putchar(*s++);
}

// Print integer
static void print_int(int n) {
    char buf[12];
    int i = 0;
    int neg = 0;
    
    if (n < 0) {
        neg = 1;
        n = -n;
    }
    
    if (n == 0) {
        putchar('0');
        return;
    }
    
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    
    if (neg) putchar('-');
    
    while (i > 0) {
        putchar(buf[--i]);
    }
}

// Print hex
static void print_hex(unsigned int n) {
    const char hex[] = "0123456789ABCDEF";
    putchar('0');
    putchar('x');
    for (int i = 28; i >= 0; i -= 4) {
        putchar(hex[(n >> i) & 0xF]);
    }
}

// Simple printf (supports %d, %x, %s, %c)
static void printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd':
                case 'i':
                    print_int(__builtin_va_arg(args, int));
                    break;
                case 'x':
                case 'X':
                    print_hex(__builtin_va_arg(args, unsigned int));
                    break;
                case 's':
                    print(__builtin_va_arg(args, char*));
                    break;
                case 'c':
                    putchar(__builtin_va_arg(args, int));
                    break;
                case '%':
                    putchar('%');
                    break;
                default:
                    putchar('%');
                    putchar(*fmt);
            }
        } else {
            putchar(*fmt);
        }
        fmt++;
    }
    
    __builtin_va_end(args);
}

// Read integer from input (helper)
static int _read_int_internal(void) {
    int n = 0;
    int neg = 0;
    char c;
    
    // Skip whitespace
    do {
        c = getchar();
        putchar(c);  // Echo
    } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    
    // Check for negative
    if (c == '-') {
        neg = 1;
        c = getchar();
        putchar(c);
    } else if (c == '+') {
        c = getchar();
        putchar(c);
    }
    
    // Read digits
    while (c >= '0' && c <= '9') {
        n = n * 10 + (c - '0');
        c = getchar();
        if (c >= '0' && c <= '9') putchar(c);
    }
    
    return neg ? -n : n;
}

// Read string until whitespace (helper)
static void _read_str_internal(char *buf, int max) {
    char c;
    int i = 0;
    
    // Skip leading whitespace
    do {
        c = getchar();
        putchar(c);
    } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    
    // Read until whitespace
    while (c != ' ' && c != '\n' && c != '\r' && c != '\t' && i < max - 1) {
        buf[i++] = c;
        c = getchar();
        putchar(c);
    }
    buf[i] = '\0';
}

// scanf - supports %d, %c, %s
static int scanf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    int count = 0;
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int *p = __builtin_va_arg(args, int*);
                    *p = _read_int_internal();
                    count++;
                    break;
                }
                case 'c': {
                    char *p = __builtin_va_arg(args, char*);
                    *p = getchar();
                    putchar(*p);
                    count++;
                    break;
                }
                case 's': {
                    char *p = __builtin_va_arg(args, char*);
                    _read_str_internal(p, 256);  // max 256 chars
                    count++;
                    break;
                }
                default:
                    break;
            }
        }
        fmt++;
    }
    
    __builtin_va_end(args);
    return count;
}

// Convenience wrapper
static int read_int(void) {
    return _read_int_internal();
}

#endif
