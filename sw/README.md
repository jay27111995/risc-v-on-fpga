# Software - RISC-V Programs

## Structure

```
sw/
├── build.sh        # Build script
├── build/          # Output (*.elf, *.dis)
├── lib/            # C library headers
│   ├── stdio.h     # printf, scanf
│   ├── stdlib.h    # atoi, abs
│   ├── string.h    # strlen, memcpy
│   ├── stdint.h    # uint32_t, etc.
│   └── uart.h      # Low-level UART
├── src/            # Startup code
│   ├── start.S     # Entry point, BSS clear
│   └── link.ld     # Linker script
└── examples/       # Example programs
```

## Building

```bash
./build.sh examples/hello_sum.c
```

Output: `build/hello_sum.elf`

## Example Programs

| Program | Description |
|---------|-------------|
| `hello_sum.c` | Interactive sum with printf/scanf |
| `factorial.c` | Factorial calculation |
| `sort_test.c` | Quicksort + insertion sort |
| `tree_test.c` | Binary search tree |
| `ecall_test.c` | ECALL trap handling test |
| `timer_int_test.c` | Timer interrupt test |
| `wfi_test.c` | Wait-for-interrupt test |
| `fence_test.c` | FENCE/FENCE.I test |
| `csr_test.c` | CSR read/write tests |

## Writing Programs

```c
#include <stdio.h>

int main(void) {
    printf("Hello, RISC-V!\n");
    
    int n;
    printf("Enter a number: ");
    scanf("%d", &n);
    printf("You entered: %d\n", n);
    
    return 0;
}
```

## Available Functions

### stdio.h
- `printf(fmt, ...)` - %d, %u, %s, %c, %x
- `scanf(fmt, ...)` - %d
- `putchar(c)`, `getchar()`
- `puts(s)`, `print(s)`

### stdlib.h
- `atoi(s)`, `itoa(n, buf, base)`
- `abs(n)`
- `malloc()` - stubbed (no heap)

### string.h
- `strlen()`, `strcpy()`, `strcmp()`
- `memcpy()`, `memset()`, `memcmp()`

### uart.h
- `kbhit()` - non-blocking key check
- `getchar_nb()` - non-blocking read

## Limitations

- No heap (malloc returns NULL)
- No floating point
- No file I/O (fopen returns NULL)
- ~128KB code, ~32KB data max
