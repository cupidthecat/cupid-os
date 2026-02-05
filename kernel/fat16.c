/**
 * FAT16 Filesystem Implementation
 *
 * Implements FAT16 filesystem with MBR partition support.
 * Provides file operations: open, read, close, list directory.
 *
 * Limitations:
 * - Root directory only (no subdirectories)
 * - Read-only initially (write support future)
 * - 8.3 filenames only
 * - First partition only
 */

#include "fat16.h"
#include "blockcache.h"
#include "kernel.h"
#include "debug.h"
#include "string.h"

static fat16_fs_t fs;
static fat16_file_t open_files[8];
static int fat16_initialized = 0;

/**
 * fat16_read_fat_entry - Read FAT table entry
 *
 * @param cluster: Cluster number
 * @return FAT entry value
 */
static uint16_t fat16_read_fat_entry(uint16_t cluster) {
    if (cluster < 2) {
        return 0;
    }

    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sector = fs.fat_start + (fat_offset / fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    uint8_t buffer[512];
    if (blockcache_read(fat_sector, buffer) != 0) {
        print("FAT16: cannot read FAT\n");
        return 0xFFFF;
    }

    uint16_t entry = *(uint16_t*)(&buffer[entry_offset]);
    return entry;
}

/**
 * fat16_cluster_to_lba - Convert cluster number to LBA
 *
 * @param cluster: Cluster number
 * @return LBA sector address
 */
static uint32_t fat16_cluster_to_lba(uint16_t cluster) {
    if (cluster < 2) {
        return 0;
    }
    return fs.data_start + ((uint32_t)(cluster - 2) * fs.sectors_per_cluster);
}

/**
 * fat16_filename_to_83 - Convert filename to 8.3 format
 *
 * Converts "readme.txt" to "README  TXT"
 *
 * @param input: Input filename
 * @param output: Output buffer (11 bytes)
 */
static void fat16_filename_to_83(const char* input, char* output) {
    int i;

    // Initialize with spaces
    for (i = 0; i < 11; i++) {
        output[i] = ' ';
    }

    // Copy filename part (up to 8 chars before '.')
    i = 0;
    while (*input && *input != '.' && i < 8) {
        if (*input >= 'a' && *input <= 'z') {
            output[i] = (char)(*input - 'a' + 'A');
        } else {
            output[i] = *input;
        }
        input++;
        i++;
    }

    // Skip to extension
    while (*input && *input != '.') {
        input++;
    }
    if (*input == '.') {
        input++;
    }

    // Copy extension (up to 3 chars)
    i = 8;
    while (*input && i < 11) {
        if (*input >= 'a' && *input <= 'z') {
            output[i] = (char)(*input - 'a' + 'A');
        } else {
            output[i] = *input;
        }
        input++;
        i++;
    }
}

/**
 * fat16_init - Initialize FAT16 filesystem
 *
 * @return 0 on success, -1 on error
 */
int fat16_init(void) {
    // Read MBR
    uint8_t mbr_buffer[512];
    if (blockcache_read(0, mbr_buffer) != 0) {
        print("FAT16: cannot read MBR\n");
        return -1;
    }

    mbr_t* mbr = (mbr_t*)mbr_buffer;

    // Check signature
    if (mbr->signature != 0xAA55) {
        print("FAT16: invalid MBR signature\n");
        return -1;
    }

    // Find first FAT16 partition
    mbr_partition_t* part = NULL;
    for (int i = 0; i < 4; i++) {
        if (mbr->partitions[i].type == FAT16_TYPE_1 ||
            mbr->partitions[i].type == FAT16_TYPE_2 ||
            mbr->partitions[i].type == FAT16_TYPE_3) {
            part = &mbr->partitions[i];
            break;
        }
    }

    if (!part) {
        print("FAT16: no FAT16 partition found\n");
        return -1;
    }

    fs.partition_lba = part->lba_start;

    // Read boot sector
    uint8_t boot_buffer[512];
    if (blockcache_read(fs.partition_lba, boot_buffer) != 0) {
        print("FAT16: cannot read boot sector\n");
        return -1;
    }

    fat16_boot_sector_t* boot = (fat16_boot_sector_t*)boot_buffer;

    // Parse boot sector
    fs.bytes_per_sector = boot->bytes_per_sector;
    fs.sectors_per_cluster = boot->sectors_per_cluster;
    fs.reserved_sectors = boot->reserved_sectors;
    fs.num_fats = boot->num_fats;
    fs.root_dir_entries = boot->root_dir_entries;
    fs.sectors_per_fat = boot->sectors_per_fat;
    fs.total_sectors = boot->total_sectors_16 ? (uint32_t)boot->total_sectors_16 : boot->total_sectors_32;

    // Calculate layout
    fs.fat_start = fs.partition_lba + fs.reserved_sectors;
    fs.root_dir_start = fs.fat_start + ((uint32_t)fs.num_fats * fs.sectors_per_fat);
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    fs.data_start = fs.root_dir_start + root_dir_sectors;

    // Initialize open files
    for (int i = 0; i < 8; i++) {
        open_files[i].is_open = 0;
    }

    fat16_initialized = 1;

    print("FAT16 filesystem initialized\n");
    debug_print_int("  Partition LBA: ", fs.partition_lba);
    debug_print_int("  FAT start: ", fs.fat_start);
    debug_print_int("  Root dir start: ", fs.root_dir_start);
    debug_print_int("  Data start: ", fs.data_start);
    debug_print_int("  Sectors/cluster: ", fs.sectors_per_cluster);

    return 0;
}

/**
 * fat16_open - Open a file
 *
 * @param filename: Filename to open
 * @return File handle or NULL on error
 */
fat16_file_t* fat16_open(const char* filename) {
    if (!fat16_initialized) {
        print("No FAT16 filesystem mounted\n");
        return NULL;
    }

    // Convert filename to 8.3 format
    char name83[11];
    fat16_filename_to_83(filename, name83);

    // Search root directory
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) {
            return NULL;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) {
                return NULL;
            }

            if ((unsigned char)entries[i].filename[0] == 0xE5) {
                continue;
            }

            // Skip volume labels and directories
            if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) {
                continue;
            }

            // Compare filename
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                // Found file - allocate file descriptor
                for (int j = 0; j < 8; j++) {
                    if (!open_files[j].is_open) {
                        open_files[j].first_cluster = entries[i].first_cluster;
                        open_files[j].file_size = entries[i].file_size;
                        open_files[j].position = 0;
                        open_files[j].is_open = 1;
                        return &open_files[j];
                    }
                }
                print("FAT16: too many open files\n");
                return NULL;
            }
        }
    }

    return NULL;
}

