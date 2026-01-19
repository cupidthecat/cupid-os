#include "fs.h"
#include "string.h"

// Simple read-only in-memory filesystem
// Add new static entries here to expose files to the shell.
static const char license_text[] =
"cupid-os is GPLv3. See LICENSE for full terms.\n";

static const char motd_text[] =
"Welcome to cupid-os!\n"
"Commands: help, ls, cat <file>, time, clear, reboot\n";

static const fs_file_t fs_files[] = {
    { "LICENSE.txt", license_text, sizeof(license_text) - 1 },
    { "MOTD.txt", motd_text, sizeof(motd_text) - 1 },
};

static const uint32_t fs_file_count = sizeof(fs_files) / sizeof(fs_file_t);

void fs_init(void) {
    // Files are now initialized at compile time
}

uint32_t fs_get_file_count(void) {
    return fs_file_count;
}

const fs_file_t* fs_get_file(uint32_t index) {
    if (index >= fs_file_count) {
        return 0;
    }
    return &fs_files[index];
}

const fs_file_t* fs_find(const char* name) {
    if (!name) return 0;
    for (uint32_t i = 0; i < fs_file_count; i++) {
        if (strcmp(name, fs_files[i].name) == 0) {
            return &fs_files[i];
        }
    }
    return 0;
}
