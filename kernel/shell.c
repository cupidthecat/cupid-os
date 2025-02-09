#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "../filesystem/fs.h"
#include "../filesystem/path.h"

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
    int target_dir;
    
    if (!args || strlen(args) == 0) {
        // No argument: use the current directory.
        target_dir = fs_current_directory;
    } else {
        // If the path starts with '/', resolve as an absolute path;
        // otherwise, resolve relative to the current directory.
        if (args[0] == '/') {
            target_dir = resolve_path(args);
        } else {
            target_dir = resolve_relative_path(fs_current_directory, args);
        }
        if (target_dir < 0) {
            print("Directory not found: ");
            print(args);
            print("\n");
            return;
        }
    }
    
    // List only files whose parent equals target_dir.
    for (int i = 0; i < MAX_FILES; i++) {
        // Check if the file entry is used and if it belongs to target_dir.
        if (files[i].name[0] != 0 && files[i].parent == target_dir) {
            char clean_name[MAX_FILENAME + 1];
            // Clear the buffer so that stray characters are not printed.
            memset(clean_name, 0, sizeof(clean_name));
            strncpy(clean_name, files[i].name, MAX_FILENAME);
            clean_name[MAX_FILENAME] = '\0';
            print(clean_name);
            print(files[i].is_dir ? " <DIR>\n" : "\n");
        }
    }
}

static void shell_cd(const char* args) {
    if (!args || strlen(args) == 0) {
        print("Usage: cd <directory>\n");
        return;
    }
    // If the argument is ".", do nothing.
    if (strcmp(args, ".") == 0) {
        return;
    }
    // If the argument is "..", go to the parent directory (if not already at root).
    if (strcmp(args, "..") == 0) {
        // Here we assume that the root directory is index 0.
        if (fs_current_directory != 0) {
            fs_current_directory = files[fs_current_directory].parent;
        }
        return;
    }
    // Otherwise, look for a directory with the given name in the current directory.
    int index = fs_find_in_directory(fs_current_directory, args);
    if (index < 0) {
        print("Directory not found: ");
        print(args);
        print("\n");
        return;
    }
    if (!files[index].is_dir) {
        print(args);
        print(" is not a directory.\n");
        return;
    }
    // Change the current directory to the found directory.
    fs_current_directory = index;
}

static void shell_pwd(const char* args) {
    (void)args; // Unused parameter
    char path[MAX_PATH_LENGTH];
    fs_get_current_path(path, sizeof(path));
    print(path);
    print("\n");
}

// List of supported commands
static struct shell_command commands[] = {
    {"echo", shell_echo},
    {"mkdir", shell_mkdir},
    {"ls", shell_ls},
    {"cd", shell_cd},
    {"pwd", shell_pwd},
    // {"cat", shell_cat},  // Comment out until implemented
    {0, 0}
};