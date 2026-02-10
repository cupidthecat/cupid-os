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
#include "../drivers/serial.h"

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

/* ── Subdirectory path helpers ────────────────────────────────────── */

/* Split "dir/file" into dir_out and name_out.
 * Returns 1 if a slash was found, 0 if flat name.
 * Handles only one level of subdirectory. */
static int fat16_split_path(const char *path, char *dir_out, char *name_out) {
    int slash = -1;
    int i;
    for (i = 0; path[i]; i++) {
        if (path[i] == '/') slash = i;
    }
    if (slash < 0) {
        dir_out[0] = '\0';
        for (i = 0; path[i] && i < 63; i++) name_out[i] = path[i];
        name_out[i] = '\0';
        return 0;
    }
    for (i = 0; i < slash && i < 63; i++) dir_out[i] = path[i];
    dir_out[i] = '\0';
    int j = 0;
    for (i = slash + 1; path[i] && j < 63; i++, j++) name_out[j] = path[i];
    name_out[j] = '\0';
    return 1;
}

/* Return the first cluster of a directory named dirname (in the root dir),
 * or 0 if not found. */
static uint16_t fat16_get_dir_cluster(const char *dirname) {
    char name83[11];
    fat16_filename_to_83(dirname, name83);
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
                                  fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buf[512];
        if (blockcache_read(fs.root_dir_start + sector, buf) != 0) return 0;
        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buf;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) return 0;
            if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
            if (!(entries[i].attributes & FAT_ATTR_DIRECTORY)) continue;
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) { match = 0; break; }
            }
            if (match) return entries[i].first_cluster;
        }
    }
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

    /* Handle subdirectory path (e.g., "asm/hello.txt") */
    {
        char dir_part[64], name_part[64];
        if (fat16_split_path(filename, dir_part, name_part) && dir_part[0]) {
            uint16_t dir_cluster = fat16_get_dir_cluster(dir_part);
            if (!dir_cluster) return NULL;
            char name83s[11];
            fat16_filename_to_83(name_part, name83s);
            uint16_t cur = dir_cluster;
            while (cur >= 2 && cur < FAT16_EOC_MIN) {
                uint32_t lba = fat16_cluster_to_lba(cur);
                for (uint32_t s = 0; s < (uint32_t)fs.sectors_per_cluster; s++) {
                    uint8_t buf[512];
                    if (blockcache_read(lba + s, buf) != 0) return NULL;
                    fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buf;
                    for (int i = 0; i < 16; i++) {
                        if (entries[i].filename[0] == 0x00) return NULL;
                        if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
                        if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) continue;
                        int match = 1;
                        for (int j = 0; j < 11; j++) {
                            if (entries[i].filename[j] != name83s[j]) { match = 0; break; }
                        }
                        if (match) {
                            for (int j = 0; j < 8; j++) {
                                if (!open_files[j].is_open) {
                                    open_files[j].first_cluster = entries[i].first_cluster;
                                    open_files[j].file_size = entries[i].file_size;
                                    open_files[j].position = 0;
                                    open_files[j].is_open = 1;
                                    return &open_files[j];
                                }
                            }
                            return NULL; /* Too many open files */
                        }
                    }
                }
                cur = fat16_read_fat_entry(cur);
            }
            return NULL;
        }
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
        serial_printf("[fat16_read] ERROR: invalid file handle\n");
        return -1;
    }

    // Clamp to file size
    if (file->position + count > file->file_size) {
        count = file->file_size - file->position;
    }

    if (count == 0) {
        return 0;
    }

    serial_printf("[fat16_read] pos=%u count=%u filesize=%u first_cluster=%u\n",
                 file->position, count, file->file_size, file->first_cluster);

    uint32_t bytes_read = 0;
    uint16_t current_cluster = file->first_cluster;
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * fs.bytes_per_sector;

    // Skip to current position's cluster
    uint32_t skip_bytes = file->position;
    uint32_t skipped_clusters = 0;
    while (skip_bytes >= cluster_size) {
        uint16_t next_cluster = fat16_read_fat_entry(current_cluster);
        if (next_cluster >= FAT16_EOC_MIN) {
            serial_printf("[READ SKIP] Hit EOC after %u clusters (wanted to skip %u bytes)\n",
                         skipped_clusters, file->position);
            return (int)bytes_read;
        }
        current_cluster = next_cluster;
        skip_bytes -= cluster_size;
        skipped_clusters++;
    }
    if (file->position > 0 && skipped_clusters > 0) {
        serial_printf("[READ] Skipped %u clusters to reach position %u, now at cluster %u\n",
                     skipped_clusters, file->position, current_cluster);
    }

    // Read data
    uint8_t sector_buffer[512];
    uint32_t clusters_read = 0;
    uint16_t last_logged_cluster = 0xFFFF;

    while (bytes_read < count) {
        uint32_t cluster_lba = fat16_cluster_to_lba(current_cluster);
        uint32_t offset_in_cluster = (file->position + bytes_read) % cluster_size;
        uint32_t sector_in_cluster = offset_in_cluster / fs.bytes_per_sector;
        uint32_t offset_in_sector = offset_in_cluster % fs.bytes_per_sector;

        /* Log each cluster we read from */
        if (current_cluster != last_logged_cluster) {
            if (clusters_read < 5 || clusters_read % 50 == 0) {
                serial_printf("[READ] reading cluster[%u]=%u → LBA %u-%u (offset_in_cluster=%u)\n",
                             clusters_read, current_cluster, cluster_lba,
                             cluster_lba + fs.sectors_per_cluster - 1, offset_in_cluster);
            }
            last_logged_cluster = current_cluster;
        }

        if (blockcache_read(cluster_lba + sector_in_cluster, sector_buffer) != 0) {
            serial_printf("[fat16_read] ERROR: blockcache_read failed at LBA %u\n",
                         cluster_lba + sector_in_cluster);
            return -1;
        }

        /* Debug: check if sector is all zeros */
        if ((bytes_read % 10240) == 0) {  /* Log every ~10KB */
            uint32_t zero_count = 0;
            for (uint32_t i = 0; i < 512; i++) {
                if (sector_buffer[i] == 0) zero_count++;
            }
            if (zero_count == 512) {
                serial_printf("[fat16_read] WARNING: sector at LBA %u is ALL ZEROS! cluster=%u\n",
                             cluster_lba + sector_in_cluster, current_cluster);
            }
        }

        uint32_t bytes_to_copy = fs.bytes_per_sector - offset_in_sector;
        if (bytes_to_copy > count - bytes_read) {
            bytes_to_copy = count - bytes_read;
        }

        memcpy((uint8_t*)buffer + bytes_read, sector_buffer + offset_in_sector, bytes_to_copy);
        bytes_read += bytes_to_copy;

        // Check if we need to move to next cluster
        if ((offset_in_cluster + bytes_to_copy) >= cluster_size && bytes_read < count) {
            uint16_t next_cluster = fat16_read_fat_entry(current_cluster);
            if (clusters_read < 5 || clusters_read % 50 == 0) {
                serial_printf("[READ] cluster %u → next %u (EOC if >= 0x%04X)\n",
                             current_cluster, next_cluster, FAT16_EOC_MIN);
            }
            current_cluster = next_cluster;
            clusters_read++;
            if (current_cluster >= FAT16_EOC_MIN) {
                serial_printf("[READ] Hit EOC after %u clusters, bytes_read=%u\n",
                             clusters_read, bytes_read);
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
    if (cluster < 2) {
        serial_printf("[fat16_write_fat_entry] ERROR: invalid cluster %u\n", cluster);
        return -1;
    }

    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t sector_offset = fat_offset / fs.bytes_per_sector;
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    /* Write to each FAT copy */
    for (uint8_t fat_num = 0; fat_num < fs.num_fats; fat_num++) {
        uint32_t fat_sector = fs.fat_start +
            ((uint32_t)fat_num * fs.sectors_per_fat) + sector_offset;

        uint8_t buffer[512];
        int read_rc = blockcache_read(fat_sector, buffer);
        if (read_rc != 0) {
            serial_printf("[fat16_write_fat_entry] ERROR: read FAT%u sector %u failed (rc=%d)\n",
                         fat_num, fat_sector, read_rc);
            return -1;
        }

        *(uint16_t*)(&buffer[entry_offset]) = value;

        int write_rc = blockcache_write(fat_sector, buffer);
        if (write_rc != 0) {
            serial_printf("[fat16_write_fat_entry] ERROR: write FAT%u sector %u failed (rc=%d)\n",
                         fat_num, fat_sector, write_rc);
            return -1;
        }
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
    serial_printf("[fat16_alloc_cluster] DISK FULL: no free clusters (total=%u)\n",
                 total_clusters);
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
    serial_printf("[fat16_write_file] START: filename='%s' size=%u\n",
                 filename ? filename : "(null)", size);

    if (!fat16_initialized) {
        serial_printf("[fat16_write_file] ERROR: not initialized\n");
        print("No FAT16 filesystem mounted\n");
        return -1;
    }

    /* Convert filename to 8.3 format */
    char name83[11];
    fat16_filename_to_83(filename, name83);
    serial_printf("[fat16_write_file] name83='%c%c%c%c%c%c%c%c.%c%c%c'\n",
                 name83[0], name83[1], name83[2], name83[3], name83[4], name83[5], name83[6], name83[7],
                 name83[8], name83[9], name83[10]);

    /* ── Allocate cluster chain for the new data ── */
    uint32_t cluster_size = (uint32_t)fs.sectors_per_cluster * fs.bytes_per_sector;
    uint32_t clusters_needed = 0;
    if (size > 0) {
        clusters_needed = (size + cluster_size - 1) / cluster_size;
    }

    serial_printf("[fat16_write_file] size=%u cluster_size=%u clusters_needed=%u\n",
                 size, cluster_size, clusters_needed);

    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint16_t c = fat16_alloc_cluster();
        if (c == 0) {
            /* Disk full - free any clusters we already allocated */
            if (first_cluster) fat16_free_chain(first_cluster);
            serial_printf("[fat16_write_file] alloc failed at cluster %u/%u\n", i, clusters_needed);
            print("FAT16: disk full\n");
            return -1;
        }
        if (i == 0) {
            first_cluster = c;
            serial_printf("[ALLOC] cluster[0]=%u (first)\n", c);
        } else {
            /* Link previous cluster to this one */
            if (i < 5 || i % 50 == 0 || i >= clusters_needed - 5) {
                serial_printf("[ALLOC] cluster[%u]=%u, linking [%u]=%u → %u\n",
                             i, c, i-1, prev_cluster, c);
            }
            int link_rc = fat16_write_fat_entry(prev_cluster, c);
            if (link_rc != 0) {
                serial_printf("[ALLOC] ERROR: link failed at cluster[%u]: FAT[%u]=%u returned %d\n",
                             i, prev_cluster, c, link_rc);
                fat16_free_chain(first_cluster);
                return -1;
            }
        }
        prev_cluster = c;
    }

    /* Mark the last cluster with EOC marker */
    if (clusters_needed > 0) {
        serial_printf("[ALLOC] Setting EOC: FAT[%u]=%u (cluster[%u])\n",
                     prev_cluster, FAT16_EOC_MAX, clusters_needed - 1);
        int eoc_rc = fat16_write_fat_entry(prev_cluster, FAT16_EOC_MAX);
        if (eoc_rc != 0) {
            serial_printf("[ALLOC] ERROR: EOC write failed! FAT[%u]=0x%04X returned %d\n",
                         prev_cluster, FAT16_EOC_MAX, eoc_rc);
            fat16_free_chain(first_cluster);
            return -1;
        }
        serial_printf("[ALLOC] EOC marker set successfully\n");
    }

    /* Flush FAT writes to ensure chain is on disk */
    blockcache_sync();

    /* Verify FAT chain integrity */
    {
        serial_printf("[fat16_write_file] Verifying FAT chain: first=%u\n", first_cluster);
        uint16_t verify_cluster = first_cluster;
        uint32_t chain_len = 0;
        while (verify_cluster >= 2 && verify_cluster < FAT16_EOC_MIN && chain_len < clusters_needed + 10) {
            uint16_t next = fat16_read_fat_entry(verify_cluster);
            if (chain_len < 5 || chain_len % 50 == 0) {
                serial_printf("[VERIFY] chain[%u] = %u → %u (EOC if >= 0x%04X)\n",
                             chain_len, verify_cluster, next, FAT16_EOC_MIN);
            }
            verify_cluster = next;
            chain_len++;
        }
        serial_printf("[VERIFY] Chain length: %u (expected %u)\n", chain_len, clusters_needed);
        if (chain_len != clusters_needed) {
            serial_printf("[VERIFY] ERROR: Chain length mismatch!\n");
            return -1;
        }
    }

    /* ── Write file data to the allocated clusters ── */
    {
        uint16_t cur_cluster = first_cluster;
        uint32_t bytes_written = 0;
        uint32_t clusters_written = 0;

        /* Write to ALL allocated clusters (determined by clusters_needed) */
        while (clusters_written < clusters_needed && cur_cluster >= 2 &&
               cur_cluster < FAT16_EOC_MIN) {
            uint32_t cluster_lba = fat16_cluster_to_lba(cur_cluster);

            /* Log first few and some later clusters */
            if (clusters_written < 5 || clusters_written % 50 == 0) {
                serial_printf("[WRITE] cluster[%u]=%u → LBA %u-%u\n",
                             clusters_written, cur_cluster, cluster_lba,
                             cluster_lba + fs.sectors_per_cluster - 1);
            }

            /* Write ALL sectors in this cluster, even if past EOF */
            for (uint8_t s = 0; s < fs.sectors_per_cluster; s++) {
                uint8_t sector_buf[512];
                memset(sector_buf, 0, 512);

                /* Only copy data if we haven't written everything yet */
                if (bytes_written < size) {
                    uint32_t to_copy = size - bytes_written;
                    if (to_copy > fs.bytes_per_sector)
                        to_copy = fs.bytes_per_sector;

                    memcpy(sector_buf, (const uint8_t*)data + bytes_written, to_copy);
                    bytes_written += to_copy;
                }
                /* If bytes_written >= size, sector_buf remains all zeros */

                if (blockcache_write(cluster_lba + (uint32_t)s, sector_buf) != 0) {
                    print("FAT16: write failed\n");
                    return -1;
                }
            }

            clusters_written++;

            /* DIAGNOSTIC: Flush after EVERY cluster to force write-through */
            if (clusters_written % 10 == 0) {
                serial_printf("[WRITE] Flushing cache at cluster %u/%u\n",
                             clusters_written, clusters_needed);
            }
            blockcache_sync();

            uint16_t next_cluster = fat16_read_fat_entry(cur_cluster);
            if (clusters_written < 5 || clusters_written % 50 == 0) {
                serial_printf("[WRITE] cluster %u → next %u (EOC if >= 0x%04X)\n",
                             cur_cluster, next_cluster, FAT16_EOC_MIN);
            }
            cur_cluster = next_cluster;
        }

        /* Verify all data was written */
        serial_printf("[fat16_write_file] wrote %u clusters, %u bytes (size=%u)\n",
                     clusters_written, bytes_written, size);
        if (bytes_written < size) {
            print("FAT16: write incomplete - wrote ");
            debug_print_int("", bytes_written);
            print(" of ");
            debug_print_int("", size);
            print(" bytes\n");
            serial_printf("[fat16_write_file] INCOMPLETE: %u < %u\n", bytes_written, size);
            return -1;
        }

        /* Final flush to ensure all remaining data is on disk */
        serial_printf("[WRITE] Final flush after writing all %u clusters\n", clusters_written);
        blockcache_sync();
    }

    /* ── Find or create directory entry ── */

    /* Handle subdirectory path (e.g. "asm/hello.txt") */
    {
        char dir_part[64], name_part[64];
        if (fat16_split_path(filename, dir_part, name_part) && dir_part[0]) {
            uint16_t dir_cluster = fat16_get_dir_cluster(dir_part);
            if (!dir_cluster) {
                if (first_cluster) fat16_free_chain(first_cluster);
                return -1;
            }
            /* Rebuild 8.3 name from just the base filename */
            fat16_filename_to_83(name_part, name83);
            int sub_found = 0;
            uint16_t cur = dir_cluster;
            while (cur >= 2 && cur < FAT16_EOC_MIN && !sub_found) {
                uint32_t lba = fat16_cluster_to_lba(cur);
                for (uint32_t s = 0; s < (uint32_t)fs.sectors_per_cluster && !sub_found; s++) {
                    uint8_t buffer[512];
                    if (blockcache_read(lba + s, buffer) != 0) {
                        if (first_cluster) fat16_free_chain(first_cluster);
                        return -1;
                    }
                    fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
                    for (int i = 0; i < 16; i++) {
                        if (entries[i].filename[0] == 0x00) {
                            /* Create new entry here */
                            memset(&entries[i], 0, sizeof(fat16_dir_entry_t));
                            for (int j = 0; j < 8; j++) entries[i].filename[j] = name83[j];
                            for (int j = 0; j < 3; j++) entries[i].ext[j] = name83[8 + j];
                            entries[i].attributes = FAT_ATTR_ARCHIVE;
                            entries[i].first_cluster = first_cluster;
                            entries[i].file_size = size;
                            if (i + 1 < 16) entries[i + 1].filename[0] = 0x00;
                            blockcache_write(lba + s, buffer);
                            sub_found = 1;
                            break;
                        }
                        if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
                        if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
                        int match = 1;
                        for (int j = 0; j < 11; j++) {
                            if (entries[i].filename[j] != name83[j]) { match = 0; break; }
                        }
                        if (match) {
                            if (entries[i].first_cluster >= 2)
                                fat16_free_chain(entries[i].first_cluster);
                            entries[i].first_cluster = first_cluster;
                            entries[i].file_size = size;
                            entries[i].attributes = FAT_ATTR_ARCHIVE;
                            blockcache_write(lba + s, buffer);
                            sub_found = 1;
                            break;
                        }
                    }
                }
                cur = fat16_read_fat_entry(cur);
            }
            if (!sub_found) {
                if (first_cluster) fat16_free_chain(first_cluster);
                return -1;
            }
            blockcache_sync();
            return (int)size;
        }
    }

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
        fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    serial_printf("[fat16_write_file] searching directory: root_dir_start=%u sectors=%u\n",
                 fs.root_dir_start, root_dir_sectors);

    int found = 0;
    int free_entry_sector = -1;
    int free_entry_index = -1;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) {
            serial_printf("[fat16_write_file] ERROR: blockcache_read failed at sector %u\n",
                         fs.root_dir_start + sector);
            return -1;
        }

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
    serial_printf("[fat16_write_file] dir search done: found=%d free_sector=%d free_index=%d\n",
                 found, free_entry_sector, free_entry_index);

    if (!found) {
        /* Create new directory entry */
        if (free_entry_sector < 0) {
            serial_printf("[fat16_write_file] ERROR: root directory full\n");
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

    /* Handle subdirectory path */
    {
        char dir_part[64], name_part[64];
        if (fat16_split_path(filename, dir_part, name_part) && dir_part[0]) {
            uint16_t dir_cluster = fat16_get_dir_cluster(dir_part);
            if (!dir_cluster) return -1;
            char name83s[11];
            fat16_filename_to_83(name_part, name83s);
            uint16_t cur = dir_cluster;
            while (cur >= 2 && cur < FAT16_EOC_MIN) {
                uint32_t lba = fat16_cluster_to_lba(cur);
                for (uint32_t s = 0; s < (uint32_t)fs.sectors_per_cluster; s++) {
                    uint8_t buffer[512];
                    if (blockcache_read(lba + s, buffer) != 0) return -1;
                    fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
                    for (int i = 0; i < 16; i++) {
                        if (entries[i].filename[0] == 0x00) return -1;
                        if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
                        if (entries[i].attributes & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) continue;
                        int match = 1;
                        for (int j = 0; j < 11; j++) {
                            if (entries[i].filename[j] != name83s[j]) { match = 0; break; }
                        }
                        if (match) {
                            if (entries[i].first_cluster >= 2)
                                fat16_free_chain(entries[i].first_cluster);
                            entries[i].filename[0] = (char)0xE5;
                            blockcache_write(lba + s, buffer);
                            blockcache_sync();
                            return 0;
                        }
                    }
                }
                cur = fat16_read_fat_entry(cur);
            }
            return -1;
        }
    }

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
 * fat16_is_dir - Check if a name refers to a directory in the root dir
 *
 * @return 1 if it's a directory, 0 if not found or not a dir
 */
int fat16_is_dir(const char *dirname) {
    if (!fat16_initialized || !dirname || dirname[0] == '\0') return 0;

    char name83[11];
    fat16_filename_to_83(dirname, name83);

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
                                  fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) return 0;

        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) return 0;
            if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;
            if (!(entries[i].attributes & FAT_ATTR_DIRECTORY)) continue;

            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

/**
 * fat16_mkdir - Create a subdirectory in the FAT16 root directory
 *
 * Allocates a cluster, initialises it with '.' and '..' entries, and
 * writes a ATTR_DIRECTORY entry into the root directory.
 *
 * @param dirname: Directory name (8.3 style)
 * @return 0 on success, -1 on error
 */
int fat16_mkdir(const char *dirname) {
    if (!fat16_initialized || !dirname || dirname[0] == '\0') return -1;

    char name83[11];
    fat16_filename_to_83(dirname, name83);

    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
                                  fs.bytes_per_sector - 1) / fs.bytes_per_sector;

    /* First pass: check for existing entry with same name and find free slot */
    int free_sector = -1;
    int free_index  = -1;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + sector, buffer) != 0) return -1;

        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0x00) {
                /* End-of-directory marker — record free slot and stop scan */
                if (free_sector < 0) { free_sector = (int)sector; free_index = i; }
                goto scan_done;
            }
            if ((unsigned char)entries[i].filename[0] == 0xE5) {
                if (free_sector < 0) { free_sector = (int)sector; free_index = i; }
                continue;
            }
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;

            /* Check name match (file OR directory) */
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) { match = 0; break; }
            }
            if (match) return -1; /* Already exists */
        }
    }
