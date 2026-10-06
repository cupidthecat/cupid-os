#include "disk_image.h"
#include "fat16_names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private stdio caller. UTF-8 destinations travel as exact file bytes so the
 * test does not depend on the host command-line encoding. Files and the
 * candidate are below two GiB; retained host capture/publication are separate. */
typedef struct { FILE *file; ctool_u64 size; unsigned reads, writes; } stage_io_t;
typedef struct {
  stage_io_t payload;
  ctool_u8 *destination;
  ctool_u32 destination_size;
  ctool_fat16_name_t *names;
  ctool_u32 count;
} stage_entry_t;

static unsigned live;
static void *allocate(void *context, ctool_u32 bytes) {
  void *p;
  (void)context;
  p = malloc(bytes);
  if (p) live++;
  return p;
}
static void release(void *context, void *p, ctool_u32 bytes) {
  (void)context; (void)bytes;
  live--; free(p);
}
static ctool_u32 decimal(const char *text) {
  ctool_u32 value = 0u;
  while (*text >= '0' && *text <= '9') {
    value = value * 10u + (ctool_u32)(*text - '0'); text++;
  }
  return value;
}
static int open_source(stage_io_t *io, const char *path, const char *mode) {
  long length;
  io->file = fopen(path, mode);
  if (!io->file || fseek(io->file, 0L, SEEK_END)) return 0;
  length = ftell(io->file);
  if (length < 0L || (ctool_u64)length > 2147483647u) return 0;
  io->size = (ctool_u64)length;
  return 1;
}
static ctool_status_t source_read(void *context, ctool_u64 offset,
    ctool_u8 *bytes, ctool_u32 size) {
  stage_io_t *io = context;
  io->reads++;
  if (offset > io->size || size > io->size - offset ||
      offset > 2147483647u || fseek(io->file, (long)offset, SEEK_SET) ||
      fread(bytes, 1u, size, io->file) != size) return CTOOL_ERR_IO;
  return CTOOL_OK;
}
static ctool_status_t sector_read(void *context, ctool_u32 sector, ctool_u8 *bytes) {
  return source_read(context, (ctool_u64)sector * 512u, bytes, 512u);
}
static ctool_status_t sector_write(void *context, ctool_u32 sector, const ctool_u8 *bytes) {
  stage_io_t *io = context;
  ctool_u64 offset = (ctool_u64)sector * 512u;
  io->writes++;
  if (offset > io->size || 512u > io->size - offset ||
      offset > 2147483647u || fseek(io->file, (long)offset, SEEK_SET) ||
      fwrite(bytes, 1u, 512u, io->file) != 512u) return CTOOL_ERR_IO;
  return CTOOL_OK;
}
static ctool_status_t payload_read(void *context, ctool_u32 offset,
    ctool_u8 *bytes, ctool_u32 size) {
  return source_read(context, offset, bytes, size);
}

