#include "fs.h"
#include "string.h"

// Simple read-only in-memory filesystem
// Add new static entries here to expose files to the shell.
static const char license_text[] =
"cupid-os is GPLv3. See LICENSE for full terms.\n";

static const char motd_text[] =
"Welcome to cupid-os!\n"
"Commands: help, ls, cat <file>, time, clear, reboot\n";

static fs_file_t fs_files[] = {
    { "LICENSE.txt", 0, 0 },
    { "MOTD.txt", 0, 0 },
};

static uint32_t fs_file_count = sizeof(fs_files) / sizeof(fs_file_t);

void fs_init(void) {
    // Wire static data pointers
    fs_files[0].data = license_text;
    fs_files[1].data = motd_text;

    // Precompute sizes to avoid strlen at runtime in listings
    for (uint32_t i = 0; i < fs_file_count; i++) {
        if (fs_files[i].data) {
            fs_files[i].size = (uint32_t)strlen(fs_files[i].data);
        } else {
            fs_files[i].size = 0;
        }
    }
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
