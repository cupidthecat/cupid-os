/**
 * FAT16 Filesystem Implementation
 *
 * Implements FAT16 filesystem with MBR partition support.
 * Provides file operations: open, read, close, list directory, write.
 *
 * Limitations:
 * - Root directory only (no subdirectories)
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

/* Output function pointers (can be overridden) */
static void (*fat16_print)(const char*) = print;
static void (*fat16_putchar)(char) = putchar;
static void (*fat16_print_int)(uint32_t) = print_int;

void fat16_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char), void (*print_int_fn)(uint32_t)) {
    if (print_fn) fat16_print = print_fn;
    if (putchar_fn) fat16_putchar = putchar_fn;
    if (print_int_fn) fat16_print_int = print_int_fn;
}

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

/* ══════════════════════════════════════════════════════════════════════
 *  FAT16 Write Support
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * fat16_write_fat_entry - Write a FAT table entry
 *
 * Writes to all FAT copies to keep them in sync.
 *
 * @param cluster: Cluster number
 * @param value: Value to write
 * @return 0 on success, -1 on error
 */
static int fat16_write_fat_entry(uint16_t cluster, uint16_t value) {
    if (cluster < 2) return -1;

    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t sector_offset = fat_offset / fs.bytes_per_sector;
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    /* Write to each FAT copy */
    for (uint8_t fat_num = 0; fat_num < fs.num_fats; fat_num++) {
        uint32_t fat_sector = fs.fat_start +
            ((uint32_t)fat_num * fs.sectors_per_fat) + sector_offset;

        uint8_t buffer[512];
        if (blockcache_read(fat_sector, buffer) != 0) return -1;

        *(uint16_t*)(&buffer[entry_offset]) = value;

        if (blockcache_write(fat_sector, buffer) != 0) return -1;
    }
    return 0;
}

/**
 * fat16_alloc_cluster - Allocate a free cluster from the FAT
 *
 * Scans the FAT for a free entry (value 0x0000), marks it as end-of-chain,
 * and returns the cluster number.
 *
 * @return Cluster number (>= 2) on success, 0 on failure (disk full)
 */
static uint16_t fat16_alloc_cluster(void) {
    /* Calculate total data clusters */
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
        fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    uint32_t data_sectors = fs.total_sectors -
        (fs.reserved_sectors + (uint32_t)fs.num_fats * fs.sectors_per_fat +
         root_dir_sectors);
    uint32_t total_clusters = data_sectors / fs.sectors_per_cluster;

    /* Cluster numbers start at 2 */
    for (uint16_t c = 2; c < (uint16_t)(total_clusters + 2); c++) {
        uint16_t entry = fat16_read_fat_entry(c);
        if (entry == FAT16_FREE) {
            /* Mark as end-of-chain */
            if (fat16_write_fat_entry(c, FAT16_EOC_MAX) != 0) return 0;
            return c;
        }
    }
    return 0; /* No free clusters */
}

/**
 * fat16_free_chain - Free a cluster chain starting at the given cluster
 *
 * Follows the FAT chain and marks each cluster as free.
 *
 * @param cluster: Starting cluster of chain
 */
static void fat16_free_chain(uint16_t cluster) {
    while (cluster >= 2 && cluster < FAT16_EOC_MIN &&
           cluster != FAT16_BAD_CLUSTER) {
        uint16_t next = fat16_read_fat_entry(cluster);
        fat16_write_fat_entry(cluster, FAT16_FREE);
        cluster = next;
    }
}

/**
 * fat16_write_file - Write (create or overwrite) a file in the root directory
 *
 * If the file already exists, its old cluster chain is freed and replaced.
 * If it doesn't exist, a new directory entry is created.
 *
 * @param filename: 8.3 filename (e.g. "README.TXT")
 * @param data: File content buffer
 * @param size: Number of bytes to write
 * @return Bytes written on success, -1 on error
 */
