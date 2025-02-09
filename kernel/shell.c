#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "../filesystem/fs.h"
#include "../filesystem/path.h"
#include "../drivers/vga.h"

#define MAX_INPUT_LEN 80  // Maximum length of user input

// Declare extern files array from filesystem
extern struct file_entry files[MAX_FILES];

/**
 * struct shell_command - Structure representing a shell command
 * @name: Command name (string)
 * @func: Pointer to function implementing the command
 */
struct shell_command {
    const char* name;
    void (*func)(const char*);
};

// Forward declarations
static struct shell_command commands[];
static void shell_mkdir(const char* args);
static void shell_ls(const char* args);

/**
 * shell_echo - Implements the echo command
 * @args: Arguments passed to the command (string to echo)
 *
 * Prints the provided arguments followed by a newline.
 * If no arguments are provided, just prints a newline.
 */
static void shell_echo(const char* args) {
    if (args) {
        print(args);
    }
    print("\n");  // Ensure we always print a newline
}

/**
 * execute_command - Parses and executes a shell command
 * @input: The full input string from the user
 *
 * Splits the input into command and arguments, then searches for
 * and executes the matching command. If no matching command is found,
 * prints an error message.
 */
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

/**
 * shell_run - Main shell loop
 *
 * Implements the interactive shell interface. Handles user input,
 * command execution, and basic line editing (backspace support).
 * Runs in an infinite loop until system shutdown.
 */
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

/**
 * shell_mkdir - Implements the mkdir command
 * @args: Directory name to create
 *
 * Creates a new directory with the specified name.
 * Prints success or error message based on operation result.
 */
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

/**
 * shell_ls - Implements the ls command
 * @args: Optional directory path to list
 *
 * Lists contents of the current directory or specified directory.
 * Shows directory entries with <DIR> marker for subdirectories.
 */
static void shell_ls(const char* args) {
    int target_dir;
    
    if (!args || strlen(args) == 0) {
        target_dir = fs_current_directory;
    } else {
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
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].name[0] != 0 && files[i].parent == target_dir) {
            // Create a properly null-terminated string
            char clean_name[MAX_FILENAME + 1];
            memset(clean_name, 0, sizeof(clean_name));
            strncpy(clean_name, files[i].name, MAX_FILENAME);
            clean_name[MAX_FILENAME] = '\0';  // Ensure null termination
            
            // Print the name with proper padding
            print(clean_name);
            print(" ");  // Add space between entries
            
            if (files[i].is_dir) {
                print("<DIR>");
            }
            print("\n");
        }
    }
}

/**
 * shell_cd - Implements the cd command
 * @args: Directory path to change to
 *
 * Changes the current working directory. Supports:
 * - Absolute paths (starting with /)
 * - Relative paths
 * - Special paths (. and ..)
 */
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

/**
 * shell_pwd - Implements the pwd command
 * @args: Unused parameter (required for command function signature)
 *
 * Prints the current working directory path.
 */
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
    {0, 0}  // Null terminator for command list
};