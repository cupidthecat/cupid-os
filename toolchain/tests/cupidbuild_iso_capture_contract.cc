#include "cupidbuild_iso_capture.h"
#include "cupidbuild_iso_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int unhex(const char *input, char *output, size_t capacity) {
  size_t count = 0u;
  while (*input != 0) {
    unsigned int value = 0u;
    unsigned int index;
    for (index = 0u; index < 2u; index++) {
      unsigned char digit = (unsigned char)*input++;
      if (digit >= '0' && digit <= '9') value = value * 16u + digit - '0';
      else if (digit >= 'a' && digit <= 'f') value = value * 16u + digit - 'a' + 10u;
      else return 0;
    }
    if (value == 0u || count + 1u >= capacity) return 0;
    output[count++] = (char)value;
  }
  output[count] = 0; return 1;
}

static ctool_u32 digest(ctool_u32 value, const void *bytes, ctool_u32 count) {
  const unsigned char *data = (const unsigned char *)bytes;
  ctool_u32 index;
  for (index = 0u; index < count; index++) value = (value ^ data[index]) * 16777619u;
  return value;
}

static ctool_u32 snapshot(const cupidbuild_iso_capture_t *capture) {
  const ctool_source_t *manifest = cupidbuild_iso_capture_manifest(capture);
  const ctool_obj_iso_fixture_request_t *inventory = cupidbuild_iso_capture_inventory(capture);
  ctool_u32 value = digest(2166136261u, manifest->contents.data, manifest->contents.size);
  ctool_u32 index;
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = inventory->entries + index;
    unsigned char kind = (unsigned char)entry->kind;
    value = digest(value, entry->path.data, entry->path.size);
    value = digest(value, &kind, 1u);
    if (entry->source != (const ctool_source_t *)0)
      value = digest(value, entry->source->contents.data, entry->source->contents.size);
  }
  return value;
}

static void *allocate(void *context, ctool_u32 bytes) { (void)context; return malloc(bytes); }
static void release(void *context, void *allocation, ctool_u32 bytes) {
  (void)context; (void)bytes; free(allocation);
}

static int check_image(cupidbuild_host_observer_t *observer, cupidbuild_iso_capture_t *capture,
                       const char *logical, char *error, ctool_u32 capacity) {
  unsigned char *bytes = (unsigned char *)0;
  uint64_t size;
  ctool_source_t image;
  ctool_allocator_t allocator;
  ctool_arena_t *arena = (ctool_arena_t *)0;
  cupidbuild_iso_image_report_t report;
  int result;
  if (!cupidbuild_host_observer_file(observer, logical, 64u * 1024u * 1024u, &bytes, &size)) return 0;
  image.path.text = ctool_string(logical); image.contents = ctool_bytes(bytes, (ctool_u32)size);
  allocator.context = (void *)0; allocator.allocate = allocate; allocator.release = release;
  if (ctool_arena_open(allocator, 4096u, 131072u, &arena) != CTOOL_OK) { free(bytes); return 0; }
  result = cupidbuild_iso_image_validate(arena, cupidbuild_iso_capture_manifest(capture),
      cupidbuild_iso_capture_inventory(capture), &image, &report, error, capacity);
  ctool_arena_close(arena); free(bytes); return result;
}

static void show(const cupidbuild_iso_capture_t *capture) {
  const cupidbuild_iso_inventory_report_t *report = cupidbuild_iso_capture_report(capture);
  const ctool_obj_iso_fixture_request_t *inventory = cupidbuild_iso_capture_inventory(capture);
  const ctool_source_t *manifest = cupidbuild_iso_capture_manifest(capture);
  ctool_u32 index;
  printf("capture %u %u %u %u %u %u\n", inventory->entry_count, report->directories,
      report->files, report->directory_depth, (ctool_u32)report->file_bytes,
      digest(2166136261u, manifest->contents.data, manifest->contents.size));
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = inventory->entries + index;
    ctool_u32 size = entry->source == (const ctool_source_t *)0 ? 0u : entry->source->contents.size;
    ctool_u32 hash = entry->source == (const ctool_source_t *)0 ? 0u :
        digest(2166136261u, entry->source->contents.data, size);
    printf("entry %s %u %u %u\n", entry->path.data, (ctool_u32)entry->kind, size, hash);
  }
}

