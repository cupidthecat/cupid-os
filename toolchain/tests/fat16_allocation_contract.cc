/* The private include contains exact allocator/accessor bodies captured from
 * the active fat16.cc. Sector callbacks replace storage, not allocator logic. */
/* The Windows native oracle has a compiler-provided size_t. The kernel's
 * unrelated typedef is unused here and keeps a private name in this caller. */
#define size_t cupid_fat_size_t
#include "fat16.h"
#undef size_t
extern int printf(const char *, ...);

static fat16_fs_t fs;
static uint16_t tables[2][32768];
static uint32_t reads, writes, failed_read, failed_write;
static uint32_t outside_reads;
static void print(const char *text) { (void)text; }
static void serial_printf(const char *text, ...) { (void)text; }
static int blockcache_read(uint32_t lba, void *buffer) {
    unsigned char *out = (unsigned char *)buffer;
    const unsigned char *in;
    uint32_t i;
    reads++;
    if (failed_read == reads) return -1;
    if (lba < fs.fat_start || lba >= fs.fat_start + (uint32_t)fs.num_fats * fs.sectors_per_fat) {
        outside_reads++; return -1;
    }
    in = (const unsigned char *)tables + (lba - fs.fat_start) * 512u;
    for (i = 0; i < 512u; i++) out[i] = in[i];
    return 0;
}
static int blockcache_write(uint32_t lba, const void *buffer) {
    unsigned char *out;
    const unsigned char *in = (const unsigned char *)buffer;
    uint32_t i;
    writes++;
    if (failed_write == writes) return -1;
    if (lba < fs.fat_start || lba >= fs.fat_start + (uint32_t)fs.num_fats * fs.sectors_per_fat) return -1;
    out = (unsigned char *)tables + (lba - fs.fat_start) * 512u;
    for (i = 0; i < 512u; i++) out[i] = in[i];
    return 0;
}

#include "fat16_allocation_source.inc"

static void prepare(uint32_t used, uint32_t clusters) {
    uint32_t i, copy;
    for (copy = 0; copy < 2u; copy++)
        for (i = 0; i < 32768u; i++) tables[copy][i] = i < used ? FAT16_EOC_MAX : FAT16_FREE;
    fs.fat_start = 1u; fs.bytes_per_sector = 512u; fs.sectors_per_cluster = 8u;
    fs.reserved_sectors = 1u; fs.num_fats = 2u; fs.sectors_per_fat = 128u;
    fs.root_dir_entries = 512u; fs.total_sectors = 1u + 256u + 32u + clusters * 8u;
    reads = writes = failed_read = failed_write = outside_reads = 0u;
}
static int allocated(uint16_t expected) {
    uint16_t result = fat16_alloc_cluster();
    return result == expected && tables[0][expected] == FAT16_EOC_MAX &&
           tables[1][expected] == FAT16_EOC_MAX && outside_reads == 0u;
}
static int rejected_geometry(uint32_t mode) {
    prepare(2u, 15000u);
    if (mode == 0u) fs.bytes_per_sector = 0u;
    if (mode == 1u) fs.bytes_per_sector = 256u;
    if (mode == 2u) fs.sectors_per_cluster = 0u;
    if (mode == 3u) fs.num_fats = 0u;
    if (mode == 4u) fs.num_fats = 3u;
    if (mode == 5u) fs.sectors_per_fat = 0u;
    if (mode == 6u) fs.total_sectors = 1u;
    if (mode == 7u) fs.total_sectors = 0xffffffffu;
    if (mode == 8u) fs.sectors_per_fat = 1u;
    if (mode == 9u) fs.fat_start = 0xffffffffu;
    if (mode == 10u) fs.sectors_per_cluster = 3u;
    if (mode == 11u) fs.root_dir_entries = 0u;
    return fat16_alloc_cluster() == 0u && reads == 0u && writes == 0u;
}
int main(int argc, char **argv) {
    uint32_t mode, i, hash = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)tables;
    int ok = 0;
    if (argc != 2 || argv[1][0] < 'a' || argv[1][0] > 'm' || argv[1][1] != 0) return 1;
    mode = (uint32_t)(argv[1][0] - 'a');
    prepare(7049u, 15000u);
    if (mode == 0u) {
        ok = 1;
        for (i = 0u; i < 317u; i++) if (!allocated((uint16_t)(7049u + i))) ok = 0;
        ok = ok && reads <= 12000u && writes == 634u;
    } else if (mode == 1u) {
        tables[0][42] = tables[1][42] = tables[0][79] = tables[1][79] = FAT16_FREE;
        ok = allocated(42u) && allocated(79u);
    } else if (mode == 2u) {
        prepare(0u, 15000u); ok = allocated(2u);
        ok = ok && tables[0][0] == FAT16_FREE && tables[0][1] == FAT16_FREE;
    } else if (mode == 3u) {
        tables[0][255] = tables[1][255] = tables[0][256] = tables[1][256] = FAT16_FREE;
        ok = allocated(255u) && allocated(256u);
    } else if (mode == 4u) {
        prepare(153u, 152u); ok = allocated(153u) && fat16_alloc_cluster() == 0u;
    } else if (mode == 5u) {
        prepare(15002u, 15000u);
        ok = fat16_alloc_cluster() == 0u && writes == 0u && outside_reads == 0u;
    } else if (mode == 6u) {
        failed_read = 10u;
        ok = fat16_alloc_cluster() == 0u && writes == 0u;
    } else if (mode == 7u) {
        prepare(2u, 15000u); failed_read = 2u;
        ok = fat16_alloc_cluster() == 0u && writes == 0u && tables[0][2] == FAT16_FREE;
    } else if (mode == 8u) {
        prepare(2u, 15000u); failed_write = 1u;
        ok = fat16_alloc_cluster() == 0u && tables[0][2] == FAT16_FREE && tables[1][2] == FAT16_FREE;
    } else if (mode == 9u) {
        prepare(2u, 15000u); failed_write = 2u;
        ok = fat16_alloc_cluster() == 0u && tables[0][2] == FAT16_EOC_MAX && tables[1][2] == FAT16_FREE;
    } else if (mode == 10u) {
        ok = 1;
        for (i = 0u; i < 12u; i++) if (!rejected_geometry(i)) ok = 0;
    } else if (mode == 11u) {
        tables[0][42] = tables[1][42] = FAT16_BAD_CLUSTER;
        tables[0][79] = tables[1][79] = FAT16_EOC_MIN;
        ok = allocated(7049u) && tables[0][42] == FAT16_BAD_CLUSTER && tables[0][79] == FAT16_EOC_MIN;
    } else if (mode == 12u) {
        tables[0][257] = tables[1][257] = FAT16_FREE;
        ok = allocated(257u); tables[0][42] = tables[1][42] = FAT16_FREE;
        ok = ok && allocated(42u);
    }
    for (i = 0u; i < (uint32_t)sizeof(tables); i++) hash = (hash ^ bytes[i]) * 16777619u;
    (void)printf("mode=%u reads=%u writes=%u table_fnv=%u outside=%u\n", mode, reads, writes, hash, outside_reads);
    return ok ? 0 : 2;
}
