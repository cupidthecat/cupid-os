#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"
#include "ports.h"
#include "types.h"
#include "fs.h"

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
    putchar('0' + (remainder / 100));
    putchar('0' + ((remainder / 10) % 10));
    putchar('0' + (remainder % 10));
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
        print_int(history_count - i);
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
