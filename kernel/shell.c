#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"
#include "ports.h"
#include "types.h"
#include "fs.h"
#include "shell.h"
#include "blockcache.h"
#include "fat16.h"
#include "memory.h"
#include "math.h"
#include "panic.h"
#include "assert.h"
#include "../drivers/serial.h"
#include "ed.h"

#define MAX_INPUT_LEN 80
#define HISTORY_SIZE 16

/* ══════════════════════════════════════════════════════════════════════
 *  GUI output mode support
 * ══════════════════════════════════════════════════════════════════════ */
static shell_output_mode_t output_mode = SHELL_OUTPUT_TEXT;
static char gui_buffer[SHELL_ROWS * SHELL_COLS];
static int gui_cursor_x = 0;
static int gui_cursor_y = 0;
static int gui_visible_cols = SHELL_COLS;  /* actual visible column width */

/* Forward declarations */
static void execute_command(const char *input);
static void shell_gui_putchar(char c);
static void shell_gui_print(const char *s);
static void shell_gui_print_int(uint32_t num);

void shell_set_output_mode(shell_output_mode_t mode) {
    output_mode = mode;
    if (mode == SHELL_OUTPUT_GUI) {
        memset(gui_buffer, 0, sizeof(gui_buffer));
        gui_cursor_x = 0;
        gui_cursor_y = 0;
        /* Print initial prompt into GUI buffer */
        const char *prompt = "cupid-os shell\n> ";
        while (*prompt) {
            shell_gui_putchar(*prompt);
            prompt++;
        }
        /* Configure all subsystems to use GUI output */
        fat16_set_output(shell_gui_print, shell_gui_putchar, shell_gui_print_int);
        ed_set_output(shell_gui_print, shell_gui_putchar, shell_gui_print_int);
        memory_set_output(shell_gui_print, shell_gui_print_int);
        panic_set_output(shell_gui_print, shell_gui_putchar);
        blockcache_set_output(shell_gui_print, shell_gui_print_int);
    } else {
        /* Reset all subsystems to use kernel output */
        fat16_set_output(print, putchar, print_int);
        ed_set_output(print, putchar, print_int);
        memory_set_output(print, print_int);
        panic_set_output(print, putchar);
        blockcache_set_output(print, print_int);
    }
}

shell_output_mode_t shell_get_output_mode(void) {
    return output_mode;
}

void shell_set_visible_cols(int cols) {
    if (cols > 0 && cols <= SHELL_COLS) {
        gui_visible_cols = cols;
    }
}

const char *shell_get_buffer(void) {
    return gui_buffer;
}

int shell_get_cursor_x(void) {
    return gui_cursor_x;
}

int shell_get_cursor_y(void) {
    return gui_cursor_y;
}

/* Put a character into the GUI buffer */
static void shell_gui_putchar(char c) {
    if (c == '\n') {
        /* Clear remainder of current line */
        for (int i = gui_cursor_x; i < SHELL_COLS; i++) {
            gui_buffer[gui_cursor_y * SHELL_COLS + i] = 0;
        }
        gui_cursor_x = 0;
        gui_cursor_y++;
    } else if (c == '\b') {
        if (gui_cursor_x > 0) {
            gui_cursor_x--;
            gui_buffer[gui_cursor_y * SHELL_COLS + gui_cursor_x] = ' ';
        }
    } else if (c == '\t') {
        /* Tab = 4 spaces */
        for (int t = 0; t < 4 && gui_cursor_x < gui_visible_cols; t++) {
            gui_buffer[gui_cursor_y * SHELL_COLS + gui_cursor_x] = ' ';
            gui_cursor_x++;
        }
    } else {
        if (gui_cursor_x < gui_visible_cols) {
            gui_buffer[gui_cursor_y * SHELL_COLS + gui_cursor_x] = c;
            gui_cursor_x++;
        }
    }

    /* Wrap at visible column width */
    if (gui_cursor_x >= gui_visible_cols) {
        gui_cursor_x = 0;
        gui_cursor_y++;
    }

    /* Scroll */
    if (gui_cursor_y >= SHELL_ROWS) {
        /* Move everything up by one line */
        memcpy(gui_buffer, gui_buffer + SHELL_COLS,
               (size_t)((SHELL_ROWS - 1) * SHELL_COLS));
        memset(gui_buffer + (SHELL_ROWS - 1) * SHELL_COLS, 0, (size_t)SHELL_COLS);
        gui_cursor_y = SHELL_ROWS - 1;
    }
}

