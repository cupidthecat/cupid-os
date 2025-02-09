#ifndef FS_H
#define FS_H

#include "../kernel/types.h"
#include "../drivers/timer.h"

#define FS_BLOCK_SIZE 512
#define MAX_FILENAME 32
#define MAX_FILES 128
#define ROOT_INODE 0
#define MAX_PATH_LENGTH 256

typedef uint32_t fs_time_t;

struct file_entry {
    char name[MAX_FILENAME];
    uint32_t inode;
    uint32_t size;
    uint32_t blocks;
    uint32_t block_pointers[16];
    bool is_dir;
    int parent;
    fs_time_t created;
    fs_time_t modified;
};

struct superblock {
    uint32_t magic;
    uint32_t num_blocks;
    uint32_t free_blocks;
    uint32_t inode_count;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t root_dir;
};

extern struct file_entry files[MAX_FILES];
extern int fs_current_directory;

int fs_create_file(const char* name, bool is_dir);
int fs_find_in_directory(int parent, const char* name);
void fs_init();
int fs_resolve_path(const char* path);
void fs_list_directory(int dir_inode);
char* fs_get_current_path(char* buffer, size_t size);

#endif