int main(int argc, char **argv) {
  stage_io_t streams[5];
  ctool_disk_source_t sources[4];
  ctool_disk_image_request_t request;
  ctool_disk_image_result_t result;
  ctool_fat16_volume_t *volume = NULL;
  ctool_allocator_t allocator;
  stage_entry_t *entries = NULL;
  ctool_u32 count, i, staged = 0u;
  ctool_status_t status = CTOOL_OK;
  ctool_fat16_name_profile_t profile;
  if (argc < 10 || (argc - 10) % 2) return 2;
  count = (ctool_u32)(argc - 10) / 2u;
  profile = (ctool_fat16_name_profile_t)decimal(argv[9]);
  memset(streams, 0, sizeof(streams));
  memset(sources, 0, sizeof(sources));
  memset(&request, 0, sizeof(request));
  memset(&result, 0, sizeof(result));
  if (count) {
    entries = calloc(count, sizeof(*entries));
    if (!entries) status = CTOOL_ERR_NO_MEMORY;
  }
  for (i = 0u; i < count && status == CTOOL_OK; i++) {
    stage_io_t destination;
    ctool_string_t text;
    memset(&destination, 0, sizeof(destination));
    if (!open_source(&entries[i].payload, argv[10u + i * 2u], "rb") ||
        !open_source(&destination, argv[11u + i * 2u], "rb")) {
      status = CTOOL_ERR_IO;
    } else {
      entries[i].destination_size = (ctool_u32)destination.size;
      entries[i].destination = malloc(entries[i].destination_size ? entries[i].destination_size : 1u);
      if (!entries[i].destination) status = CTOOL_ERR_NO_MEMORY;
      if (status == CTOOL_OK)
        status = source_read(&destination, 0u, entries[i].destination, entries[i].destination_size);
      text.data = (const char *)entries[i].destination;
      text.size = entries[i].destination_size;
      if (status == CTOOL_OK)
        status = ctool_fat16_project_destination(text, profile, NULL, 0u, &entries[i].count);
      if (status == CTOOL_OK && entries[i].count > 0xffffffffu / sizeof(ctool_fat16_name_t))
        status = CTOOL_ERR_LIMIT;
      if (status == CTOOL_OK) {
        entries[i].names = malloc(entries[i].count * sizeof(ctool_fat16_name_t));
        if (!entries[i].names) status = CTOOL_ERR_NO_MEMORY;
      }
      if (status == CTOOL_OK)
        status = ctool_fat16_project_destination(text, profile, entries[i].names,
            entries[i].count, &entries[i].count);
    }
    if (destination.file && fclose(destination.file)) status = CTOOL_ERR_IO;
  }
  for (i = 0u; i < 5u && status == CTOOL_OK; i++) {
    const char *path = argv[i == 4u ? 1u : i + 2u];
    if (i == 3u && !strcmp(path, "-")) continue;
    if (!open_source(&streams[i], path, i == 4u ? "r+b" : "rb")) status = CTOOL_ERR_IO;
    if (i < 4u) {
      sources[i].context = &streams[i];
      sources[i].size = streams[i].size;
      sources[i].read_exact = source_read;
    }
  }
  request.bootloader = sources[0]; request.kernel = sources[1];
  request.checked_template = sources[2]; request.previous = sources[3];
  request.previous_present = streams[3].file != NULL;
  request.force_format = decimal(argv[8]) != 0u;
  request.image_sectors = decimal(argv[6]); request.fat_start_lba = decimal(argv[7]);
  request.candidate.context = &streams[4];
  request.candidate.sector_count = request.image_sectors;
  request.candidate.read_sector = sector_read; request.candidate.write_sector = sector_write;
  if (status == CTOOL_OK && streams[4].size != (ctool_u64)request.image_sectors * 512u)
    status = CTOOL_ERR_INPUT;
  if (status == CTOOL_OK) status = ctool_disk_image_compose(&request, &result);
  allocator.context = NULL; allocator.allocate = allocate; allocator.release = release;
  if (status == CTOOL_OK && count)
    status = ctool_fat16_open(allocator, request.candidate, request.fat_start_lba, &volume);
  for (i = 0u; i < count && status == CTOOL_OK; i++) {
    ctool_fat16_payload_t payload;
    payload.context = &entries[i].payload;
    payload.size = (ctool_u32)entries[i].payload.size;
    payload.read_exact = payload_read;
    status = ctool_fat16_stage(volume, entries[i].names, entries[i].count, payload);
    if (status == CTOOL_OK) staged++;
  }
  ctool_fat16_close(volume);
  for (i = 0u; i < count && entries; i++) {
    if (entries[i].payload.file && fclose(entries[i].payload.file)) status = CTOOL_ERR_IO;
    free(entries[i].destination); free(entries[i].names);
  }
  free(entries);
  for (i = 0u; i < 5u; i++) if (streams[i].file && fclose(streams[i].file)) status = CTOOL_ERR_IO;
  printf("status=%u ready=%u reused=%u staged=%u writes=%u live=%u\n",
      (unsigned)status, (unsigned)(status == CTOOL_OK && result.ready),
      (unsigned)result.reused, (unsigned)staged, streams[4].writes, live);
  return 0;
}