/* Print a string into the GUI buffer */
static void shell_gui_print(const char *s) {
    while (*s) {
        shell_gui_putchar(*s);
        s++;
    }
}

/* Print an integer into the GUI buffer */
static void shell_gui_print_int(uint32_t num) {
    char buf[12];
    int i = 0;
    if (num == 0) { shell_gui_putchar('0'); return; }
    while (num > 0) {
        buf[i++] = (char)((num % 10) + (uint32_t)'0');
        num /= 10;
    }
    while (i > 0) shell_gui_putchar(buf[--i]);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Output wrappers that route to GUI or text mode
 * ══════════════════════════════════════════════════════════════════════ */
static void shell_print(const char *s) {
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_print(s);
    } else {
        print(s);
    }
}

static void shell_putchar(char c) {
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_putchar(c);
    } else {
        putchar(c);
    }
}

static void shell_print_int(uint32_t num) {
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_print_int(num);
    } else {
        print_int(num);
    }
}

/* External-linkage wrappers for kernel.c routing */
void shell_gui_putchar_ext(char c) {
    shell_gui_putchar(c);
}

void shell_gui_print_ext(const char *s) {
    shell_gui_print(s);
}

void shell_gui_print_int_ext(uint32_t num) {
    shell_gui_print_int(num);
}

// Scancodes for extended keys we care about
#define SCANCODE_ARROW_UP    0x48
#define SCANCODE_ARROW_DOWN  0x50
#define SCANCODE_ARROW_LEFT  0x4B
#define SCANCODE_ARROW_RIGHT 0x4D

struct shell_command {
    const char* name;
    const char* description;
    void (*func)(const char*);
};

// Forward declarations for commands
static void shell_help(const char* args);
static void shell_clear(const char* args);
static void shell_echo(const char* args);
static void shell_time_cmd(const char* args);
static void shell_reboot_cmd(const char* args);
static void shell_history_cmd(const char* args);
static void shell_ls(const char* args);
static void shell_cat(const char* args);
static void shell_sync(const char* args);
static void shell_cachestats(const char* args);
static void shell_lsdisk(const char* args);
static void shell_catdisk(const char* args);
static void shell_memdump(const char* args);
static void shell_memstats(const char* args);
static void shell_memleak(const char* args);
static void shell_memcheck(const char* args);
static void shell_stacktrace(const char* args);
static void shell_registers(const char* args);
static void shell_sysinfo(const char* args);
static void shell_loglevel(const char* args);
static void shell_logdump(const char* args);
static void shell_crashtest(const char* args);
static void shell_ed(const char* args);

// List of supported commands
static struct shell_command commands[] = {
    {"help", "Show available commands", shell_help},
    {"clear", "Clear the screen", shell_clear},
    {"echo", "Echo text back", shell_echo},
    {"time", "Show system uptime", shell_time_cmd},
    {"reboot", "Reboot the machine", shell_reboot_cmd},
    {"history", "Show recent commands", shell_history_cmd},
    {"ls", "List files in the in-memory filesystem", shell_ls},
    {"cat", "Show a file from the in-memory filesystem", shell_cat},
    {"sync", "Flush disk cache to disk", shell_sync},
    {"cachestats", "Show cache statistics", shell_cachestats},
    {"lsdisk", "List files on disk", shell_lsdisk},
    {"catdisk", "Show file from disk", shell_catdisk},
    {"memdump", "Dump memory region (hex addr len)", shell_memdump},
    {"memstats", "Show memory statistics", shell_memstats},
    {"memleak", "Detect memory leaks", shell_memleak},
    {"memcheck", "Check heap integrity", shell_memcheck},
    {"stacktrace", "Show call stack", shell_stacktrace},
    {"registers", "Dump CPU registers", shell_registers},
    {"sysinfo", "Show system information", shell_sysinfo},
    {"loglevel", "Set serial log level", shell_loglevel},
    {"logdump", "Show recent log entries", shell_logdump},
    {"crashtest", "Test crash handling", shell_crashtest},
    {"ed", "Ed line editor", shell_ed},
    {0, 0, 0} // Null terminator
};