scan_done:
    if (free_sector < 0) return -1; /* Root directory full */

    /* Allocate a cluster for the directory contents */
    uint16_t cluster = fat16_alloc_cluster();
    if (cluster == 0) return -1;

    /* Zero-fill the cluster */
    uint32_t lba = fat16_cluster_to_lba(cluster);
    {
        uint8_t zero[512];
        memset(zero, 0, 512);
        for (uint32_t s = 0; s < (uint32_t)fs.sectors_per_cluster; s++) {
            blockcache_write(lba + s, zero);
        }
    }

    /* Write '.' and '..' entries into the first sector of the cluster */
    {
        uint8_t first[512];
        memset(first, 0, 512);
        fat16_dir_entry_t *dot = (fat16_dir_entry_t *)first;

        /* '.' — points to this directory */
        memset(dot[0].filename, ' ', 8);
        memset(dot[0].ext,      ' ', 3);
        dot[0].filename[0] = '.';
        dot[0].attributes  = FAT_ATTR_DIRECTORY;
        dot[0].first_cluster = cluster;

        /* '..' — points to root (cluster 0 in FAT16 root) */
        memset(dot[1].filename, ' ', 8);
        memset(dot[1].ext,      ' ', 3);
        dot[1].filename[0] = '.';
        dot[1].filename[1] = '.';
        dot[1].attributes  = FAT_ATTR_DIRECTORY;
        dot[1].first_cluster = 0;

        blockcache_write(lba, first);
    }

    /* Write directory entry in root dir */
    {
        uint8_t buffer[512];
        if (blockcache_read(fs.root_dir_start + (uint32_t)free_sector, buffer) != 0) {
            fat16_free_chain(cluster);
            return -1;
        }
        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buffer;
        fat16_dir_entry_t *e = &entries[free_index];

        memset(e, 0, sizeof(fat16_dir_entry_t));
        for (int j = 0; j < 8; j++) e->filename[j] = name83[j];
        for (int j = 0; j < 3; j++) e->ext[j]      = name83[8 + j];
        e->attributes    = FAT_ATTR_DIRECTORY;
        e->first_cluster = cluster;
        e->file_size     = 0;

        if (blockcache_write(fs.root_dir_start + (uint32_t)free_sector, buffer) != 0) {
            fat16_free_chain(cluster);
            return -1;
        }
    }

    blockcache_sync();
    return 0;
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