int main(int argc, char **argv) {
  cupidbuild_host_observer_t *observer = (cupidbuild_host_observer_t *)0;
  cupidbuild_iso_capture_t *capture = (cupidbuild_iso_capture_t *)0;
  char root[8192], manifest[8192], fixtures[8192], image[8192], error[514];
  const char *mode;
  int result = 1;
  unsigned int repetition;
  if (argc < 5 || argc > 6 || !unhex(argv[1], root, sizeof(root)) ||
      !unhex(argv[2], manifest, sizeof(manifest)) || !unhex(argv[3], fixtures, sizeof(fixtures))) return 90;
  mode = argv[4];
  if (!cupidbuild_host_observer_open(root, &observer)) return 91;
  if (strcmp(mode, "arguments") == 0) {
    if (cupidbuild_iso_capture_manifest((const cupidbuild_iso_capture_t *)0) != (const ctool_source_t *)0 ||
        cupidbuild_iso_capture_inventory((const cupidbuild_iso_capture_t *)0) != (const ctool_obj_iso_fixture_request_t *)0 ||
        cupidbuild_iso_capture_report((const cupidbuild_iso_capture_t *)0) != (const cupidbuild_iso_inventory_report_t *)0 ||
        cupidbuild_iso_capture_require_unchanged((cupidbuild_iso_capture_t *)0)) goto done;
    cupidbuild_iso_capture_close((cupidbuild_iso_capture_t *)0);
    for (repetition = 0u; repetition < 7u; repetition++) {
      capture = (cupidbuild_iso_capture_t *)observer;
      (void)memset(error, '?', sizeof(error));
      if (cupidbuild_iso_capture_open(repetition == 0u || repetition == 6u ? (cupidbuild_host_observer_t *)0 : observer,
          repetition == 1u ? (const char *)0 : repetition == 2u ? "" : manifest,
          repetition == 3u ? (const char *)0 : fixtures,
          repetition == 4u ? (cupidbuild_iso_capture_t **)0 : &capture,
          repetition == 5u ? (char *)0 : error + 1, repetition == 6u ? 1u : 512u)) {
        cupidbuild_iso_capture_close(capture); capture = (cupidbuild_iso_capture_t *)0; goto done;
      }
      if (repetition != 4u && capture != (cupidbuild_iso_capture_t *)0) {
        capture = (cupidbuild_iso_capture_t *)0; goto done;
      }
      capture = (cupidbuild_iso_capture_t *)0;
      if (error[0] != '?' || error[513] != '?' ||
          (repetition == 6u && error[1] != 0)) goto done;
    }
    result = cupidbuild_host_observer_require_unchanged(observer) ? 0 : 1; goto done;
  }
  (void)memset(error, '?', sizeof(error));
  if (!cupidbuild_iso_capture_open(observer, manifest, fixtures, &capture,
      strcmp(mode, "zero") == 0 ? (char *)0 : error + 1, strcmp(mode, "zero") == 0 ? 0u : 512u)) {
    if (capture != (cupidbuild_iso_capture_t *)0 || error[0] != '?' || error[513] != '?') goto done;
    fprintf(stderr, "%s\n", strcmp(mode, "zero") == 0 ? "capture failed" : error + 1);
    result = strcmp(mode, "reject") == 0 ? 0 : 1; goto done;
  }
  if (error[0] != '?' || error[513] != '?') goto done;
  if (strcmp(mode, "reject") == 0) goto done;
  if (strcmp(mode, "wait") == 0) {
    unsigned char resume;
    ctool_u32 before = snapshot(capture);
    printf("ready %u\n", before); fflush(stdout);
    if (fread(&resume, 1u, 1u, stdin) != 1u || before != snapshot(capture)) goto done;
    if ((cupidbuild_iso_capture_require_unchanged(capture) != 0) != (resume == 'p')) goto done;
    printf("retained %u\n", snapshot(capture)); result = 0; goto done;
  }
  if (strcmp(mode, "image") == 0) {
    if (argc != 6 || !unhex(argv[5], image, sizeof(image)) ||
        !check_image(observer, capture, image, error + 1, 512u)) {
      fprintf(stderr, "%s\n", error + 1); goto done;
    }
  }
  if (!cupidbuild_iso_capture_require_unchanged(capture)) goto done;
  show(capture);
  if (strcmp(mode, "repeat") == 0) {
    for (repetition = 0u; repetition < 3u; repetition++) {
      ctool_u32 before = snapshot(capture);
      cupidbuild_iso_capture_close(capture); capture = (cupidbuild_iso_capture_t *)0;
      if (!cupidbuild_host_observer_require_unchanged(observer) ||
          !cupidbuild_iso_capture_open(observer, manifest, fixtures, &capture, error + 1, 512u) ||
          snapshot(capture) != before) goto done;
    }
  }
  result = 0;
done:
  cupidbuild_iso_capture_close(capture);
  if (!cupidbuild_host_observer_close(observer)) return 92;
  return result;
}
