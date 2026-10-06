#include "fat16_stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  FILE *file;
  unsigned reads;
  unsigned writes;
  unsigned fail;
  const char *mode;
} contract_io_t;

static unsigned live_allocations;
static int fail_allocation;

static unsigned contract_uint(const char *text) {
  unsigned result = 0u;
  while (*text >= '0' && *text <= '9') {
    result = result * 10u + (unsigned)(*text - '0');
    text++;
  }
  return result;
}

static void *contract_allocate(void *context, ctool_u32 size) {
  void *p;
  (void)context;
  if (fail_allocation) return NULL;
  p = malloc(size);
  if (p) live_allocations++;
  return p;
}

static void contract_release(void *context, void *p, ctool_u32 size) {
  (void)context;
  (void)size;
  live_allocations--;
  free(p);
}

static ctool_status_t contract_read(void *context, ctool_u32 sector,
                                     ctool_u8 *bytes) {
  contract_io_t *io = context;
  io->reads++;
  if (!strcmp(io->mode, "read") && io->reads == io->fail) return CTOOL_ERR_IO;
  if (fseek(io->file, (long)sector * 512L, SEEK_SET) ||
      fread(bytes, 1u, 512u, io->file) != 512u) return CTOOL_ERR_IO;
  return CTOOL_OK;
}

static ctool_status_t contract_write(void *context, ctool_u32 sector,
                                      const ctool_u8 *bytes) {
  contract_io_t *io = context;
  io->writes++;
  if (!strcmp(io->mode, "write") && io->writes == io->fail) return CTOOL_ERR_IO;
  if (fseek(io->file, (long)sector * 512L, SEEK_SET)) return CTOOL_ERR_IO;
  if (!strcmp(io->mode, "partial") && io->writes == io->fail) {
    if (fwrite(bytes, 1u, 256u, io->file) != 256u) return CTOOL_ERR_IO;
    return CTOOL_ERR_IO;
  }
  if (fwrite(bytes, 1u, 512u, io->file) != 512u) return CTOOL_ERR_IO;
  return CTOOL_OK;
}

static ctool_status_t contract_payload(void *context, ctool_u32 offset,
                                        ctool_u8 *bytes, ctool_u32 size) {
  contract_io_t *io = context;
  io->reads++;
  if (!strcmp(io->mode, "payload") && io->reads == io->fail) return CTOOL_ERR_IO;
  if (fseek(io->file, (long)offset, SEEK_SET) ||
      fread(bytes, 1u, size, io->file) != size) return CTOOL_ERR_IO;
  return CTOOL_OK;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

int main(int argc, char **argv) {
  contract_io_t disk, source;
  ctool_fat16_store_t store;
  ctool_allocator_t allocator;
  ctool_fat16_payload_t payload;
  ctool_fat16_volume_t *v = NULL;
  ctool_fat16_name_t names[64];
  ctool_status_t opened, staged = CTOOL_ERR_INTERNAL, repeated = CTOOL_ERR_INTERNAL;
  unsigned i, j, count;
  long disk_size, payload_size;
  if (argc < 7) return 2;
  count = contract_uint(argv[6]);
  if (count == 0u || count > 64u || argc != (int)(7u + count)) return 2;
  memset(&disk, 0, sizeof(disk));
  memset(&source, 0, sizeof(source));
  disk.mode = source.mode = argv[4];
  disk.fail = source.fail = contract_uint(argv[5]);
  fail_allocation = !strcmp(argv[4], "allocation");
  disk.file = fopen(argv[1], "r+b");
  source.file = fopen(argv[3], "rb");
  if (!disk.file || !source.file) return 2;
  if (fseek(disk.file, 0, SEEK_END) || fseek(source.file, 0, SEEK_END)) return 2;
  disk_size = ftell(disk.file);
  payload_size = ftell(source.file);
  if (disk_size < 0 || payload_size < 0 || disk_size % 512L) return 2;
  for (i = 0u; i < count; i++) {
    if (strlen(argv[7u + i]) != 22u) return 2;
    for (j = 0u; j < 11u; j++) {
      int high = hex_digit(argv[7u + i][j * 2u]);
      int low = hex_digit(argv[7u + i][j * 2u + 1u]);
      if (high < 0 || low < 0) return 2;
      names[i].bytes[j] = (ctool_u8)((high << 4) | low);
    }
  }
  allocator.context = NULL;
  allocator.allocate = contract_allocate;
  allocator.release = contract_release;
  store.context = &disk;
  store.sector_count = (ctool_u32)(disk_size / 512L);
  store.read_sector = contract_read;
  store.write_sector = contract_write;
  payload.context = &source;
  payload.size = (ctool_u32)payload_size;
  payload.read_exact = contract_payload;
  opened = ctool_fat16_open(allocator, store, contract_uint(argv[2]), &v);
  if (opened == CTOOL_OK) {
    staged = ctool_fat16_stage(v, names, count, payload);
    if (staged != CTOOL_OK) {
      disk.mode = source.mode = "none";
      repeated = ctool_fat16_stage(v, names, count, payload);
    }
  }
  ctool_fat16_close(v);
  if (fclose(disk.file) || fclose(source.file) || live_allocations) return 3;
  printf("open=%u stage=%u repeat=%u reads=%u writes=%u payload_reads=%u live=%u\n",
         (unsigned)opened, (unsigned)staged, (unsigned)repeated,
         disk.reads, disk.writes, source.reads, live_allocations);
  return 0;
}
