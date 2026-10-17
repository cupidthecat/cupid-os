#include "iso_fixture_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *bundle_allocate(void *context, ctool_u32 bytes) {
  (void)context; return malloc(bytes);
}

static void bundle_release(void *context, void *allocation, ctool_u32 bytes) {
  (void)context; (void)bytes; free(allocation);
}

ctool_arena_t *iso_bundle_test_arena_open(ctool_u32 limit) {
  ctool_allocator_t allocator;
  ctool_arena_t *arena = (ctool_arena_t *)0;
  allocator.context = (void *)0; allocator.allocate = bundle_allocate;
  allocator.release = bundle_release;
  if (ctool_arena_open(allocator, 4096u, limit, &arena) != CTOOL_OK) return (ctool_arena_t *)0;
  return arena;
}

void iso_bundle_test_arena_close(ctool_arena_t *arena) { ctool_arena_close(arena); }

static int bundle_mark_equal(ctool_arena_mark_t a, ctool_arena_mark_t b) {
  return a.owner == b.owner && a.block == b.block && a.used == b.used &&
      a.generation == b.generation;
}

/* Exercise preflight failures before fake huge views can be dereferenced. */
int iso_bundle_test_api(void) {
  static const unsigned char byte[] = {'x'};
  ctool_source_t manifest = {{{"manifest", 8u}}, {byte, 1u}};
  ctool_source_t file = {{{"file", 4u}}, {byte, 1u}};
  ctool_obj_iso_fixture_entry_t entry = {{"file", 4u}, CTOOL_OBJ_ISO_FIXTURE_FILE, &file};
  ctool_obj_iso_fixture_request_t inventory = {&entry, 1u};
  ctool_iso_fixture_bundle_request_t decoded;
  ctool_bytes_t encoded;
  ctool_source_t bundle;
  ctool_arena_t *arena = iso_bundle_test_arena_open(65536u);
  ctool_arena_mark_t before;
  ctool_u32 index;
  unsigned char *prefix = (unsigned char *)0;
  char error[33];
  if (arena == (ctool_arena_t *)0 ||
      ctool_arena_alloc(arena, 37u, 1u, (void **)&prefix) != CTOOL_OK) return 10;
  (void)memset(prefix, 0x5a, 37u); before = ctool_arena_mark(arena);
  for (index = 0u; index < 5u; index++) {
    encoded = ctool_bytes(byte, 99u); (void)memset(error, '?', sizeof(error));
    if (ctool_iso_fixture_bundle_encode(index == 0u ? (ctool_arena_t *)0 : arena,
        index == 1u ? (const ctool_source_t *)0 : &manifest,
        index == 2u ? (const ctool_obj_iso_fixture_request_t *)0 : &inventory,
        index == 3u ? (ctool_bytes_t *)0 : &encoded,
        index == 4u ? (char *)0 : error, 32u) ||
        (index != 3u && (encoded.data != (const unsigned char *)0 || encoded.size != 0u)) ||
        error[32] != '?' || !bundle_mark_equal(before, ctool_arena_mark(arena))) return 11;
  }
  for (index = 0u; index < 11u; index++) {
    ctool_source_t bad_manifest = manifest;
    ctool_source_t bad_file = file;
    ctool_obj_iso_fixture_entry_t bad_entry = entry;
    ctool_obj_iso_fixture_request_t bad_inventory = inventory;
    bad_inventory.entries = &bad_entry; bad_entry.source = &bad_file;
    if (index == 0u) bad_manifest.contents.data = (const unsigned char *)0;
    if (index == 1u) bad_manifest.contents.size = 0u;
    if (index == 2u) bad_manifest.contents.size = CTOOL_ISO_BUNDLE_MANIFEST_BYTES + 1u;
    if (index == 3u) bad_inventory.entries = (const ctool_obj_iso_fixture_entry_t *)0;
    if (index == 4u) bad_inventory.entry_count = 513u;
    if (index == 5u) bad_entry.path.data = (const char *)0;
    if (index == 6u) bad_entry.path.size = 1024u;
    if (index == 7u) bad_entry.kind = (ctool_obj_iso_fixture_kind_t)9u;
    if (index == 8u) bad_entry.kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    if (index == 9u) bad_file.contents.data = (const unsigned char *)0;
    if (index == 10u) bad_file.contents.size = 0xffffffffu;
    if (ctool_iso_fixture_bundle_encode(arena, &bad_manifest, &bad_inventory,
        &encoded, error, 32u) || encoded.size != 0u || encoded.data != (const unsigned char *)0 ||
        !bundle_mark_equal(before, ctool_arena_mark(arena))) return 12;
    if (index == 10u && strstr(error, "32-bit") == (char *)0) return 13;
  }
  if (!ctool_iso_fixture_bundle_encode(arena, &manifest, &inventory, &encoded,
      (char *)0, 0u)) return 14;
  bundle.path.text = ctool_string("request.bundle"); bundle.contents = encoded;
  before = ctool_arena_mark(arena);
  for (index = 0u; index < 4u; index++) {
    (void)memset(&decoded, 0x99, sizeof(decoded));
    if (ctool_iso_fixture_bundle_decode(index == 0u ? (ctool_arena_t *)0 : arena,
        index == 1u ? (const ctool_source_t *)0 : &bundle,
        index == 2u ? (ctool_iso_fixture_bundle_request_t *)0 : &decoded,
        index == 3u ? (char *)0 : error, 32u) ||
        (index != 2u && (decoded.manifest.contents.data != (const unsigned char *)0 ||
         decoded.manifest.contents.size != 0u || decoded.inventory.entry_count != 0u ||
         decoded.inventory.entries != (const ctool_obj_iso_fixture_entry_t *)0)) ||
        !bundle_mark_equal(before, ctool_arena_mark(arena))) return 15;
  }
  if (!ctool_iso_fixture_bundle_decode(arena, &bundle, &decoded, (char *)0, 0u)) return 16;
  for (index = 0u; index < 37u; index++) if (prefix[index] != 0x5au) return 17;
  iso_bundle_test_arena_close(arena); return 0;
}

