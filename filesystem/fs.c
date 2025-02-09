#include "fs.h"
#include "../kernel/string.h"
#include "../kernel/kernel.h"
static struct superblock sb;
struct file_entry files[MAX_FILES] = {0};
static uint8_t* fs_data;

void fs_init() {
    // Initialize all files to zero first
    
    // Initialize superblock
    sb.magic = 0xC0DE1234;
    sb.block_size = FS_BLOCK_SIZE;
    sb.root_dir = ROOT_INODE;
    
    // Create root directory
    struct file_entry* root = &files[ROOT_INODE];
    strncpy(root->name, "/", MAX_FILENAME);
    root->name[MAX_FILENAME-1] = '\0';
    root->is_dir = true;
    root->created = timer_get_uptime_ms();

    print("[:3] Filesystem Initialized\n");
}

int fs_create_file(const char* name, bool is_dir) {
    // Find free inode
    for(int i = 0; i < MAX_FILES; i++) {
        if(files[i].name[0] == 0) {
            strncpy(files[i].name, name, MAX_FILENAME-1);
            files[i].name[MAX_FILENAME-1] = '\0'; // Add null terminator
            files[i].is_dir = is_dir;
            files[i].created = timer_get_uptime_ms();
            return i;
        }
    }
    return -1;
}

void fs_write(int inode, const void* data, size_t size) {
    if(inode < 0 || inode >= MAX_FILES) return;
    
    struct file_entry* file = &files[inode];
    // Simple allocation - just use next available block
    uint32_t blocks_needed = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    
    for(int i = 0; i < blocks_needed; i++) {
        file->block_pointers[i] = i; // Temporary simple allocation
    }
    
    memcpy(fs_data + file->block_pointers[0] * FS_BLOCK_SIZE, data, size);
    file->size = size;
    file->modified = timer_get_uptime_ms();
}