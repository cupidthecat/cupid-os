#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

// Serial port constants
#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8
#define SERIAL_COM3 0x3E8
#define SERIAL_COM4 0x2E8

// Serial port registers (relative to base port)
#define SERIAL_DATA_REG         0  // Data register (R/W)
#define SERIAL_INTERRUPT_EN     1  // Interrupt enable register
#define SERIAL_INTERRUPT_ID     2  // Interrupt ID register (R/O)
#define SERIAL_LINE_CONTROL     3  // Line control register
#define SERIAL_MODEM_CONTROL    4  // Modem control register
#define SERIAL_LINE_STATUS      5  // Line status register (R/O)
#define SERIAL_MODEM_STATUS     6  // Modem status register (R/O)
#define SERIAL_SCRATCH          7  // Scratch register

// Line control register bits
#define SERIAL_LCR_DLAB         0x80  // Divisor latch access bit
#define SERIAL_LCR_8BIT_WORD    0x03  // 8-bit word length

// Line status register bits
#define SERIAL_LSR_DATA_READY   0x01  // Data ready
#define SERIAL_LSR_TX_EMPTY     0x20  // Transmitter holding register empty

// Modem control register bits
#define SERIAL_MCR_DTR          0x01  // Data terminal ready
#define SERIAL_MCR_RTS          0x02  // Request to send
#define SERIAL_MCR_OUT1         0x04  // OUT1
#define SERIAL_MCR_OUT2         0x08  // OUT2
#define SERIAL_MCR_LOOPBACK     0x10  // Loopback mode

// Function declarations
void serial_init(uint16_t port);
void serial_write_char(char c);
int serial_read_char(void);
int serial_is_transmit_empty(uint16_t port);

#endif
