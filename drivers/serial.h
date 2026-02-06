#ifndef SERIAL_H
#define SERIAL_H

#include "../kernel/types.h"

/* COM1 base port */
#define SERIAL_COM1 0x3F8

/* Log levels */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_PANIC = 4
} log_level_t;

/* In-memory log buffer settings */
#define LOG_LINE_MAX   120
#define LOG_BUFFER_LINES 100

/* Initialize serial port (COM1, 115200 baud, 8N1) */
void serial_init(void);

/* Write single character to serial */
void serial_write_char(char c);

/* Write null-terminated string to serial */
void serial_write_string(const char *str);

/* Formatted serial output (supports %s, %d, %u, %x, %p, %c, %%) */
void serial_printf(const char *fmt, ...);

/* Timestamped log at given level */
void klog(log_level_t level, const char *fmt, ...);

/* Set minimum log level */
void set_log_level(log_level_t level);

/* Get current log level name */
const char *get_log_level_name(void);

/* Dump in-memory log buffer to VGA */
void print_log_buffer(void);

/* Convenience macros */
#define KDEBUG(fmt, ...) klog(LOG_DEBUG, "[DEBUG] " fmt, ##__VA_ARGS__)
#define KINFO(fmt, ...)  klog(LOG_INFO,  "[INFO]  " fmt, ##__VA_ARGS__)
#define KWARN(fmt, ...)  klog(LOG_WARN,  "[WARN]  " fmt, ##__VA_ARGS__)
#define KERROR(fmt, ...) klog(LOG_ERROR, "[ERROR] " fmt, ##__VA_ARGS__)

#endif
