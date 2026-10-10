#include "disk_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  FILE *file;
  const char *role;
  const char *mode;
  unsigned calls;
  unsigned fail;
} disk_io_t;

static unsigned parse_uint(const char *text) {
  unsigned value = 0u;
  while (*text >= '0' && *text <= '9') { value = value * 10u + (unsigned)(*text - '0'); text++; }
  return value;
}

static ctool_status_t source_read(void *context, ctool_u64 offset,
                                   ctool_u8 *bytes, ctool_u32 size) {
  disk_io_t *io = context;
  io->calls++;
  if (!strcmp(io->mode, io->role) && io->calls == io->fail) return CTOOL_ERR_IO;
  if (offset > 2147483647u || fseek(io->file, (long)offset, SEEK_SET) ||
      fread(bytes, 1u, size, io->file) != size) return CTOOL_ERR_IO;
  return CTOOL_OK;
}

static ctool_status_t candidate_write(void *context, ctool_u32 sector,
                                       const ctool_u8 *bytes) {
  disk_io_t *io = context;
  io->calls++;
  if (!strcmp(io->mode, "write") && io->calls == io->fail) return CTOOL_ERR_IO;
  if (sector > 2147483647u / 512u || fseek(io->file, (long)sector * 512L, SEEK_SET))
    return CTOOL_ERR_IO;
  if (!strcmp(io->mode, "partial") && io->calls == io->fail) {
    if (fwrite(bytes, 1u, 256u, io->file) != 256u) return CTOOL_ERR_IO;
    return CTOOL_ERR_IO;
  }
  return fwrite(bytes, 1u, 512u, io->file) == 512u ? CTOOL_OK : CTOOL_ERR_IO;
}

int main(int argc, char **argv) {
  ctool_disk_image_request_t request;
  ctool_disk_image_result_t result;
  ctool_disk_source_t sources[4];
  disk_io_t streams[5];
  const char *roles[5] = {"boot", "kernel", "template", "previous", "candidate"};
  unsigned i;
  ctool_status_t status;
  if (argc != 11) return 2;
  memset(&request, 0, sizeof(request));
  memset(&result, 0, sizeof(result));
  memset(streams, 0, sizeof(streams));
  memset(sources, 0, sizeof(sources));
  for (i = 0u; i < 5u; i++) {
    const char *path = argv[i == 4u ? 1u : i + 2u];
    streams[i].role = roles[i];
    streams[i].mode = argv[9];
    streams[i].fail = parse_uint(argv[10]);
    if (i == 3u && !strcmp(path, "-")) continue;
    streams[i].file = fopen(path, i == 4u ? "r+b" : "rb");
    if (!streams[i].file) return 3;
    if (i < 4u) {
      long size;
      if (fseek(streams[i].file, 0L, SEEK_END)) return 4;
      size = ftell(streams[i].file);
      if (size < 0L) return 4;
      sources[i].context = &streams[i];
      sources[i].size = (ctool_u64)size;
      sources[i].read_exact = source_read;
    }
  }
  request.bootloader = sources[0]; request.kernel = sources[1];
  request.checked_template = sources[2]; request.previous = sources[3];
  request.previous_present = streams[3].file != NULL;
  request.force_format = parse_uint(argv[8]) != 0u;
  request.image_sectors = parse_uint(argv[6]); request.fat_start_lba = parse_uint(argv[7]);
  request.candidate.context = &streams[4];
  request.candidate.sector_count = request.image_sectors;
  request.candidate.write_sector = candidate_write;
  if (!strcmp(argv[9], "wide-kernel")) request.kernel.size = 0x100000000ull;
  if (!strcmp(argv[9], "wrong-capacity")) request.candidate.sector_count--;
  if (!strcmp(argv[9], "no-writer")) request.candidate.write_sector = NULL;
  if (!strcmp(argv[9], "no-template-reader")) request.checked_template.read_exact = NULL;
  status = ctool_disk_image_compose(!strcmp(argv[9], "null-request") ? NULL : &request,
                                  !strcmp(argv[9], "null-result") ? NULL : &result);
  for (i = 0u; i < 5u; i++) if (streams[i].file && fclose(streams[i].file)) return 5;
  printf("status=%u ready=%u reused=%u dirty=%u boot=%u kernel=%u template=%u previous=%u writes=%u\n",
      (unsigned)status, (unsigned)result.ready, (unsigned)result.reused,
      (unsigned)result.attempted_write, streams[0].calls, streams[1].calls,
      streams[2].calls, streams[3].calls, streams[4].calls);
  return 0;
}
