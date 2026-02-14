//help: Create directories
//help: Usage: mkdir <dir1> [dir2] ...
//help: Creates one or more directories. Parent directories must
//help: already exist - mkdir does not create parent directories.

// Parse a single token from a string
// Returns the length of the token (0 if no token found)
int parse_token(char *str, int start, char *out, int maxlen) {
    int i = start;

    // Skip leading spaces
    while (str[i] == ' ' || str[i] == '\t') {
        i = i + 1;
    }

    // Check if end of string
    if (str[i] == 0) {
        out[0] = 0;
        return 0;
    }

    // Copy token until space or end
    int j = 0;
    while (str[i] != 0 && str[i] != ' ' && str[i] != '\t' && j < maxlen - 1) {
        out[j] = str[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;

    return i - start;
}

void main() {
    char *args = (char*)get_args();

    // Check if arguments provided
    if (strlen(args) == 0) {
        print("Usage: mkdir <dir1> [dir2] ...\n");
        print("Create one or more directories\n");
        return;
    }

    // Process each directory argument
    char dirname[256];
    char path[256];
    int pos = 0;
    int dirs_created = 0;
    int errors = 0;

    while (1) {
        // Parse next directory name
        int len = parse_token(args, pos, dirname, 256);
        if (len == 0) break;  // No more tokens
        pos = pos + len;

        // Resolve path
        resolve_path(dirname, path);

        // Try to create the directory
        int result = vfs_mkdir(path);

        if (result == 0) {
            // Success
            dirs_created = dirs_created + 1;
        } else {
            // Error
            print("mkdir: cannot create directory '");
            print(dirname);
            print("': ");

            if (result == -2) {
                print("Parent directory does not exist\n");
            } else if (result == -17) {
                print("File exists\n");
            } else if (result == -20) {
                print("Not a directory\n");
            } else if (result == -28) {
                print("No space left on device\n");
            } else {
                print("Error code ");
                print_int(result);
                print("\n");
            }

            errors = errors + 1;
        }
    }

    // Print summary if multiple directories
    if (dirs_created + errors > 1) {
        print("Created ");
        print_int(dirs_created);
        print(" director");
        if (dirs_created == 1) {
            print("y");
        } else {
            print("ies");
        }

        if (errors > 0) {
            print(", ");
            print_int(errors);
            print(" error");
            if (errors != 1) {
                print("s");
            }
        }
        print("\n");
    }
}
