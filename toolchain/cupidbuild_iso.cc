#include "cupidbuild_iso.h"

#define ISO_ENTRIES 512u
#define ISO_PATH_BYTES 1023u
#define ISO_MANIFEST_BYTES ((ISO_PATH_BYTES + 2u) * ISO_ENTRIES)

static void iso_report_clear(cupidbuild_iso_inventory_report_t *report) {
  if (report != (cupidbuild_iso_inventory_report_t *)0) {
    report->directories = 0u;
    report->files = 0u;
    report->directory_depth = 0u;
    report->file_bytes = 0u;
  }
}

static int iso_error(char *error, ctool_u32 capacity, const char *text) {
  ctool_u32 index = 0u;
  if (error != (char *)0 && capacity != 0u) {
    while (index + 1u < capacity && text[index] != '\0') {
      error[index] = text[index];
      index++;
    }
    error[index] = '\0';
  }
  return 0;
}

static unsigned char iso_fold(unsigned char byte) {
  if (byte >= 'A' && byte <= 'Z') byte = (unsigned char)(byte + ('a' - 'A'));
  return byte;
}

static int iso_equal(ctool_string_t left, ctool_string_t right, int folded) {
  ctool_u32 index;
  if (left.size != right.size) return 0;
  for (index = 0u; index < left.size; index++) {
    unsigned char a = (unsigned char)left.data[index];
    unsigned char b = (unsigned char)right.data[index];
    if (folded) {
      a = iso_fold(a);
      b = iso_fold(b);
    }
    if (a != b) return 0;
  }
  return 1;
}

static int iso_path(ctool_string_t path, int directory, ctool_u32 *depth) {
  ctool_u32 index;
  ctool_u32 start = 0u;
  ctool_u32 components = 0u;
  if (path.data == (const char *)0 || path.size == 0u || path.size > ISO_PATH_BYTES)
    return 0;
  for (index = 0u; index <= path.size; index++) {
    if (index == path.size || path.data[index] == '/') {
      ctool_u32 length = index - start;
      if (length == 0u || length > 127u ||
          (length == 1u && path.data[start] == '.') ||
          (length == 2u && path.data[start] == '.' && path.data[start + 1u] == '.'))
        return 0;
      components++;
      start = index + 1u;
    } else {
      unsigned char byte = (unsigned char)path.data[index];
      if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-'))
        return 0;
    }
  }
  if ((directory && components >= 8u) || (!directory && components > 8u))
    return 0;
  *depth = directory ? components + 1u : components;
  return 1;
}

int cupidbuild_iso_inventory_validate(
    const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory,
    cupidbuild_iso_inventory_report_t *report,
    char *error, ctool_u32 error_capacity) {
  unsigned char seen[ISO_ENTRIES];
  cupidbuild_iso_inventory_report_t candidate;
  ctool_u32 index;
  ctool_u32 offset = 0u;
  ctool_u32 matched = 0u;
  iso_report_clear(report);
  if (error != (char *)0 && error_capacity != 0u) error[0] = '\0';
  if (report == (cupidbuild_iso_inventory_report_t *)0 ||
      (error == (char *)0 && error_capacity != 0u) ||
      manifest == (const ctool_source_t *)0 ||
      inventory == (const ctool_obj_iso_fixture_request_t *)0)
    return iso_error(error, error_capacity, "ISO manifest, inventory and result are required");
  if (manifest->contents.data == (const ctool_u8 *)0 || manifest->contents.size == 0u ||
      manifest->contents.size > ISO_MANIFEST_BYTES)
    return iso_error(error, error_capacity, "ISO manifest is empty, invalid or exceeds its limit");
  if (inventory->entries == (const ctool_obj_iso_fixture_entry_t *)0 ||
      inventory->entry_count == 0u || inventory->entry_count > ISO_ENTRIES)
    return iso_error(error, error_capacity, "ISO inventory requires between one and 512 entries");
  candidate.directories = 1u;
  candidate.files = 0u;
  candidate.directory_depth = 1u;
  candidate.file_bytes = 0u;
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = &inventory->entries[index];
    ctool_u32 prior;
    ctool_u32 depth;
    int directory = entry->kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    seen[index] = 0u;
    if (!directory && entry->kind != CTOOL_OBJ_ISO_FIXTURE_FILE)
      return iso_error(error, error_capacity, "ISO inventory entry kind is invalid");
    if ((directory && entry->source != (const ctool_source_t *)0) ||
        (!directory && entry->source == (const ctool_source_t *)0))
      return iso_error(error, error_capacity, "ISO inventory source does not match its kind");
    if (!directory && entry->source->contents.data == (const ctool_u8 *)0 &&
        entry->source->contents.size != 0u)
      return iso_error(error, error_capacity, "ISO file payload view is invalid");
    if (!iso_path(entry->path, directory, &depth))
      return iso_error(error, error_capacity, "ISO inventory path or directory depth is invalid");
    for (prior = 0u; prior < index; prior++) {
      if (iso_equal(entry->path, inventory->entries[prior].path, 1))
        return iso_error(error, error_capacity, "ISO inventory has a duplicate or case collision");
    }
    if (directory) {
      candidate.directories++;
      if (depth > candidate.directory_depth) candidate.directory_depth = depth;
    } else {
      candidate.files++;
      candidate.file_bytes += (ctool_u64)entry->source->contents.size;
    }
  }
  for (index = 0u; index < inventory->entry_count; index++) {
    ctool_string_t parent = inventory->entries[index].path;
    ctool_u32 length = parent.size;
    ctool_u32 other;
    while (length != 0u && parent.data[length - 1u] != '/') length--;
    if (length == 0u) continue;
    parent.size = length - 1u;
    for (other = 0u; other < inventory->entry_count; other++) {
      if (iso_equal(parent, inventory->entries[other].path, 0) &&
          inventory->entries[other].kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY)
        break;
    }
    if (other == inventory->entry_count)
      return iso_error(error, error_capacity, "ISO inventory entry has no exact directory parent");
  }
  while (offset < manifest->contents.size) {
    ctool_u32 start = offset;
    ctool_u32 end;
    ctool_u32 depth;
    ctool_string_t path;
    while (offset < manifest->contents.size && manifest->contents.data[offset] != '\n')
      offset++;
    end = offset;
    if (end > start && manifest->contents.data[end - 1u] == '\r') end--;
    path.data = (const char *)manifest->contents.data + start;
    path.size = end - start;
    if (!iso_path(path, 0, &depth))
      return iso_error(error, error_capacity, "ISO manifest path is invalid");
    for (index = 0u; index < inventory->entry_count; index++) {
      if (iso_equal(path, inventory->entries[index].path, 0)) break;
      if (iso_equal(path, inventory->entries[index].path, 1))
        return iso_error(error, error_capacity, "ISO manifest has a case collision");
    }
    if (index == inventory->entry_count)
      return iso_error(error, error_capacity, "ISO manifest entry has no captured input");
    if (seen[index])
      return iso_error(error, error_capacity, "ISO manifest contains a duplicate path");
    seen[index] = 1u;
    matched++;
    if (offset < manifest->contents.size) offset++;
  }
  if (matched != inventory->entry_count)
    return iso_error(error, error_capacity, "ISO captured input is absent from the manifest");
  *report = candidate;
  return 1;
}