int fat16_write_file(const char* filename, const void* data, uint32_t size) {
    if (!fat16_initialized) {
        print("No FAT16 filesystem mounted\n");
        return -1;
    }

    /* Convert filename to 8.3 format */
    char name83[11];
    fat16_filename_to_83(filename, name83);

    /* ── Allocate cluster chain for the new data ── */
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * fs.bytes_per_sector;
    uint32_t clusters_needed = 0;
    if (size > 0) {
        clusters_needed = (size + cluster_size - 1) / cluster_size;
    }

    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t c = fat16_alloc_cluster();
        if (c == 0) {
            /* Disk full - free any clusters we already allocated */
            if (first_cluster) fat16_free_chain(first_cluster);
            print("FAT16: disk full\n");
            return -1;
        }
        if (i == 0) {
            first_cluster = c;
        } else {
            /* Link previous cluster to this one */
            if (fat16_write_fat_entry(prev_cluster, c) != 0) {
                fat16_free_chain(first_cluster);
                return -1;
            }
        }
        prev_cluster = c;
    }

    /* ── Write file data to the allocated clusters ── */
    {
        uint16_t cur_cluster = first_cluster;
        uint32_t bytes_written = 0;

        while (bytes_written < size && cur_cluster >= 2 &&
               cur_cluster < FAT16_EOC_MIN) {
            uint32_t cluster_lba = fat16_cluster_to_lba(cur_cluster);

            for (uint8_t s = 0; s < fs.sectors_per_cluster && bytes_written < size; s++) {
                uint8_t sector_buf[512];
                memset(sector_buf, 0, 512);

                uint32_t to_copy = size - bytes_written;
                if (to_copy > fs.bytes_per_sector)
                    to_copy = fs.bytes_per_sector;

                memcpy(sector_buf, (const uint8_t*)data + bytes_written, to_copy);

                if (blockcache_write(cluster_lba + (uint32_t)s, sector_buf) != 0) {
                    print("FAT16: write failed\n");
                    return -1;
                }
                bytes_written += to_copy;
            }

            cur_cluster = fat16_read_fat_entry(cur_cluster);
        }
    }

    /* ── Find or create directory entry ── */
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
        fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    int found = 0;
    int free_entry_sector = -1;
    int free_entry_index = -1;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0)
            return -1;

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (int i = 0; i < 16; i++) {
            /* End of directory? */
            if (entries[i].filename[0] == 0x00) {
                if (free_entry_sector < 0) {
                    free_entry_sector = (int)sector;
                    free_entry_index = i;
                }
                goto dir_search_done;
            }

            /* Deleted entry - remember as free slot */
            if ((unsigned char)entries[i].filename[0] == 0xE5) {
                if (free_entry_sector < 0) {
                    free_entry_sector = (int)sector;
                    free_entry_index = i;
                }
                continue;
            }

            /* Skip volume labels and directories */
            if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY))
                continue;

            /* Compare filename */
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                /* Existing file - free old cluster chain */
                if (entries[i].first_cluster >= 2)
                    fat16_free_chain(entries[i].first_cluster);

                /* Update entry */
                entries[i].first_cluster = first_cluster;
                entries[i].file_size = size;
                entries[i].attributes = FAT_ATTR_ARCHIVE;

                if (blockcache_write(fs.root_dir_start + sector, buffer) != 0)
                    return -1;

                found = 1;
                break;
            }
        }
        if (found) break;
    }

dir_search_done:
    if (!found) {
        /* Create new directory entry */
        if (free_entry_sector < 0) {
            print("FAT16: root directory full\n");
            /* Free the clusters we allocated */
            if (first_cluster) fat16_free_chain(first_cluster);
            return -1;
        }

        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + (uint32_t)free_entry_sector,
                           buffer) != 0)
            return -1;

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        fat16_dir_entry_t* entry = &entries[free_entry_index];

        /* Zero out the entry */
        memset(entry, 0, sizeof(fat16_dir_entry_t));

        /* Set filename */
        for (int j = 0; j < 8; j++) entry->filename[j] = name83[j];
        for (int j = 0; j < 3; j++) entry->ext[j] = name83[8 + j];

        entry->attributes = FAT_ATTR_ARCHIVE;
        entry->first_cluster = first_cluster;
        entry->file_size = size;

        /* If this was the end-of-directory marker, write a new end marker
         * in the next slot (if within same sector) */
        if (entries[free_entry_index].filename[0] != (char)0xE5) {
            /* Was a 0x00 entry, need to put a new 0x00 terminator after */
            if (free_entry_index + 1 < 16) {
                if (entries[free_entry_index + 1].filename[0] != 0x00 &&
                    (unsigned char)entries[free_entry_index + 1].filename[0] != 0xE5) {
                    /* There's already data after, no need for terminator */
                } else {
                    entries[free_entry_index + 1].filename[0] = 0x00;
                }
            }
        }

        /* Write the entry back (it was zeroed above, set filename now) */
        for (int j = 0; j < 8; j++) entry->filename[j] = name83[j];
        for (int j = 0; j < 3; j++) entry->ext[j] = name83[8 + j];
        entry->attributes = FAT_ATTR_ARCHIVE;
        entry->first_cluster = first_cluster;
        entry->file_size = size;

        if (blockcache_write(fs.root_dir_start + (uint32_t)free_entry_sector,
                            buffer) != 0)
            return -1;
    }

    /* Flush the cache to ensure everything is on disk */
    blockcache_sync();

    return (int)size;
}

