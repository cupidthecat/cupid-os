#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "../filesystem/fs.h"

#define MAX_INPUT_LEN 80

// Declare extern files array
extern struct file_entry files[MAX_FILES];

// Shell command structure
struct shell_command {
    const char* name;
    void (*func)(const char*);
};

static struct shell_command commands[];
static void shell_mkdir(const char* args);
static void shell_ls(const char* args);

// Echo command implementation
static void shell_echo(const char* args) {
    if (args) {
        print(args);
    }
    print("\n");  // Ensure we always print a newline
}

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
    print("Unknown command: '");
    print(input);  // Print full input instead of just command part
    print("'\n");
}

// Main shell loop
void shell_run(void) {
    char input[MAX_INPUT_LEN + 1];
    int pos = 0;
    
    print("cupid-os shell\n> ");
    
    while (1) {
        char c = getchar();
        
        if (c == '\n') {
            input[pos] = 0;
            putchar('\n');         
            execute_command(input);
            pos = 0;
            memset(input, 0, sizeof(input)); // Clear buffer
            print("> ");           // Always print new prompt on a new line
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

// New command implementations
static void shell_mkdir(const char* args) {
    if (!args || strlen(args) == 0) {
        print("Error: Missing directory name\nUsage: mkdir <name>\n");
        return;
    }
    
    int inode = fs_create_file(args, true);
    if(inode >= 0) {
        print("Directory created\n");
    } else {
        print("Error creating directory - maybe maximum files reached?\n");
    }
}

static void shell_ls(const char* args) {
    // List directory contents
    for(int i = 0; i < MAX_FILES; i++) {
        if(files[i].name[0] != 0 && files[i].name[0] != 0xFF) {
            print(files[i].name);
            print(files[i].is_dir ? " <DIR>\n" : "\n");
        }
    }
} 

// List of supported commands
static struct shell_command commands[] = {
    {"echo", shell_echo},
    {"mkdir", shell_mkdir},
    {"ls", shell_ls},
    // {"cat", shell_cat},  // Comment out until implemented
    {0, 0}
};