static ctool_u32 bundle_hash(ctool_u32 hash, const unsigned char *data, ctool_u32 size) {
  ctool_u32 index;
  for (index = 0u; index < size; index++) hash = (hash ^ data[index]) * 16777619u;
  return hash;
}

static int bundle_number(const char *text, ctool_u32 *value) {
  ctool_u32 result = 0u;
  if (*text == 0) return 0;
  while (*text != 0) {
    ctool_u32 digit;
    if (*text < '0' || *text > '9') return 0;
    digit = (ctool_u32)(*text++ - '0');
    if (result > (0xffffffffu - digit) / 10u) return 0;
    result = result * 10u + digit;
  }
  *value = result; return 1;
}

int main(int argc, char **argv) {
  ctool_u32 capacity, limit, roundtrip;
  ctool_u32 count = 0u, manifest_size = 0u, hash = 0u, encoded_size = 0u;
  ctool_u64 payload_bytes = 0u;
  ctool_source_t bundle;
  ctool_arena_t *arena;
  ctool_arena_mark_t before;
  ctool_iso_fixture_bundle_request_t decoded;
  unsigned char *prefix = (unsigned char *)0;
  unsigned char *data;
  FILE *stream;
  long size;
  char error[513];
  int repetition, result = 0;
  if (argc == 2 && strcmp(argv[1], "--api") == 0) return iso_bundle_test_api();
  if (argc != 5 || !bundle_number(argv[2], &capacity) || capacity > 512u ||
      !bundle_number(argv[3], &limit) || !bundle_number(argv[4], &roundtrip) ||
      roundtrip > 1u) return 90;
  stream = fopen(argv[1], "rb");
  if (stream == (FILE *)0 || fseek(stream, 0, SEEK_END) != 0 ||
      (size = ftell(stream)) < 0 || (unsigned long)size > 0xffffffffu ||
      fseek(stream, 0, SEEK_SET) != 0) return 91;
  data = (unsigned char *)malloc((size_t)size + 1u);
  if (data == (unsigned char *)0 || fread(data, 1u, (size_t)size, stream) != (size_t)size ||
      fclose(stream) != 0) return 92;
  bundle.path.text = ctool_string("request.bundle"); bundle.contents = ctool_bytes(data, (ctool_u32)size);
  arena = iso_bundle_test_arena_open(limit);
  if (arena == (ctool_arena_t *)0 ||
      ctool_arena_alloc(arena, 37u, 1u, (void **)&prefix) != CTOOL_OK) return 93;
  (void)memset(prefix, 0x5a, 37u); before = ctool_arena_mark(arena);
  for (repetition = 0; repetition < 3; repetition++) {
    ctool_u32 index;
    (void)memset(error, '?', sizeof(error));
    result = ctool_iso_fixture_bundle_decode(arena, &bundle, &decoded,
        capacity == 0u ? (char *)0 : error, capacity);
    if (error[capacity] != '?') return 94;
    if (result) {
      count = decoded.inventory.entry_count; manifest_size = decoded.manifest.contents.size;
      hash = bundle_hash(2166136261u, decoded.manifest.contents.data, manifest_size);
      payload_bytes = 0u;
      for (index = 0u; index < count; index++) {
        const ctool_obj_iso_fixture_entry_t *entry = decoded.inventory.entries + index;
        unsigned char kind = (unsigned char)entry->kind;
        hash = bundle_hash(hash, &kind, 1u);
        hash = bundle_hash(hash, (const unsigned char *)entry->path.data, entry->path.size);
        if (entry->source != (const ctool_source_t *)0) {
          payload_bytes += entry->source->contents.size;
          hash = bundle_hash(hash, entry->source->contents.data, entry->source->contents.size);
        }
      }
      if (roundtrip != 0u) {
        ctool_bytes_t encoded;
        result = ctool_iso_fixture_bundle_encode(arena, &decoded.manifest, &decoded.inventory,
            &encoded, capacity == 0u ? (char *)0 : error, capacity);
        if (result && (encoded.size != bundle.contents.size ||
            memcmp(encoded.data, data, encoded.size) != 0)) return 95;
        encoded_size = encoded.size;
      }
      if (ctool_arena_rewind(arena, before) != CTOOL_OK) return 96;
    } else if (decoded.inventory.entry_count != 0u ||
        decoded.inventory.entries != (const ctool_obj_iso_fixture_entry_t *)0 ||
        decoded.manifest.contents.size != 0u || decoded.manifest.contents.data != (const unsigned char *)0)
      return 97;
    if (!bundle_mark_equal(before, ctool_arena_mark(arena))) return 98;
    for (index = 0u; index < 37u; index++) if (prefix[index] != 0x5au) return 99;
  }
  (void)printf("%u %u %llu %u %u\n%s\n", count, manifest_size, payload_bytes, hash,
      encoded_size, capacity == 0u ? "" : error);
  iso_bundle_test_arena_close(arena); free(data); return result ? 0 : 1;
}
