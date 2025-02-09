#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#define MAX_INPUT_LEN 80

// Shell command structure
struct shell_command {
    const char* name;
    void (*func)(const char*);
};

// Echo command implementation
static void shell_echo(const char* args) {
    if (args) {
        print(args);
    }
    print("\n");  // Ensure we always print a newline
}

// List of supported commands
static struct shell_command commands[] = {
    {"echo", shell_echo},
    {0, 0} // Null terminator
};

// Find and execute a command
static void execute_command(const char* input) {
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
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            commands[i].func(args);
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
        char c = getchar();
        
        if (c == '\n') {
            input[pos] = 0;  // Ensure null termination
            putchar('\n');         
            execute_command(input);
            pos = 0;
            memset(input, 0, sizeof(input)); // Clear buffer
            print("> ");
        }
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                print("\b \b");  // Erase from screen
                input[pos] = '\0'; // Remove from buffer
            }
        }
        else if (pos < MAX_INPUT_LEN) {
            input[pos++] = c;
            putchar(c);
        }
    }
} 