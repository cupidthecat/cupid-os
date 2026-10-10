#include "iso_fixture_bundle.h"
#include <string.h>

static const unsigned char iso_bundle_magic[8] = {'C', 'U', 'P', 'I', 'S', 'O', '1', 0};

static int iso_bundle_error(char *error, ctool_u32 capacity, const char *message) {
  ctool_u32 index = 0u;
  if (error != (char *)0 && capacity != 0u) {
    while (index + 1u < capacity && message[index] != 0) {
      error[index] = message[index]; index++;
    }
    error[index] = 0;
  }
  return 0;
}

static int iso_bundle_bytes(ctool_bytes_t bytes) {
  return bytes.size == 0u || bytes.data != (const unsigned char *)0;
}

static void iso_bundle_put(unsigned char *data, ctool_u32 value) {
  data[0] = (unsigned char)value; data[1] = (unsigned char)(value >> 8u);
  data[2] = (unsigned char)(value >> 16u); data[3] = (unsigned char)(value >> 24u);
}

static ctool_u32 iso_bundle_get(const unsigned char *data) {
  return (ctool_u32)data[0] | ((ctool_u32)data[1] << 8u) |
      ((ctool_u32)data[2] << 16u) | ((ctool_u32)data[3] << 24u);
}

int ctool_iso_fixture_bundle_encode(
    ctool_arena_t *arena, const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory, ctool_bytes_t *bundle_out,
    char *error, ctool_u32 error_capacity) {
  ctool_u32 index;
  ctool_u32 offset;
  ctool_u64 total;
  ctool_arena_mark_t mark;
  unsigned char *data = (unsigned char *)0;
  if (bundle_out != (ctool_bytes_t *)0) *bundle_out = ctool_bytes((const unsigned char *)0, 0u);
  if (error != (char *)0 && error_capacity != 0u) error[0] = 0;
  if (arena == (ctool_arena_t *)0 || manifest == (const ctool_source_t *)0 ||
      inventory == (const ctool_obj_iso_fixture_request_t *)0 || bundle_out == (ctool_bytes_t *)0 ||
      (error == (char *)0 && error_capacity != 0u))
    return iso_bundle_error(error, error_capacity, "ISO bundle arena, manifest, inventory and result are required");
  if (!iso_bundle_bytes(manifest->contents) || manifest->contents.size == 0u ||
      manifest->contents.size > CTOOL_ISO_BUNDLE_MANIFEST_BYTES ||
      inventory->entries == (const ctool_obj_iso_fixture_entry_t *)0 ||
      inventory->entry_count == 0u || inventory->entry_count > CTOOL_ISO_BUNDLE_ENTRIES)
    return iso_bundle_error(error, error_capacity, "ISO bundle manifest or entry count exceeds framing bounds");
  total = 16u + (ctool_u64)manifest->contents.size;
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = inventory->entries + index;
    ctool_u32 size = 0u;
    if (entry->path.data == (const char *)0 || entry->path.size == 0u ||
        entry->path.size > CTOOL_ISO_BUNDLE_PATH_BYTES)
      return iso_bundle_error(error, error_capacity, "ISO bundle entry name exceeds framing bounds");
    if (entry->kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY) {
      if (entry->source != (const ctool_source_t *)0)
        return iso_bundle_error(error, error_capacity, "ISO bundle directory cannot have a file source");
    } else if (entry->kind == CTOOL_OBJ_ISO_FIXTURE_FILE) {
      if (entry->source == (const ctool_source_t *)0 || !iso_bundle_bytes(entry->source->contents))
        return iso_bundle_error(error, error_capacity, "ISO bundle file source is invalid");
      size = entry->source->contents.size;
    } else return iso_bundle_error(error, error_capacity, "ISO bundle entry kind is invalid");
    total += 16u + (ctool_u64)entry->path.size + size;
    if (total > 0xffffffffu)
      return iso_bundle_error(error, error_capacity, "ISO bundle exceeds 32-bit storage");
  }
  mark = ctool_arena_mark(arena);
  if (ctool_arena_alloc(arena, (ctool_u32)total, 1u, (void **)&data) != CTOOL_OK) {
    (void)ctool_arena_rewind(arena, mark);
    return iso_bundle_error(error, error_capacity, "ISO bundle allocation failed");
  }
  (void)memcpy(data, iso_bundle_magic, sizeof(iso_bundle_magic));
  iso_bundle_put(data + 8u, manifest->contents.size); iso_bundle_put(data + 12u, inventory->entry_count);
  (void)memcpy(data + 16u, manifest->contents.data, manifest->contents.size);
  offset = 16u + manifest->contents.size;
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = inventory->entries + index;
    ctool_u32 size = entry->source == (const ctool_source_t *)0 ? 0u : entry->source->contents.size;
    iso_bundle_put(data + offset, (ctool_u32)entry->kind);
    iso_bundle_put(data + offset + 4u, entry->path.size);
    iso_bundle_put(data + offset + 8u, size); iso_bundle_put(data + offset + 12u, 0u);
    offset += 16u;
    (void)memcpy(data + offset, entry->path.data, entry->path.size); offset += entry->path.size;
    if (size != 0u) (void)memcpy(data + offset, entry->source->contents.data, size);
    offset += size;
  }
  *bundle_out = ctool_bytes(data, (ctool_u32)total); return 1;
}

