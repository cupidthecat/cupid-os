#include "serial.h"
#include "../kernel/ports.h"
#include "../kernel/kernel.h"
#include "../kernel/string.h"
#include "timer.h"

/* ── UART register helpers ────────────────────────────────────────── */
#define SERIAL_DATA(base)        (base)
#define SERIAL_INT_EN(base)      ((uint16_t)(base + 1))
#define SERIAL_FIFO_CTRL(base)   ((uint16_t)(base + 2))
#define SERIAL_LINE_CTRL(base)   ((uint16_t)(base + 3))
#define SERIAL_MODEM_CTRL(base)  ((uint16_t)(base + 4))
#define SERIAL_LINE_STATUS(base) ((uint16_t)(base + 5))

/* ── State ────────────────────────────────────────────────────────── */
static log_level_t current_log_level = LOG_DEBUG;

/* In-memory circular log buffer */
static char   log_buffer[LOG_BUFFER_LINES][LOG_LINE_MAX];
static uint32_t log_write_idx  = 0;   /* next slot to write              */
static uint32_t log_stored     = 0;   /* how many lines stored (≤ MAX)   */

/* ── Init ─────────────────────────────────────────────────────────── */
void serial_init(void) {
    /* Disable interrupts */
    outb(SERIAL_INT_EN(SERIAL_COM1), 0x00);

    /* Enable DLAB – set baud rate divisor */
    outb(SERIAL_LINE_CTRL(SERIAL_COM1), 0x80);
    outb(SERIAL_DATA(SERIAL_COM1),      0x01);     /* divisor low  = 1 (115200) */
    outb(SERIAL_INT_EN(SERIAL_COM1),    0x00);     /* divisor high = 0          */

    /* 8N1, DLAB off */
    outb(SERIAL_LINE_CTRL(SERIAL_COM1), 0x03);

    /* Enable FIFO, clear, 14-byte threshold */
    outb(SERIAL_FIFO_CTRL(SERIAL_COM1), 0xC7);

    /* RTS + DTR */
    outb(SERIAL_MODEM_CTRL(SERIAL_COM1), 0x03);
}

/* ── Low-level write ──────────────────────────────────────────────── */
static int serial_transmit_ready(void) {
    return inb(SERIAL_LINE_STATUS(SERIAL_COM1)) & 0x20;
}

void serial_write_char(char c) {
    while (!serial_transmit_ready()) { /* spin */ }
    if (c == '\n') {
        outb(SERIAL_DATA(SERIAL_COM1), '\r');
        while (!serial_transmit_ready()) { /* spin */ }
    }
    outb(SERIAL_DATA(SERIAL_COM1), (uint8_t)c);
}

void serial_write_string(const char *str) {
    while (*str) {
        serial_write_char(*str);
        str++;
    }
}

/* ── Numeric helpers ──────────────────────────────────────────────── */
static void serial_print_dec(uint32_t num) {
    char buf[12];
    int i = 0;
    if (num == 0) { serial_write_char('0'); return; }
    while (num > 0) { buf[i++] = (char)('0' + (num % 10)); num /= 10; }
    while (i > 0) { serial_write_char(buf[--i]); }
}

static void serial_print_hex(uint32_t num) {
    const char hex[] = "0123456789abcdef";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[num & 0xF];
        num >>= 4;
    }
    buf[10] = '\0';
    serial_write_string(buf);
}

/* ── serial_printf ────────────────────────────────────────────────── */
static void vserial_printf(const char *fmt, __builtin_va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { serial_write_char(*fmt); fmt++; continue; }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            serial_write_string(s ? s : "(null)");
            break;
        }
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            if (v < 0) { serial_write_char('-'); v = -v; }
            serial_print_dec((uint32_t)v);
            break;
        }
        case 'u': serial_print_dec(__builtin_va_arg(ap, uint32_t)); break;
        case 'x': serial_print_hex(__builtin_va_arg(ap, uint32_t)); break;
        case 'p': serial_print_hex((uint32_t)__builtin_va_arg(ap, void *)); break;
        case 'c': serial_write_char((char)__builtin_va_arg(ap, int));       break;
        case '%': serial_write_char('%'); break;
        default:
            serial_write_char('%');
            serial_write_char(*fmt);
            break;
        }
        if (*fmt) fmt++;
    }
}

void serial_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vserial_printf(fmt, ap);
    __builtin_va_end(ap);
}

/* ── In-memory log buffer helpers ─────────────────────────────────── */

/* Append a line to the circular log buffer.  line should NOT contain '\n'. */
static void log_buffer_append(const char *line) {
    size_t len = strlen(line);
    if (len >= LOG_LINE_MAX) len = LOG_LINE_MAX - 1;
    memcpy(log_buffer[log_write_idx], line, len);
    log_buffer[log_write_idx][len] = '\0';

    log_write_idx = (log_write_idx + 1) % LOG_BUFFER_LINES;
    if (log_stored < LOG_BUFFER_LINES) log_stored++;
}

void print_log_buffer(void) {
    if (log_stored == 0) {
        print("(no log entries)\n");
        return;
    }
    /* oldest entry index */
    uint32_t start;
    if (log_stored < LOG_BUFFER_LINES) {
        start = 0;
    } else {
        start = log_write_idx;          /* oldest was overwritten here */
    }
    for (uint32_t i = 0; i < log_stored; i++) {
        uint32_t idx = (start + i) % LOG_BUFFER_LINES;
        print(log_buffer[idx]);
        print("\n");
    }
}