/**
 * fat16_enumerate_subdir - Enumerate entries inside a subdirectory
 *
 * Finds the named directory in the root dir, then walks its cluster chain
 * calling the callback for each valid entry (skips '.' and '..').
 *
 * @param dirname:  Directory name (8.3 compatible)
 * @param callback: Called per entry
 * @param ctx:      Passed through to callback
 * @return Number of entries enumerated, -1 on error
 */
int fat16_enumerate_subdir(const char *dirname,
                           fat16_enum_callback_t callback, void *ctx) {
    if (!fat16_initialized || !dirname || !callback) return -1;

    char name83[11];
    fat16_filename_to_83(dirname, name83);

    /* Find the directory entry in root dir */
    uint32_t root_dir_sectors = ((uint32_t)fs.root_dir_entries * 32 +
                                  fs.bytes_per_sector - 1) / fs.bytes_per_sector;
    uint16_t dir_cluster = 0;
    int found = 0;

    for (uint32_t sector = 0; sector < root_dir_sectors && !found; sector++) {
        uint8_t buf[512];
        if (blockcache_read(fs.root_dir_start + sector, buf) != 0) return -1;

        fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buf;
        for (int i = 0; i < 16 && !found; i++) {
            if (entries[i].filename[0] == 0x00) goto search_done;
            if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
            if (!(entries[i].attributes & FAT_ATTR_DIRECTORY)) continue;
            if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;

            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entries[i].filename[j] != name83[j]) { match = 0; break; }
            }
            if (match) { dir_cluster = entries[i].first_cluster; found = 1; }
        }
    }
