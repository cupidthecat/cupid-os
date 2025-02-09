// filesystem/path.c
#include "fs.h"
#include "../kernel/string.h"
#include "path.h"

int resolve_path(const char* path) {
    int current = 0;  // root directory index
    char token[MAX_FILENAME];
    int token_index = 0;
    int i = 0;

    if (path[0] == '/')
        i++;

    while (1) {
        char c = path[i];
        if (c == '/' || c == '\0') {
            token[token_index] = '\0';
            if (token_index > 0) {
                int next = fs_find_in_directory(current, token);
                if (next < 0 || !files[next].is_dir) {
                    return -1;
                }
                current = next;
            }
            token_index = 0;
            if (c == '\0')
                break;
        } else {
            token[token_index++] = c;
            if (token_index >= MAX_FILENAME)
                token_index = MAX_FILENAME - 1;
        }
        i++;
    }
    return current;
}

int resolve_relative_path(int base, const char* path) {
    int current = base;
    char token[MAX_FILENAME];
    int token_index = 0;
    int i = 0;

    while (1) {
        char c = path[i];
        if (c == '/' || c == '\0') {
            token[token_index] = '\0';
            if (token_index > 0) {
                if (strcmp(token, ".") == 0) {
                    // stay in current directory
                } else if (strcmp(token, "..") == 0) {
                    if (current != 0)
                        current = files[current].parent;
                } else {
                    int next = fs_find_in_directory(current, token);
                    if (next < 0 || !files[next].is_dir)
                        return -1;
                    current = next;
                }
            }
            token_index = 0;
            if (c == '\0')
                break;
        } else {
            token[token_index++] = c;
            if (token_index >= MAX_FILENAME)
                token_index = MAX_FILENAME - 1;
        }
        i++;
    }
    return current;
}