/**
 * fat16_read - Read from file
 *
 * @param file: File handle
 * @param buffer: Buffer to read into
 * @param count: Number of bytes to read
 * @return Bytes read, or -1 on error
 */
int fat16_read(fat16_file_t* file, void* buffer, uint32_t count) {
    if (!file || !file->is_open) {
        return -1;
    }

    // Clamp to file size
    if (file->position + count > file->file_size) {
        count = file->file_size - file->position;
    }

    if (count == 0) {
        return 0;
    }

    uint32_t bytes_read = 0;
    uint16_t current_cluster = file->first_cluster;
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * fs.bytes_per_sector;

    // Skip to current position's cluster
    uint32_t skip_bytes = file->position;
    while (skip_bytes >= cluster_size) {
        current_cluster = fat16_read_fat_entry(current_cluster);
        if (current_cluster >= FAT16_EOC_MIN) {
            return (int)bytes_read;
        }
        skip_bytes -= cluster_size;
    }

    // Read data
    uint8_t sector_buffer[512];
    while (bytes_read < count) {
        uint32_t cluster_lba = fat16_cluster_to_lba(current_cluster);
        uint32_t offset_in_cluster = (file->position + bytes_read) % cluster_size;
        uint32_t sector_in_cluster = offset_in_cluster / fs.bytes_per_sector;
        uint32_t offset_in_sector = offset_in_cluster % fs.bytes_per_sector;

        if (blockcache_read(cluster_lba + sector_in_cluster, sector_buffer) != 0) {
            return -1;
        }

        uint32_t bytes_to_copy = fs.bytes_per_sector - offset_in_sector;
        if (bytes_to_copy > count - bytes_read) {
            bytes_to_copy = count - bytes_read;
        }

        memcpy((uint8_t*)buffer + bytes_read, sector_buffer + offset_in_sector, bytes_to_copy);
        bytes_read += bytes_to_copy;

        // Check if we need to move to next cluster
        if ((offset_in_cluster + bytes_to_copy) >= cluster_size && bytes_read < count) {
            current_cluster = fat16_read_fat_entry(current_cluster);
            if (current_cluster >= FAT16_EOC_MIN) {
                break;
            }
        }
    }

    file->position += bytes_read;
    return (int)bytes_read;
}

/**
 * fat16_close - Close file
 *
 * @param file: File handle
 * @return 0 on success
 */
int fat16_close(fat16_file_t* file) {
    if (!file) {
        return -1;
    }
    file->is_open = 0;
    return 0;
}

/**
 * fat16_list_root - List root directory
 *
 * @return Number of files listed
 */
int fat16_list_root(void) {
    if (!fat16_initialized) {
        print("No FAT16 filesystem mounted\n");
        return -1;
    }

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    int file_count = 0;

    print("Files in /disk:\n");

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) {
            return -1;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) {
                return file_count;
            }

            if ((unsigned char)entries[i].filename[0] == 0xE5) {
                continue;
            }

            // Skip volume labels and directories
            if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) {
                continue;
            }

            // Print filename
            print("  ");
            for (int j = 0; j < 8; j++) {
                if (entries[i].filename[j] != ' ') {
                    putchar(entries[i].filename[j]);
                }
            }

            if (entries[i].ext[0] != ' ') {
                putchar('.');
                for (int j = 0; j < 3; j++) {
                    if (entries[i].ext[j] != ' ') {
                        putchar(entries[i].ext[j]);
                    }
                }
            }

            print(" (");
            print_int(entries[i].file_size);
            print(" bytes)\n");

            file_count++;
        }
    }

    return file_count;
}