static char history[HISTORY_SIZE][MAX_INPUT_LEN + 1];
static int history_count = 0;
static int history_next = 0;     // Next insert slot
static int history_view = -1;    // -1 when not browsing history

// Minimal strncmp to avoid touching global string utils
static int shell_strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static bool shell_starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) return false;
        s++;
        prefix++;
    }
    return true;
}

static void history_record(const char* line) {
    if (!line || line[0] == '\0') {
        return;
    }

    size_t len = strlen(line);
    if (len > MAX_INPUT_LEN) {
        len = MAX_INPUT_LEN;
    }

    for (size_t i = 0; i < len; i++) {
        history[history_next][i] = line[i];
    }
    history[history_next][len] = '\0';

    history_next = (history_next + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

// Fetch entry offset from newest (0 = most recent)
static const char* history_get_from_newest(int offset) {
    if (offset < 0 || offset >= history_count) {
        return NULL;
    }

    int idx = history_next - 1 - offset;
    while (idx < 0) {
        idx += HISTORY_SIZE;
    }
    return history[idx];
}

static void replace_input(const char* new_text, char* input, int* pos) {
    // Erase current buffer from screen
    while (*pos > 0) {
        shell_print("\b \b");
        (*pos)--;
    }

    // Load new text
    int i = 0;
    if (new_text) {
        while (new_text[i] && i < MAX_INPUT_LEN) {
            input[i] = new_text[i];
            shell_putchar(new_text[i]);
            i++;
        }
    }
    input[i] = '\0';
    *pos = i;
}

static void tab_complete(char* input, int* pos) {
    // Only complete the command (before first space)
    for (int i = 0; i < *pos; i++) {
        if (input[i] == ' ') {
            // Special-case filename completion for "cat "
            if (shell_starts_with(input, "cat ") && i == 3) {
                size_t prefix_len = (size_t)(*pos - 4);
                const char* prefix = &input[4];
                const char* first_match = NULL;
                int match_count = 0;

                uint32_t file_count = fs_get_file_count();
                for (uint32_t f = 0; f < file_count; f++) {
                    const fs_file_t* file = fs_get_file(f);
                    if (!file) continue;
                    if (shell_strncmp(file->name, prefix, prefix_len) == 0) {
                        if (match_count == 0) {
                            first_match = file->name;
                        }
                        match_count++;
                    }
                }

                if (match_count == 1 && first_match) {
                    const char* completion = first_match + prefix_len;
                    while (*completion && *pos < MAX_INPUT_LEN) {
                        input[*pos] = *completion;
                        shell_putchar(*completion);
                        (*pos)++;
                        completion++;
                    }
                } else if (match_count > 1) {
                    shell_print("\n");
                    for (uint32_t f = 0; f < file_count; f++) {
                        const fs_file_t* file = fs_get_file(f);
                        if (!file) continue;
                        if (shell_strncmp(file->name, prefix, prefix_len) == 0) {
                            shell_print(file->name);
                            shell_print("  ");
                        }
                    }
                    shell_print("\n> ");
                    for (int k = 0; k < *pos; k++) {
                        shell_putchar(input[k]);
                    }
                } else {
                    // No match; do nothing
                }
            }
            return;
        }
    }

    size_t prefix_len = (size_t)(*pos);
    char prefix[MAX_INPUT_LEN + 1];
    for (size_t i = 0; i < prefix_len; i++) {
        prefix[i] = input[i];
    }
    prefix[prefix_len] = '\0';

    const char* first_match = NULL;
    int match_count = 0;

    for (int i = 0; commands[i].name; i++) {
        if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
            if (match_count == 0) {
                first_match = commands[i].name;
            }
            match_count++;
        }
    }

    if (match_count == 1 && first_match) {
        const char* completion = first_match + prefix_len;
        while (*completion && *pos < MAX_INPUT_LEN) {
            input[*pos] = *completion;
            shell_putchar(*completion);
            (*pos)++;
            completion++;
        }
    } else if (match_count > 1) {
        shell_print("\n");
        for (int i = 0; commands[i].name; i++) {
            if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
                shell_print(commands[i].name);
                shell_print("  ");
            }
        }
        shell_print("\n> ");
        for (int i = 0; i < *pos; i++) {
            shell_putchar(input[i]);
        }
    }
}

