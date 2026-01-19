#include "serial.h"
#include "../kernel/ports.h"

// Current serial port being used
static uint16_t current_port = 0;

// Initialize serial port
void serial_init(uint16_t port) {
    current_port = port;

    // Disable interrupts
    outb(port + SERIAL_INTERRUPT_EN, 0x00);

    // Enable DLAB (Divisor Latch Access Bit) to set baud rate
    outb(port + SERIAL_LINE_CONTROL, SERIAL_LCR_DLAB);

    // Set baud rate to 115200 (divisor = 1)
    outb(port + SERIAL_DATA_REG, 0x01);        // Low byte
    outb(port + SERIAL_INTERRUPT_EN, 0x00);    // High byte

    // Disable DLAB and set 8-bit word length, no parity, 1 stop bit
    outb(port + SERIAL_LINE_CONTROL, SERIAL_LCR_8BIT_WORD);

    // Enable FIFO, clear FIFO queues, 14-byte threshold
    outb(port + SERIAL_INTERRUPT_ID, 0xC7);

    // Enable RTS/CTS, set DTR
    outb(port + SERIAL_MODEM_CONTROL, SERIAL_MCR_DTR | SERIAL_MCR_RTS | SERIAL_MCR_OUT2);
}

// Check if transmit buffer is empty
int serial_is_transmit_empty(uint16_t port) {
    return inb(port + SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY;
}

// Write a character to serial port
void serial_write_char(char c) {
    if (current_port == 0) return;  // Not initialized

    // Wait for transmit buffer to be empty
    while (!serial_is_transmit_empty(current_port)) {
        // Busy wait
    }

    // Write character
    outb(current_port + SERIAL_DATA_REG, c);
}

// Read a character from serial port (non-blocking)
int serial_read_char(void) {
    if (current_port == 0) return -1;  // Not initialized

    // Check if data is ready
    if (inb(current_port + SERIAL_LINE_STATUS) & SERIAL_LSR_DATA_READY) {
        return inb(current_port + SERIAL_DATA_REG);
    }

    return -1;  // No data available
}
