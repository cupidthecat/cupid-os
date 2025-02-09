#include "fs.h"
#include "../kernel/string.h"
#include "../kernel/kernel.h"
static struct superblock sb;
struct file_entry files[MAX_FILES] = {0};
static uint8_t* fs_data;
int fs_current_directory = 0;

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
    // Find an empty slot (for simplicity).
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].name[0] == 0) {
            strncpy(files[i].name, name, MAX_FILENAME);
            files[i].name[MAX_FILENAME - 1] = '\0';  // Ensure null termination
            files[i].is_dir = is_dir;
            // Set the parent to the current directory
            files[i].parent = fs_current_directory;
            return i;
        }
    }
    return -1; // no available slot
}

int fs_find_in_directory(int parent, const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        // Check that the slot is used, that its parent matches, and names are equal.
        if (files[i].name[0] != 0 && files[i].parent == parent && strcmp(files[i].name, name) == 0) {
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

char* fs_get_current_path(char* buffer, size_t size) {
    int path_components[MAX_FILES];
    int count = 0;
    int current = fs_current_directory;

    // Collect directory hierarchy
    while (current != ROOT_INODE && count < MAX_FILES) {
        path_components[count++] = current;
        current = files[current].parent;
    }

    // Build path string
    size_t pos = 0;
    buffer[pos++] = '/';
    
    // Reverse traverse the components
    for (int i = count - 1; i >= 0; i--) {
        const char* name = files[path_components[i]].name;
        size_t name_len = strlen(name);
        
        if (pos + name_len + 1 >= size) break;
        
        strncpy(buffer + pos, name, name_len);
        pos += name_len;
        buffer[pos++] = '/';
    }
    
    // Remove trailing slash if not root
    if (pos > 1) pos--;
    buffer[pos] = '\0';
    
    return buffer;
}