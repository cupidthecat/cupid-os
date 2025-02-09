#include "fs.h"
#include "../kernel/string.h"
#include "../kernel/kernel.h"

// Filesystem global variables
static struct superblock sb;          // Superblock containing filesystem metadata
struct file_entry files[MAX_FILES] = {0};  // Array of all file entries (inodes)
static uint8_t* fs_data;              // Pointer to filesystem data blocks
int fs_current_directory = 0;         // Current working directory inode

/**
 * Initialize the filesystem
 * Sets up superblock, creates root directory, and initializes file entries
 */
void fs_init() {
    // Initialize superblock with filesystem metadata
    sb.magic = 0xC0DE1234;           // Magic number to identify filesystem
    sb.block_size = FS_BLOCK_SIZE;   // Size of each block in bytes
    sb.root_dir = ROOT_INODE;        // Inode number of root directory
    
    // Create root directory entry
    struct file_entry* root = &files[ROOT_INODE];
    strncpy(root->name, "/", MAX_FILENAME);  // Set root directory name
    root->name[MAX_FILENAME-1] = '\0';      // Ensure null termination
    root->is_dir = true;                     // Mark as directory
    root->created = timer_get_uptime_ms();   // Set creation timestamp

    print("[:3] Filesystem Initialized\n");
}

/**
 * Create a new file or directory
 * @param name: Name of the new file/directory (must be unique in parent directory)
 * @param is_dir: Boolean indicating whether to create a directory (true) or file (false)
 * @return: Inode number of created file/directory, or -1 if no space available
 */
int fs_create_file(const char* name, bool is_dir) {
    // Find an empty slot in the files array
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].name[0] == 0) {
            // Copy name with proper bounds checking
            strncpy(files[i].name, name, MAX_FILENAME);
            files[i].name[MAX_FILENAME - 1] = '\0';  // Ensure null termination
            files[i].is_dir = is_dir;                // Set directory flag
            files[i].parent = fs_current_directory;  // Set parent to current directory
            return i;                                // Return inode number
        }
    }
    return -1; // No available slots
}

/**
 * Find a file/directory in a specific parent directory
 * @param parent: Inode number of parent directory to search in
 * @param name: Name of file/directory to find
 * @return: Inode number of found entry, or -1 if not found
 */
int fs_find_in_directory(int parent, const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        // Check for matching entry
        if (files[i].name[0] != 0 &&              // Entry is in use
            files[i].parent == parent &&          // Parent matches
            strcmp(files[i].name, name) == 0) {   // Name matches
            return i;
        }
    }
    return -1; // Not found
}

/**
 * Write data to a file
 * @param inode: Inode number of file to write to
 * @param data: Pointer to data to write
 * @param size: Number of bytes to write
 */
void fs_write(int inode, const void* data, size_t size) {
    // Validate inode number
    if(inode < 0 || inode >= MAX_FILES) return;
    
    struct file_entry* file = &files[inode];
    // Calculate number of blocks needed
    uint32_t blocks_needed = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    
    // Simple block allocation
    for(int i = 0; i < blocks_needed; i++) {
        file->block_pointers[i] = i; // Temporary simple allocation
    }
    
    // Copy data to filesystem
    memcpy(fs_data + file->block_pointers[0] * FS_BLOCK_SIZE, data, size);
    file->size = size;                          // Update file size
    file->modified = timer_get_uptime_ms();     // Update modification time
}

/**
 * Get current working directory path
 * @param buffer: Pointer to buffer to store path string
 * @param size: Size of buffer in bytes
 * @return: Pointer to buffer containing path string
 */
char* fs_get_current_path(char* buffer, size_t size) {
    int path_components[MAX_FILES];  // Array to store inodes in path
    int count = 0;                   // Number of components in path
    int current = fs_current_directory;

    // Collect directory hierarchy from current to root
    while (current != ROOT_INODE && count < MAX_FILES) {
        path_components[count++] = current;
        current = files[current].parent;
    }

    // Build path string starting with root
    size_t pos = 0;
    buffer[pos++] = '/';
    
    // Traverse components in reverse order (root to current)
    for (int i = count - 1; i >= 0; i--) {
        const char* name = files[path_components[i]].name;
        size_t name_len = strlen(name);
        
        // Check for buffer overflow
        if (pos + name_len + 1 >= size) break;
        
        // Append component to path
        strncpy(buffer + pos, name, name_len);
        pos += name_len;
        buffer[pos++] = '/';
    }
    
    // Remove trailing slash if not root
    if (pos > 1) pos--;
    buffer[pos] = '\0';  // Null terminate string
    
    return buffer;
}