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
        print("\b \b");
        (*pos)--;
    }

    // Load new text
    int i = 0;
    if (new_text) {
        while (new_text[i] && i < MAX_INPUT_LEN) {
            input[i] = new_text[i];
            putchar(new_text[i]);
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
                        putchar(*completion);
                        (*pos)++;
                        completion++;
                    }
                } else if (match_count > 1) {
                    print("\n");
                    for (uint32_t f = 0; f < file_count; f++) {
                        const fs_file_t* file = fs_get_file(f);
                        if (!file) continue;
                        if (shell_strncmp(file->name, prefix, prefix_len) == 0) {
                            print(file->name);
                            print("  ");
                        }
                    }
                    print("\n> ");
                    for (int k = 0; k < *pos; k++) {
                        putchar(input[k]);
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
            putchar(*completion);
            (*pos)++;
            completion++;
        }
    } else if (match_count > 1) {
        print("\n");
        for (int i = 0; commands[i].name; i++) {
            if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
                print(commands[i].name);
                print("  ");
            }
        }
        print("\n> ");
        for (int i = 0; i < *pos; i++) {
            putchar(input[i]);
        }
    }
}

// Echo command implementation
static void shell_echo(const char* args) {
    if (args) {
        print(args);
    }
    print("\n");  // Ensure we always print a newline
}

static void shell_help(const char* args) {
    (void)args;
    print("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        print("  ");
        print(commands[i].name);
        print(" - ");
        print(commands[i].description);
        print("\n");
    }
}

static void shell_clear(const char* args) {
    (void)args;
    clear_screen();
}

static void shell_time_cmd(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    uint32_t seconds = ms / 1000;
    uint32_t remainder = ms % 1000;

    print("Uptime: ");
    print_int(seconds);
    putchar('.');
    // Print zero-padded remainder as fractional seconds
    putchar((char)('0' + (remainder / 100)));
    putchar((char)('0' + ((remainder / 10) % 10)));
    putchar((char)('0' + (remainder % 10)));
    print("s (");
    print_int(ms);
    print(" ms)\n");
}

static void shell_reboot_cmd(const char* args) {
    (void)args;
    print("Rebooting...\n");
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
        print("No history yet.\n");
        return;
    }

    int start = history_count - 1;
    for (int i = start; i >= 0; i--) {
        const char* entry = history_get_from_newest(start - i);
        print_int((uint32_t)(history_count - i));
        print(": ");
        print(entry ? entry : "");
        print("\n");
    }
}

static void shell_ls(const char* args) {
    (void)args;
    uint32_t count = fs_get_file_count();
    for (uint32_t i = 0; i < count; i++) {
        const fs_file_t* file = fs_get_file(i);
        if (file) {
            print(file->name);
            print("  ");
            print_int(file->size);
            print(" bytes\n");
        }
    }
}

static void shell_cat(const char* args) {
    if (!args || args[0] == '\0') {
        print("Usage: cat <filename>\n");
        return;
    }

    const fs_file_t* file = fs_find(args);
    if (!file) {
        print("File not found: ");
        print(args);
        print("\n");
        return;
    }

    if (file->data && file->size > 0) {
        for (uint32_t i = 0; i < file->size; i++) {
            putchar(file->data[i]);
        }
        if (file->data[file->size - 1] != '\n') {
            print("\n");
        }
    } else {
        print("(empty file)\n");
    }
}

static void shell_sync(const char* args) {
    (void)args;
    blockcache_sync();
    print("Cache flushed to disk\n");
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
        print("Usage: catdisk <filename>\n");
        return;
    }

    fat16_file_t* file = fat16_open(args);
    if (!file) {
        print("File not found: ");
        print(args);
        print("\n");
        return;
    }

    char buffer[512];
    int bytes_read;
    while ((bytes_read = fat16_read(file, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            putchar(buffer[i]);
        }
    }

    fat16_close(file);
    print("\n");
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
        print("Usage: memdump <hex_addr> <length>\n");
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
        print(": ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            print_hex_byte(*((uint8_t*)(addr + i + j)));
            putchar(' ');
        }
        print(" ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t b = *((uint8_t*)(addr + i + j));
            putchar((b >= 0x20 && b < 0x7F) ? (char)b : '.');
        }
        print("\n");
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
    print("Checking heap integrity...\n");
    heap_check_integrity();
    print("Heap integrity OK\n");
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

    print("CPU Registers:\n");
    print("  EAX: "); print_hex(eax_v);
    print("  EBX: "); print_hex(ebx_v);
    print("  ECX: "); print_hex(ecx_v);
    print("  EDX: "); print_hex(edx_v);
    print("\n");
    print("  ESI: "); print_hex(esi_v);
    print("  EDI: "); print_hex(edi_v);
    print("  EBP: "); print_hex(ebp_v);
    print("  ESP: "); print_hex(esp_v);
    print("\n");
    print("  EFLAGS: "); print_hex(eflags_v); print("\n");
}