/* ── Logging API ──────────────────────────────────────────────────── */
void set_log_level(log_level_t level) { current_log_level = level; }

const char *get_log_level_name(void) {
    switch (current_log_level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    case LOG_PANIC: return "PANIC";
    default:        return "UNKNOWN";
    }
}

void klog(log_level_t level, const char *fmt, ...) {
    if (level < current_log_level) return;

    /* Build line into a local buffer for the in-memory log */
    char line[LOG_LINE_MAX];
    int  pos = 0;

    /* ── timestamp ── */
    uint32_t ms      = timer_get_uptime_ms();
    uint32_t seconds = ms / 1000;
    uint32_t millis  = ms % 1000;

    /* Write timestamp to serial and to line buffer */
    serial_write_char('[');
    line[pos++] = '[';

    /* seconds */
    {
        char tmp[12]; int t = 0;
        uint32_t s = seconds;
        if (s == 0) { tmp[t++] = '0'; }
        else { while (s > 0) { tmp[t++] = (char)('0' + (s % 10)); s /= 10; } }
        while (t > 0) {
            char ch = tmp[--t];
            serial_write_char(ch);
            if (pos < LOG_LINE_MAX - 1) line[pos++] = ch;
        }
    }
    serial_write_char('.');
    if (pos < LOG_LINE_MAX - 1) line[pos++] = '.';

    /* zero-padded millis */
    {
        char d0 = (char)('0' + (millis / 100));
        char d1 = (char)('0' + ((millis / 10) % 10));
        char d2 = (char)('0' + (millis % 10));
        serial_write_char(d0); if (pos < LOG_LINE_MAX-1) line[pos++] = d0;
        serial_write_char(d1); if (pos < LOG_LINE_MAX-1) line[pos++] = d1;
        serial_write_char(d2); if (pos < LOG_LINE_MAX-1) line[pos++] = d2;
    }
    serial_write_string("] ");
    if (pos < LOG_LINE_MAX-1) line[pos++] = ']';
    if (pos < LOG_LINE_MAX-1) line[pos++] = ' ';

    /* ── user message ── */
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            serial_write_char(*fmt);
            if (pos < LOG_LINE_MAX - 1) line[pos++] = *fmt;
            fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            serial_write_string(s);
            while (*s && pos < LOG_LINE_MAX - 1) { line[pos++] = *s; s++; }
            break;
        }
        case 'd': {
            int v = __builtin_va_arg(ap, int);
            char tmp[12]; int t = 0;
            if (v < 0) {
                serial_write_char('-');
                if (pos < LOG_LINE_MAX-1) line[pos++] = '-';
                v = -v;
            }
            uint32_t uv = (uint32_t)v;
            if (uv == 0) { tmp[t++] = '0'; }
            else { while (uv > 0) { tmp[t++] = (char)('0' + (uv % 10)); uv /= 10; } }
            while (t > 0) {
                char ch = tmp[--t];
                serial_write_char(ch);
                if (pos < LOG_LINE_MAX-1) line[pos++] = ch;
            }
            break;
        }
        case 'u': {
            uint32_t uv = __builtin_va_arg(ap, uint32_t);
            char tmp[12]; int t = 0;
            if (uv == 0) { tmp[t++] = '0'; }
            else { while (uv > 0) { tmp[t++] = (char)('0' + (uv % 10)); uv /= 10; } }
            while (t > 0) {
                char ch = tmp[--t];
                serial_write_char(ch);
                if (pos < LOG_LINE_MAX-1) line[pos++] = ch;
            }
            break;
        }
        case 'x': {
            uint32_t xv = __builtin_va_arg(ap, uint32_t);
            const char hex[] = "0123456789abcdef";
            char buf2[11]; buf2[0]='0'; buf2[1]='x';
            for (int i2 = 9; i2 >= 2; i2--) { buf2[i2] = hex[xv & 0xF]; xv >>= 4; }
            buf2[10] = '\0';
            serial_write_string(buf2);
            for (int i2 = 0; buf2[i2] && pos < LOG_LINE_MAX-1; i2++) line[pos++] = buf2[i2];
            break;
        }
        case 'p': {
            uint32_t pv = (uint32_t)__builtin_va_arg(ap, void *);
            const char hex[] = "0123456789abcdef";
            char buf2[11]; buf2[0]='0'; buf2[1]='x';
            for (int i2 = 9; i2 >= 2; i2--) { buf2[i2] = hex[pv & 0xF]; pv >>= 4; }
            buf2[10] = '\0';
            serial_write_string(buf2);
            for (int i2 = 0; buf2[i2] && pos < LOG_LINE_MAX-1; i2++) line[pos++] = buf2[i2];
            break;
        }
        case 'c': {
            char cv = (char)__builtin_va_arg(ap, int);
            serial_write_char(cv);
            if (pos < LOG_LINE_MAX-1) line[pos++] = cv;
            break;
        }
        case '%':
            serial_write_char('%');
            if (pos < LOG_LINE_MAX-1) line[pos++] = '%';
            break;
        default:
            serial_write_char('%');
            serial_write_char(*fmt);
            if (pos < LOG_LINE_MAX-1) line[pos++] = '%';
            if (pos < LOG_LINE_MAX-1) line[pos++] = *fmt;
            break;
        }
        if (*fmt) fmt++;
    }
    __builtin_va_end(ap);

    serial_write_char('\n');
    line[pos] = '\0';
    log_buffer_append(line);
}