search_done:
    if (!found) return -1;

    /* Walk the cluster chain and enumerate entries */
    int count = 0;
    uint16_t cur = dir_cluster;

    while (cur >= 2 && cur < FAT16_EOC_MIN) {
        uint32_t lba = fat16_cluster_to_lba(cur);

        for (uint32_t s = 0; s < (uint32_t)fs.sectors_per_cluster; s++) {
            uint8_t buf2[512];
            if (blockcache_read(lba + s, buf2) != 0) return -1;

            fat16_dir_entry_t *entries = (fat16_dir_entry_t *)buf2;
            for (int i = 0; i < 16; i++) {
                if (entries[i].filename[0] == 0x00) return count;
                if ((unsigned char)entries[i].filename[0] == 0xE5) continue;
                if (entries[i].attributes & FAT_ATTR_VOLUME_ID) continue;

                /* Skip '.' and '..' */
                if (entries[i].filename[0] == '.' &&
                    (entries[i].filename[1] == ' ' ||
                     entries[i].filename[1] == '.'))
                    continue;

                /* Build human-readable name */
                char name[13];
                int pos = 0;
                for (int j = 0; j < 8; j++) {
                    if (entries[i].filename[j] != ' ') {
                        char ch = entries[i].filename[j];
                        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                        name[pos++] = ch;
                    }
                }
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

        cur = fat16_read_fat_entry(cur);
    }

    return count;
}
