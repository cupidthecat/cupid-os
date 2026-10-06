#include "cupidbuild_iso_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *image_allocate(void *context, ctool_u32 bytes) {
  (void)context; return malloc(bytes);
}

static void image_release(void *context, void *allocation, ctool_u32 bytes) {
  (void)context; (void)bytes; free(allocation);
}

ctool_arena_t *iso_image_test_arena_open(ctool_u32 limit) {
  ctool_allocator_t allocator;
  ctool_arena_t *arena = (ctool_arena_t *)0;
  allocator.context = (void *)0; allocator.allocate = image_allocate;
  allocator.release = image_release;
  if (ctool_arena_open(allocator, 4096u, limit, &arena) != CTOOL_OK) return (ctool_arena_t *)0;
  return arena;
}

void iso_image_test_arena_close(ctool_arena_t *arena) { ctool_arena_close(arena); }

static int image_number(const char *text, ctool_u32 *value) {
  ctool_u32 result = 0u;
  if (*text == 0) return 0;
  while (*text != 0) {
    ctool_u32 digit;
    if (*text < '0' || *text > '9') return 0;
    digit = (ctool_u32)(*text++ - '0');
    if (result > (131072u - digit) / 10u) return 0;
    result = result * 10u + digit;
  }
  *value = result; return 1;
}

static int image_read(const char *path, ctool_source_t *source) {
  FILE *stream = fopen(path, "rb");
  long size;
  unsigned char *data;
  if (stream == (FILE *)0) return 0;
  if (fseek(stream, 0, SEEK_END) != 0 || (size = ftell(stream)) < 0 ||
      (unsigned long)size > 0xffffffffu || fseek(stream, 0, SEEK_SET) != 0) {
    (void)fclose(stream); return 0;
  }
  data = (unsigned char *)malloc((size_t)size + 1u);
  if (data == (unsigned char *)0) { (void)fclose(stream); return 0; }
  if (fread(data, 1u, (size_t)size, stream) != (size_t)size || fclose(stream) != 0) {
    free(data); return 0;
  }
  data[size] = 0u; source->path.text = ctool_string(path);
  source->contents = ctool_bytes(data, (ctool_u32)size); return 1;
}

int main(int argc, char **argv) {
  ctool_source_t manifest;
  ctool_source_t image;
  ctool_source_t sources[513];
  ctool_obj_iso_fixture_entry_t entries[513];
  ctool_obj_iso_fixture_request_t inventory;
  cupidbuild_iso_image_report_t report;
  ctool_arena_t *arena;
  char error[513];
  ctool_u32 capacity;
  ctool_u32 limit;
  void *prefix = (void *)0;
  ctool_arena_mark_t before;
  int argument;
  int result;
  int repetition;
  if (argc < 5 || !image_read(argv[1], &manifest) || !image_read(argv[2], &image)) return 90;
  if (!image_number(argv[3], &capacity) || capacity > 512u ||
      !image_number(argv[4], &limit)) return 91;
  arena = iso_image_test_arena_open(limit);
  if (arena == (ctool_arena_t *)0) return 92;
  (void)memset(sources, 0, sizeof(sources)); (void)memset(entries, 0, sizeof(entries));
  inventory.entries = entries; inventory.entry_count = 0u;
  for (argument = 5; argument < argc;) {
    ctool_u32 index = inventory.entry_count;
    if (index == 513u || argument + 1 >= argc) return 93;
    entries[index].path = ctool_string(argv[argument + 1]);
    if (strcmp(argv[argument], "--directory") == 0) {
      entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY; argument += 2;
    } else if (strcmp(argv[argument], "--file") == 0 && argument + 2 < argc) {
      if (!image_read(argv[argument + 2], &sources[index])) return 94;
      entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_FILE; entries[index].source = &sources[index];
      argument += 3;
    } else return 95;
    inventory.entry_count++;
  }
  if (ctool_arena_alloc(arena, 37u, 1u, &prefix) != CTOOL_OK) return 97;
  (void)memset(prefix, 0x5a, 37u); before = ctool_arena_mark(arena);
  for (argument = 0; argument < 6; argument++) {
    if (cupidbuild_iso_image_validate(argument == 0 ? (ctool_arena_t *)0 : arena,
        argument == 1 ? (const ctool_source_t *)0 : &manifest,
        argument == 2 ? (const ctool_obj_iso_fixture_request_t *)0 : &inventory,
        argument == 3 ? (const ctool_source_t *)0 : &image,
        argument == 4 ? (cupidbuild_iso_image_report_t *)0 : &report,
        argument == 5 ? (char *)0 : error, 512u)) return 98;
    if (argument != 4 && (report.inventory.directories != 0u || report.image_bytes != 0u)) return 99;
  }
  if (limit >= 16384u) {
    static const unsigned char byte[] = {'x'};
    ctool_source_t huge = {{{"huge", 4u}}, {byte, 0xffffffffu}};
    ctool_source_t huge_manifest = {{{"manifest", 8u}}, {(const unsigned char *)"huge\n", 5u}};
    ctool_source_t tiny_image = {{{"image", 5u}}, {byte, 1u}};
    ctool_obj_iso_fixture_entry_t entry = {{"huge", 4u}, CTOOL_OBJ_ISO_FIXTURE_FILE, &huge};
    ctool_obj_iso_fixture_request_t request = {&entry, 1u};
    if (cupidbuild_iso_image_validate(arena, &huge_manifest, &request, &tiny_image,
        &report, error, 512u) || strstr(error, "32-bit") == (char *)0 ||
        report.image_bytes != 0u || report.inventory.files != 0u) return 102;
  }
  for (repetition = 0; repetition < 3; repetition++) {
    ctool_arena_mark_t after;
    ctool_u32 index;
    (void)memset(&report, 0x99, sizeof(report)); (void)memset(error, '?', sizeof(error));
    result = cupidbuild_iso_image_validate(arena, &manifest, &inventory, &image,
        &report, capacity == 0u ? (char *)0 : error, capacity);
    if (error[capacity] != '?' || (!result && (report.inventory.directories != 0u ||
        report.inventory.files != 0u || report.inventory.directory_depth != 0u ||
        report.inventory.file_bytes != 0u || report.image_bytes != 0u || report.blocks != 0u ||
        report.path_table_bytes != 0u || report.continuation_block != 0u))) return 96;
    after = ctool_arena_mark(arena);
    if (after.owner != before.owner || after.block != before.block ||
        after.used != before.used || after.generation != before.generation) return 100;
    for (index = 0u; index < 37u; index++)
      if (((unsigned char *)prefix)[index] != 0x5au) return 101;
  }
  (void)printf("%u %u %u %llu %u %u %u %u\n%s\n", report.inventory.directories,
      report.inventory.files, report.inventory.directory_depth, report.inventory.file_bytes,
      report.image_bytes, report.blocks, report.path_table_bytes, report.continuation_block,
      capacity == 0u ? "" : error);
  for (argument = 0; (ctool_u32)argument < inventory.entry_count; argument++)
    free((void *)sources[argument].contents.data);
  free((void *)manifest.contents.data); free((void *)image.contents.data);
  iso_image_test_arena_close(arena);
  return result ? 0 : 1;
}
