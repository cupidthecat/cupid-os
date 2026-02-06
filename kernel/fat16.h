#ifndef FAT16_H
#define FAT16_H

#include "types.h"
#include "blockdev.h"

// MBR partition types
#define FAT16_TYPE_1 0x04  // FAT16 < 32MB
#define FAT16_TYPE_2 0x06  // FAT16 >= 32MB
#define FAT16_TYPE_3 0x0E  // FAT16 LBA

// FAT entry values
#define FAT16_FREE         0x0000
#define FAT16_BAD_CLUSTER  0xFFF7
#define FAT16_EOC_MIN      0xFFF8
#define FAT16_EOC_MAX      0xFFFF

// File attributes
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20

#pragma pack(push, 1)

typedef struct {
    uint8_t status;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} mbr_partition_t;

typedef struct {
    uint8_t boot_code[446];
    mbr_partition_t partitions[4];
    uint16_t signature;
} mbr_t;

typedef struct {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_dir_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat16_boot_sector_t;

typedef struct {
    char filename[8];
    char ext[3];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster;
    uint32_t file_size;
} fat16_dir_entry_t;

#pragma pack(pop)

typedef struct {
    uint32_t partition_lba;
    uint32_t fat_start;
    uint32_t root_dir_start;
    uint32_t data_start;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_dir_entries;
    uint32_t total_sectors;
    uint16_t sectors_per_fat;
} fat16_fs_t;

typedef struct {
    uint16_t first_cluster;
    uint32_t file_size;
    uint32_t position;
    uint8_t is_open;
} fat16_file_t;

// Callback for enumerating directory entries
// Returns: 0 to continue, non-zero to stop
typedef int (*fat16_enum_callback_t)(const char *name, uint32_t size,
                                     uint8_t attr, void *ctx);

// Public API
int fat16_init(void);
fat16_file_t* fat16_open(const char* filename);
int fat16_read(fat16_file_t* file, void* buffer, uint32_t count);
int fat16_close(fat16_file_t* file);
int fat16_list_root(void);
int fat16_write_file(const char* filename, const void* data, uint32_t size);
int fat16_delete_file(const char* filename);
void fat16_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char), void (*print_int_fn)(uint32_t));
int fat16_enumerate_root(fat16_enum_callback_t callback, void *ctx);

#endif
