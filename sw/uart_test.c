// UART Test Program
// Sends "Hello from RISC-V!" and echoes back any input

#include "uart.h"

// Helper to send a string using the buffer (more efficient than char-by-char)
void print_str(const char *chars, int len) {
    while (TX_READY);  // Wait for host to consume previous
    for (int i = 0; i < len; i++) {
        TX_BUFFER[i] = chars[i];
    }
    TX_LEN = len;
    TX_READY = 1;
}

int main(void) {
    // Send "Hello from RISC-V!\n" as a buffer
    // Can't use string literals (they're in IMEM, loads read DMEM)
    // So we build it character by character
    char msg[20];
    msg[0] = 'H';
    msg[1] = 'e';
    msg[2] = 'l';
    msg[3] = 'l';
    msg[4] = 'o';
    msg[5] = ' ';
    msg[6] = 'f';
    msg[7] = 'r';
    msg[8] = 'o';
    msg[9] = 'm';
    msg[10] = ' ';
    msg[11] = 'R';
    msg[12] = 'I';
    msg[13] = 'S';
    msg[14] = 'C';
    msg[15] = '-';
    msg[16] = 'V';
    msg[17] = '!';
    msg[18] = '\n';
    print_str(msg, 19);
    
    // Echo loop - receive and echo back
    while (1) {
        char buf[64];
        int len = uart_read(buf, 64);
        
        // Echo back what we received
        uart_write(buf, len);
    }

    return 0;
}