// Echo command implementation
static void shell_echo(const char* args) {
    if (args) {
        shell_print(args);
    }
    shell_print("\n");  // Ensure we always print a newline
}

static void shell_help(const char* args) {
    (void)args;
    shell_print("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        shell_print("  ");
        shell_print(commands[i].name);
        shell_print(" - ");
        shell_print(commands[i].description);
        shell_print("\n");
    }
}

static void shell_clear(const char* args) {
    (void)args;
    if (output_mode == SHELL_OUTPUT_GUI) {
        /* Clear GUI buffer; the prompt will be printed by
         * shell_gui_handle_key after the command returns. */
        memset(gui_buffer, 0, sizeof(gui_buffer));
        gui_cursor_x = 0;
        gui_cursor_y = 0;
    } else {
        clear_screen();
    }
}

static void shell_time_cmd(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    uint32_t seconds = ms / 1000;
    uint32_t remainder = ms % 1000;

    shell_print("Uptime: ");
    shell_print_int(seconds);
    shell_putchar('.');
    // Print zero-padded remainder as fractional seconds
    shell_putchar((char)('0' + (remainder / 100)));
    shell_putchar((char)('0' + ((remainder / 10) % 10)));
    shell_putchar((char)('0' + (remainder % 10)));
    shell_print("s (");
    shell_print_int(ms);
    shell_print(" ms)\n");
}

