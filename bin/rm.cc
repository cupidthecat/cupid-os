//help: Remove (delete) files
//help: Usage: rm <file1> [file2] ...
//help: Deletes one or more files from the filesystem.
//help: Use with caution - deleted files cannot be recovered!

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
        print("Usage: rm <file1> [file2] ...\n");
        print("Remove (delete) one or more files\n");
        return;
    }

    // Process each file argument
    char filename[256];
    char path[256];
    int pos = 0;
    int files_deleted = 0;
    int errors = 0;

    while (1) {
        // Parse next filename
        int len = parse_token(args, pos, filename, 256);
        if (len == 0) break;  // No more tokens
        pos = pos + len;

        // Resolve path
        resolve_path(filename, path);

        // Try to delete the file
        int result = vfs_unlink(path);

        if (result == 0) {
            // Success
            files_deleted = files_deleted + 1;
        } else {
            // Error
            print("rm: cannot remove '");
            print(filename);
            print("': ");

            if (result == -2) {
                print("No such file or directory\n");
            } else if (result == -13) {
                print("Permission denied\n");
            } else if (result == -21) {
                print("Is a directory\n");
            } else {
                print("Error code ");
                print_int(result);
                print("\n");
            }

            errors = errors + 1;
        }
    }

    // Print summary if multiple files
    if (files_deleted + errors > 1) {
        print("Removed ");
        print_int(files_deleted);
        print(" file");
        if (files_deleted != 1) {
            print("s");
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
