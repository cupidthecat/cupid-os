#include "../kernel/types.h"
#include "../drivers/timer.h"

#define FS_BLOCK_SIZE 512
#define MAX_FILENAME 32
#define MAX_FILES 128
#define ROOT_INODE 0

typedef uint32_t fs_time_t;

struct file_entry {
    char name[MAX_FILENAME];
    uint32_t inode;
    uint32_t size;
    uint32_t blocks;
    uint32_t block_pointers[16];
    bool is_dir;
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

int fs_create_file(const char* name, bool is_dir);
void fs_init();