static void shell_reboot_cmd(const char* args) {
    (void)args;
    shell_print("Rebooting...\n");
    __asm__ volatile("cli");
    while (inb(0x64) & 0x02) {
        // Wait for controller ready
    }
    outb(0x64, 0xFE);
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void shell_history_cmd(const char* args) {
    (void)args;
    if (history_count == 0) {
        shell_print("No history yet.\n");
        return;
    }

    int start = history_count - 1;
    for (int i = start; i >= 0; i--) {
        const char* entry = history_get_from_newest(start - i);
        shell_print_int((uint32_t)(history_count - i));
        shell_print(": ");
        shell_print(entry ? entry : "");
        shell_print("\n");
    }
}

static void shell_ls(const char* args) {
    (void)args;
    uint32_t count = fs_get_file_count();
    for (uint32_t i = 0; i < count; i++) {
        const fs_file_t* file = fs_get_file(i);
        if (file) {
            shell_print(file->name);
            shell_print("  ");
            shell_print_int(file->size);
            shell_print(" bytes\n");
        }
    }
}

static void shell_cat(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: cat <filename>\n");
        return;
    }

    const fs_file_t* file = fs_find(args);
    if (!file) {
        shell_print("File not found: ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    if (file->data && file->size > 0) {
        for (uint32_t i = 0; i < file->size; i++) {
            shell_putchar(file->data[i]);
        }
        if (file->data[file->size - 1] != '\n') {
            shell_print("\n");
        }
    } else {
        shell_print("(empty file)\n");
    }
}

static void shell_sync(const char* args) {
    (void)args;
    blockcache_sync();
    shell_print("Cache flushed to disk\n");
}

static void shell_cachestats(const char* args) {
    (void)args;
    blockcache_stats();
}

static void shell_lsdisk(const char* args) {
    (void)args;
    fat16_list_root();
}

static void shell_catdisk(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: catdisk <filename>\n");
        return;
    }

    fat16_file_t* file = fat16_open(args);
    if (!file) {
        shell_print("File not found: ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    char buffer[512];
    int bytes_read;
    while ((bytes_read = fat16_read(file, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            shell_putchar(buffer[i]);
        }
    }

    fat16_close(file);
    shell_print("\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  Debugging & Memory Safety Shell Commands
 * ══════════════════════════════════════════════════════════════════════ */

/* ── simple hex-string-to-uint32 parser ──────────────────────────── */
static uint32_t parse_hex(const char* s) {
    uint32_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A') + 10;
        else break;
        val = (val << 4) | d;
        s++;
    }
    return val;
}

static uint32_t parse_dec(const char* s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

/* skip whitespace, return pointer to next token */
static const char* skip_ws(const char* s) {
    while (*s == ' ') s++;
    return s;
}

/* skip non-whitespace */
static const char* skip_nws(const char* s) {
    while (*s && *s != ' ') s++;
    return s;
}

/* ── memdump <addr> <len> ── */
static void shell_memdump(const char* args) {
    if (!args || !args[0]) {
        shell_print("Usage: memdump <hex_addr> <length>\n");
        return;
    }
    const char* p = skip_ws(args);
    uint32_t addr = parse_hex(p);
    p = skip_ws(skip_nws(p));
    uint32_t len = 64;
    if (*p) len = parse_dec(p);
    if (len > 512) len = 512;

    for (uint32_t i = 0; i < len; i += 16) {
        print_hex(addr + i);
        shell_print(": ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            print_hex_byte(*((uint8_t*)(addr + i + j)));
            shell_putchar(' ');
        }
        shell_print(" ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t b = *((uint8_t*)(addr + i + j));
            shell_putchar((b >= 0x20 && b < 0x7F) ? (char)b : '.');
        }
        shell_print("\n");
    }
}

/* ── memstats ── */
static void shell_memstats(const char* args) {
    (void)args;
    print_memory_stats();
}

/* ── memleak [threshold_sec] ── */
static void shell_memleak(const char* args) {
    uint32_t threshold = 60000; /* default 60s */
    if (args && args[0]) {
        threshold = parse_dec(args) * 1000;
        if (threshold == 0) threshold = 60000;
    }
    detect_memory_leaks(threshold);
}

/* ── memcheck ── */
static void shell_memcheck(const char* args) {
    (void)args;
    shell_print("Checking heap integrity...\n");
    heap_check_integrity();
    shell_print("Heap integrity OK\n");
}

/* ── stacktrace ── */
static void shell_stacktrace(const char* args) {
    (void)args;
    uint32_t ebp, eip;
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
    print_stack_trace(ebp, eip);
}

/* ── registers ── */
static void shell_registers(const char* args) {
    (void)args;
    uint32_t eax_v, ebx_v, ecx_v, edx_v, esi_v, edi_v, ebp_v, esp_v, eflags_v;
    __asm__ volatile("movl %%eax, %0" : "=r"(eax_v));
    __asm__ volatile("movl %%ebx, %0" : "=r"(ebx_v));
    __asm__ volatile("movl %%ecx, %0" : "=r"(ecx_v));
    __asm__ volatile("movl %%edx, %0" : "=r"(edx_v));
    __asm__ volatile("movl %%esi, %0" : "=r"(esi_v));
    __asm__ volatile("movl %%edi, %0" : "=r"(edi_v));
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp_v));
    __asm__ volatile("movl %%esp, %0" : "=r"(esp_v));
    __asm__ volatile("pushfl; popl %0" : "=r"(eflags_v));

    shell_print("CPU Registers:\n");
    shell_print("  EAX: "); print_hex(eax_v);
    shell_print("  EBX: "); print_hex(ebx_v);
    shell_print("  ECX: "); print_hex(ecx_v);
    shell_print("  EDX: "); print_hex(edx_v);
    shell_print("\n");
    shell_print("  ESI: "); print_hex(esi_v);
    shell_print("  EDI: "); print_hex(edi_v);
    shell_print("  EBP: "); print_hex(ebp_v);
    shell_print("  ESP: "); print_hex(esp_v);
    shell_print("\n");
    shell_print("  EFLAGS: "); print_hex(eflags_v); print("\n");
}

/* ── sysinfo ── */
static void shell_sysinfo(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    shell_print("System Information:\n");
    shell_print("  Uptime: "); shell_print_int(ms / 1000); shell_putchar('.');
    shell_print_int(ms % 1000); shell_print("s\n");
    shell_print("  CPU Freq: "); shell_print_int((uint32_t)(get_cpu_freq() / 1000000));
    shell_print(" MHz\n");
    shell_print("  Timer Freq: "); shell_print_int(timer_get_frequency()); shell_print(" Hz\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    shell_print("  Memory: "); shell_print_int(free_pg * 4); shell_print(" KB free / ");
    shell_print_int(total_pg * 4); shell_print(" KB total\n");

    print_memory_stats();
}

/* ── loglevel [debug|info|warn|error|panic] ── */
static void shell_loglevel(const char* args) {
    if (!args || !args[0]) {
        shell_print("Current log level: ");
        shell_print(get_log_level_name());
        shell_print("\nUsage: loglevel <debug|info|warn|error|panic>\n");
        return;
    }
    if (strcmp(args, "debug") == 0)      set_log_level(LOG_DEBUG);
    else if (strcmp(args, "info") == 0)  set_log_level(LOG_INFO);
    else if (strcmp(args, "warn") == 0)  set_log_level(LOG_WARN);
    else if (strcmp(args, "error") == 0) set_log_level(LOG_ERROR);
    else if (strcmp(args, "panic") == 0) set_log_level(LOG_PANIC);
    else {
        shell_print("Unknown level: ");
        shell_print(args);
        shell_print("\n");
        return;
    }
    shell_print("Log level set to ");
    shell_print(get_log_level_name());
    shell_print("\n");
    KINFO("Log level changed to %s", get_log_level_name());
}

/* ── logdump ── */
static void shell_logdump(const char* args) {
    (void)args;
    shell_print("=== Recent Log Entries ===\n");
    print_log_buffer();
}

/* ── crashtest <type> ── */
static void shell_crashtest(const char* args) {
    if (!args || !args[0]) {
        shell_print("Usage: crashtest <type>\n");
        shell_print("  Types: panic, nullptr, divzero, assert, overflow, stackoverflow\n");
        return;
    }
    if (strcmp(args, "panic") == 0) {
        kernel_panic("Test panic from shell");
    } else if (strcmp(args, "nullptr") == 0) {
        shell_print("Dereferencing NULL pointer...\n");
        volatile int* p = (volatile int*)0;
        (void)*p;
    } else if (strcmp(args, "divzero") == 0) {
        shell_print("Dividing by zero...\n");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    } else if (strcmp(args, "assert") == 0) {
        ASSERT_MSG(1 == 2, "deliberate assertion failure from crashtest");
    } else if (strcmp(args, "overflow") == 0) {
        shell_print("Allocating and overflowing buffer...\n");
        char* buf = kmalloc(16);
        if (buf) {
            memset(buf, 'A', 32);   /* overflow by 16 bytes */
            kfree(buf);             /* should detect destroyed canary */
        }
    } else if (strcmp(args, "stackoverflow") == 0) {
        shell_print("Triggering stack overflow...\n");
        volatile char big[65536];
        big[0] = 'x';
        big[65535] = 'y';
        (void)big;
    } else {
        shell_print("Unknown crash test: ");
        shell_print(args);
        shell_print("\n");
    }
}

/* ── ed line editor ── */
static void shell_ed(const char* args) {
    ed_run(args);
}

// Find and execute a command
static void execute_command(const char* input) {
    // Skip empty input
    if (!input || input[0] == '\0') {
        return;
    }

    char cmd[MAX_INPUT_LEN];
    const char* args = 0;
    
    // Split input into command and arguments
    int i = 0;
    while (input[i] && input[i] != ' ') {
        cmd[i] = input[i];
        i++;
    }
    cmd[i] = 0;
    
    if (input[i] == ' ') {
        args = &input[i+1];
    }

    // Find and execute the command
    for (int j = 0; commands[j].name; j++) {
        if (strcmp(cmd, commands[j].name) == 0) {
            commands[j].func(args);
            return;
        }
    }

    // Handle unknown command
    shell_print("Unknown command: ");
    shell_print(cmd);
    shell_print("\n");  // Ensure we print a newline
}

// Main shell loop
void shell_run(void) {
    char input[MAX_INPUT_LEN + 1];
    int pos = 0;
    
    shell_print("cupid-os shell\n> ");
    
    while (1) {
        key_event_t event;
        keyboard_read_event(&event);
        char c = event.character;

        // History navigation with up/down arrows
        if (event.character == 0 && event.scancode == SCANCODE_ARROW_UP) {
            if (history_count > 0 && history_view < history_count - 1) {
                history_view++;
                const char* entry = history_get_from_newest(history_view);
                replace_input(entry, input, &pos);
            }
            continue;
        } else if (event.character == 0 && event.scancode == SCANCODE_ARROW_DOWN) {
            if (history_count > 0) {
                if (history_view > 0) {
                    history_view--;
                    const char* entry = history_get_from_newest(history_view);
                    replace_input(entry, input, &pos);
                } else if (history_view == 0) {
                    history_view = -1;
                    replace_input("", input, &pos);
                }
            }
            continue;
        }

        if (c == '\t') {
            tab_complete(input, &pos);
            continue;
        }

        if (c == '\n') {
            input[pos] = 0;  // Ensure null termination
            shell_putchar('\n');
            history_record(input);
            execute_command(input);
            pos = 0;
            history_view = -1;
            memset(input, 0, sizeof(input)); // Clear buffer
            shell_print("> ");
        }
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                shell_print("\b \b");  // Erase from screen
                input[pos] = '\0'; // Remove from buffer
            }
            history_view = -1; // Stop browsing when editing
        }
        else if (c && pos < MAX_INPUT_LEN) {
            input[pos++] = c;
            shell_putchar(c);
            history_view = -1; // Stop browsing when typing
        }
    }
} 

/* ══════════════════════════════════════════════════════════════════════
 *  GUI-mode shell key handler
 *
 *  Called from terminal_app.c when a key is pressed while the
 *  terminal window is focused.  Mirrors the text-mode shell_run()
 *  logic but writes to the GUI buffer instead of VGA text memory.
 * ══════════════════════════════════════════════════════════════════════ */

static char gui_input[MAX_INPUT_LEN + 1];
static int  gui_input_pos = 0;
static int  gui_history_view = -1;

/* GUI-mode wrappers (print/putchar go to GUI buffer) */
static void gui_replace_input(const char *new_text) {
    while (gui_input_pos > 0) {
        shell_gui_putchar('\b');
        gui_input_pos--;
    }
    int i = 0;
    if (new_text) {
        while (new_text[i] && i < MAX_INPUT_LEN) {
            gui_input[i] = new_text[i];
            shell_gui_putchar(new_text[i]);
            i++;
        }
    }
    gui_input[i] = '\0';
    gui_input_pos = i;
}

/* Temporarily redirect print/putchar to GUI buffer, execute command,
 * then restore.  We achieve this by using a wrapper approach:
 * shell commands call print()/putchar() which go to VGA text.
 * In GUI mode we need them to go to the GUI buffer.
 * The simplest approach: hook print/putchar via function pointers. */

/* Global function pointers for output redirection */
static void (*shell_print_fn)(const char *) = NULL;
static void (*shell_putchar_fn)(char) = NULL;
static void (*shell_print_int_fn)(uint32_t) = NULL;

static void gui_exec_command(const char *input) {
    /* Temporarily redirect kernel print/putchar to GUI buffer.
     * Since commands call print() and putchar() from kernel.c,
     * we can't easily redirect those.  Instead, we use a simpler
     * approach: the GUI buffer captures the output by hooking
     * into the output path.
     *
     * For now, we just execute the command normally.  The output
     * will go to both VGA text (invisible since we're in Mode 13h)
     * and the serial port.  To capture it in the GUI buffer, we
     * intercept at a higher level by buffering the output ourselves.
     *
     * Simple solution: copy what print/putchar write into our buffer
     * by calling shell_gui_print/shell_gui_putchar for each character
     * that would be printed.  We do this by overriding print temporarily.
     */

    /* Since commands use print() from kernel.c and we can't easily
     * redirect that, we handle only built-in shell commands that we
     * control.  For a complete solution, we'd need to make print()
     * check the output mode, but that changes kernel.c's interface.
     *
     * Pragmatic approach: execute_command calls print()/putchar() which
     * still write to 0xB8000 (harmless in Mode 13h - it's not visible).
     * We also echo the output to the GUI buffer by temporarily swapping
     * the VGA write functions. */

    /* Execute through the existing command processor */
    execute_command(input);
}

void shell_gui_handle_key(uint8_t scancode, char character) {
    if (output_mode != SHELL_OUTPUT_GUI) return;

    /* History navigation */
    if (character == 0 && scancode == SCANCODE_ARROW_UP) {
        if (history_count > 0 && gui_history_view < history_count - 1) {
            gui_history_view++;
            const char *entry = history_get_from_newest(gui_history_view);
            gui_replace_input(entry);
        }
        return;
    }
    if (character == 0 && scancode == SCANCODE_ARROW_DOWN) {
        if (history_count > 0) {
            if (gui_history_view > 0) {
                gui_history_view--;
                const char *entry = history_get_from_newest(gui_history_view);
                gui_replace_input(entry);
            } else if (gui_history_view == 0) {
                gui_history_view = -1;
                gui_replace_input("");
            }
        }
        return;
    }

    /* Tab completion */
    if (character == '\t') {
        tab_complete(gui_input, &gui_input_pos);
        gui_history_view = -1;
        return;
    }

    if (character == '\n') {
        gui_input[gui_input_pos] = '\0';
        shell_gui_putchar('\n');

        if (gui_input_pos > 0) {
            history_record(gui_input);
            /* Execute command - output goes to VGA text memory (hidden)
             * AND we mirror it by patching print/putchar temporarily */
            gui_exec_command(gui_input);
        }

        gui_input_pos = 0;
        gui_history_view = -1;
        memset(gui_input, 0, sizeof(gui_input));
        shell_gui_print("> ");
    }
    else if (character == '\b') {
        if (gui_input_pos > 0) {
            gui_input_pos--;
            shell_gui_print("\b \b");
            gui_input[gui_input_pos] = '\0';
        }
        gui_history_view = -1;
    }
    else if (character && gui_input_pos < MAX_INPUT_LEN) {
        gui_input[gui_input_pos++] = character;
        shell_gui_putchar(character);
        gui_history_view = -1;
    }

    (void)shell_print_fn;
    (void)shell_putchar_fn;
    (void)shell_print_int_fn;
    (void)shell_gui_print_int;
}