/* ── sysinfo ── */
static void shell_sysinfo(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    print("System Information:\n");
    print("  Uptime: "); print_int(ms / 1000); putchar('.');
    print_int(ms % 1000); print("s\n");
    print("  CPU Freq: "); print_int((uint32_t)(get_cpu_freq() / 1000000));
    print(" MHz\n");
    print("  Timer Freq: "); print_int(timer_get_frequency()); print(" Hz\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    print("  Memory: "); print_int(free_pg * 4); print(" KB free / ");
    print_int(total_pg * 4); print(" KB total\n");

    print_memory_stats();
}

/* ── loglevel [debug|info|warn|error|panic] ── */
static void shell_loglevel(const char* args) {
    if (!args || !args[0]) {
        print("Current log level: ");
        print(get_log_level_name());
        print("\nUsage: loglevel <debug|info|warn|error|panic>\n");
        return;
    }
    if (strcmp(args, "debug") == 0)      set_log_level(LOG_DEBUG);
    else if (strcmp(args, "info") == 0)  set_log_level(LOG_INFO);
    else if (strcmp(args, "warn") == 0)  set_log_level(LOG_WARN);
    else if (strcmp(args, "error") == 0) set_log_level(LOG_ERROR);
    else if (strcmp(args, "panic") == 0) set_log_level(LOG_PANIC);
    else {
        print("Unknown level: ");
        print(args);
        print("\n");
        return;
    }
    print("Log level set to ");
    print(get_log_level_name());
    print("\n");
    KINFO("Log level changed to %s", get_log_level_name());
}

/* ── logdump ── */
static void shell_logdump(const char* args) {
    (void)args;
    print("=== Recent Log Entries ===\n");
    print_log_buffer();
}

/* ── crashtest <type> ── */
static void shell_crashtest(const char* args) {
    if (!args || !args[0]) {
        print("Usage: crashtest <type>\n");
        print("  Types: panic, nullptr, divzero, assert, overflow, stackoverflow\n");
        return;
    }
    if (strcmp(args, "panic") == 0) {
        kernel_panic("Test panic from shell");
    } else if (strcmp(args, "nullptr") == 0) {
        print("Dereferencing NULL pointer...\n");
        volatile int* p = (volatile int*)0;
        (void)*p;
    } else if (strcmp(args, "divzero") == 0) {
        print("Dividing by zero...\n");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    } else if (strcmp(args, "assert") == 0) {
        ASSERT_MSG(1 == 2, "deliberate assertion failure from crashtest");
    } else if (strcmp(args, "overflow") == 0) {
        print("Allocating and overflowing buffer...\n");
        char* buf = kmalloc(16);
        if (buf) {
            memset(buf, 'A', 32);   /* overflow by 16 bytes */
            kfree(buf);             /* should detect destroyed canary */
        }
    } else if (strcmp(args, "stackoverflow") == 0) {
        print("Triggering stack overflow...\n");
        volatile char big[65536];
        big[0] = 'x';
        big[65535] = 'y';
        (void)big;
    } else {
        print("Unknown crash test: ");
        print(args);
        print("\n");
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
    print("Unknown command: ");
    print(cmd);
    print("\n");  // Ensure we print a newline
}

// Main shell loop
void shell_run(void) {
    char input[MAX_INPUT_LEN + 1];
    int pos = 0;
    
    print("cupid-os shell\n> ");
    
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
            putchar('\n');
            history_record(input);
            execute_command(input);
            pos = 0;
            history_view = -1;
            memset(input, 0, sizeof(input)); // Clear buffer
            print("> ");
        }
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                print("\b \b");  // Erase from screen
                input[pos] = '\0'; // Remove from buffer
            }
            history_view = -1; // Stop browsing when editing
        }
        else if (c && pos < MAX_INPUT_LEN) {
            input[pos++] = c;
            putchar(c);
            history_view = -1; // Stop browsing when typing
        }
    }
} 