/**
 * fat16_delete_file - Delete a file from the root directory
 *
 * Marks the directory entry as deleted (0xE5) and frees the cluster chain.
 *
 * @param filename: 8.3 filename to delete
 * @return 0 on success, -1 on error
 */
int fat16_delete_file(const char* filename) {
    if (!fat16_initialized) return -1;

    char name83[11];
    fat16_filename_to_83(filename, name83);

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
        fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0)
            return -1;

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) return -1; /* End of dir */
            if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
            if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY))
                continue;

            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                /* Free cluster chain */
                if (entries[i].first_cluster >= 2)
                    fat16_free_chain(entries[i].first_cluster);

                /* Mark entry as deleted */
                entries[i].filename[0] = (char)0xE5;

                if (blockcache_write(fs.root_dir_start + sector, buffer) != 0)
                    return -1;

                blockcache_sync();
                return 0;
            }
        }
    }
    return -1;
}

/**
 * fat16_list_root - List root directory
 *
 * @return Number of files listed
 */
int fat16_list_root(void) {
    if (!fat16_initialized) {
        fat16_print("No FAT16 filesystem mounted\n");
        return -1;
    }

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    int file_count = 0;

    fat16_print("Files in /disk:\n");

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
            fat16_print("  ");
            for (int j = 0; j < 8; j++) {
                if (entries[i].filename[j] != ' ') {
                    fat16_putchar(entries[i].filename[j]);
                }
            }

            if (entries[i].ext[0] != ' ') {
                fat16_putchar('.');
                for (int j = 0; j < 3; j++) {
                    if (entries[i].ext[j] != ' ') {
                        fat16_putchar(entries[i].ext[j]);
                    }
                }
            }

            fat16_print(" (");
            fat16_print_int(entries[i].file_size);
            fat16_print(" bytes)\n");

            file_count++;
        }
    }

    return file_count;
}

/**
 * fat16_enumerate_root - Enumerate root directory entries via callback
 *
 * Calls the callback for each valid file/directory entry in the root
 * directory.  The callback receives the human-readable filename (with
 * extension), size, attributes, and user context pointer.
 *
 * @param callback: Function called for each entry
 * @param ctx:      Opaque pointer forwarded to callback
 * @return Number of entries enumerated, or -1 on error
 */
int fat16_enumerate_root(fat16_enum_callback_t callback, void *ctx) {
    if (!fat16_initialized || !callback) {
        return -1;
    }

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
                                 fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    int count = 0;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) {
            return -1;
        }

        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) {
                return count;  /* End of directory */
            }
            if ((unsigned char)entries[i].filename[0] == 0xE5) {
                continue;  /* Deleted entry */
            }
            /* Skip volume labels */
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) {
                continue;
            }

            /* Build human-readable name: "FILENAME.EXT" */
            char name[13];
            int pos = 0;

            /* Copy base name, trim trailing spaces */
            for (int j = 0; j < 8; j++) {
                if (entries[i].filename[j] != ' ') {
                    char ch = entries[i].filename[j];
                    /* Convert to lowercase for display */
                    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                    name[pos++] = ch;
                }
            }

            /* Append extension if present */
            if (entries[i].ext[0] != ' ') {
                name[pos++] = '.';
                for (int j = 0; j < 3; j++) {
                    if (entries[i].ext[j] != ' ') {
                        char ch = entries[i].ext[j];
                        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                        name[pos++] = ch;
                    }
                }
            }
            name[pos] = '\0';

            int ret = callback(name, entries[i].file_size,
                               entries[i].attributes, ctx);
            count++;
            if (ret != 0) return count;
        }
    }

    return count;
}