int ctool_iso_fixture_bundle_decode(
    ctool_arena_t *arena, const ctool_source_t *bundle,
    ctool_iso_fixture_bundle_request_t *request_out,
    char *error, ctool_u32 error_capacity) {
  ctool_arena_mark_t mark;
  ctool_obj_iso_fixture_entry_t *entries = (ctool_obj_iso_fixture_entry_t *)0;
  ctool_source_t *sources = (ctool_source_t *)0;
  ctool_u32 count;
  ctool_u32 manifest_size;
  ctool_u32 offset;
  ctool_u32 index;
  const char *failure;
  if (request_out != (ctool_iso_fixture_bundle_request_t *)0)
    (void)memset(request_out, 0, sizeof(*request_out));
  if (error != (char *)0 && error_capacity != 0u) error[0] = 0;
  if (arena == (ctool_arena_t *)0 || bundle == (const ctool_source_t *)0 ||
      request_out == (ctool_iso_fixture_bundle_request_t *)0 || (error == (char *)0 && error_capacity != 0u))
    return iso_bundle_error(error, error_capacity, "ISO bundle arena, source and result are required");
  if (!iso_bundle_bytes(bundle->contents) || bundle->contents.size < 16u ||
      memcmp(bundle->contents.data, iso_bundle_magic, sizeof(iso_bundle_magic)) != 0)
    return iso_bundle_error(error, error_capacity, "ISO bundle header is invalid");
  manifest_size = iso_bundle_get(bundle->contents.data + 8u);
  count = iso_bundle_get(bundle->contents.data + 12u);
  if (manifest_size == 0u || manifest_size > CTOOL_ISO_BUNDLE_MANIFEST_BYTES ||
      manifest_size > bundle->contents.size - 16u || count == 0u || count > CTOOL_ISO_BUNDLE_ENTRIES)
    return iso_bundle_error(error, error_capacity, "ISO bundle manifest or entry count exceeds framing bounds");
  offset = 16u + manifest_size;
  /* Validate the complete framing before creating views or allocating arrays. */
  for (index = 0u; index < count; index++) {
    ctool_u32 kind, name_size, payload_size;
    if (bundle->contents.size - offset < 16u)
      return iso_bundle_error(error, error_capacity, "ISO bundle entry header is truncated");
    kind = iso_bundle_get(bundle->contents.data + offset);
    name_size = iso_bundle_get(bundle->contents.data + offset + 4u);
    payload_size = iso_bundle_get(bundle->contents.data + offset + 8u);
    if ((kind != CTOOL_OBJ_ISO_FIXTURE_DIRECTORY && kind != CTOOL_OBJ_ISO_FIXTURE_FILE) ||
        iso_bundle_get(bundle->contents.data + offset + 12u) != 0u ||
        name_size == 0u || name_size > CTOOL_ISO_BUNDLE_PATH_BYTES ||
        (kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY && payload_size != 0u))
      return iso_bundle_error(error, error_capacity, "ISO bundle entry header is invalid");
    offset += 16u;
    if (name_size > bundle->contents.size - offset)
      return iso_bundle_error(error, error_capacity, "ISO bundle entry name is truncated");
    offset += name_size;
    if (payload_size > bundle->contents.size - offset)
      return iso_bundle_error(error, error_capacity, "ISO bundle file payload is truncated");
    offset += payload_size;
  }
  if (offset != bundle->contents.size)
    return iso_bundle_error(error, error_capacity, "ISO bundle has trailing bytes");
  mark = ctool_arena_mark(arena);
  if (ctool_arena_alloc_zero(arena, count, (ctool_u32)sizeof(*entries), 8u, (void **)&entries) != CTOOL_OK ||
      ctool_arena_alloc_zero(arena, count, (ctool_u32)sizeof(*sources), 8u, (void **)&sources) != CTOOL_OK) {
    failure = "ISO bundle allocation failed"; goto failed;
  }
  offset = 16u + manifest_size;
  for (index = 0u; index < count; index++) {
    ctool_u32 kind = iso_bundle_get(bundle->contents.data + offset);
    ctool_u32 name_size = iso_bundle_get(bundle->contents.data + offset + 4u);
    ctool_u32 payload_size = iso_bundle_get(bundle->contents.data + offset + 8u);
    offset += 16u;
    entries[index].kind = (ctool_obj_iso_fixture_kind_t)kind;
    entries[index].path.data = (const char *)bundle->contents.data + offset;
    entries[index].path.size = name_size; offset += name_size;
    if (kind == CTOOL_OBJ_ISO_FIXTURE_FILE) {
      sources[index].path.text = entries[index].path;
      sources[index].contents = ctool_bytes(bundle->contents.data + offset, payload_size);
      entries[index].source = sources + index;
    }
    offset += payload_size;
  }
  request_out->manifest.path = bundle->path;
  request_out->manifest.contents = ctool_bytes(bundle->contents.data + 16u, manifest_size);
  request_out->inventory.entries = entries; request_out->inventory.entry_count = count;
  return 1;
failed:
  (void)ctool_arena_rewind(arena, mark); return iso_bundle_error(error, error_capacity, failure);
}
