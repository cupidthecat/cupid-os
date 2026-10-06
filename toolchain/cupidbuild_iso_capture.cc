#include "cupidbuild_iso_capture.h"
#include <stdlib.h>
#include <string.h>

#define ISO_CAPTURE_ENTRIES 512u
#define ISO_CAPTURE_PATH 1023u
#define ISO_CAPTURE_MANIFEST ((ISO_CAPTURE_PATH + 2u) * ISO_CAPTURE_ENTRIES)
#define ISO_CAPTURE_HOST_PATH 8191u
#define ISO_CAPTURE_PAYLOAD (64u * 1024u * 1024u)

struct cupidbuild_iso_capture {
  cupidbuild_host_observer_t *observer;
  ctool_source_t manifest;
  char *fixtures;
  ctool_obj_iso_fixture_request_t inventory;
  cupidbuild_iso_inventory_report_t report;
  ctool_obj_iso_fixture_entry_t entries[ISO_CAPTURE_ENTRIES];
  ctool_source_t sources[ISO_CAPTURE_ENTRIES];
  char *names[ISO_CAPTURE_ENTRIES];
  char *logical[ISO_CAPTURE_ENTRIES];
  uint64_t sizes[ISO_CAPTURE_ENTRIES];
  char *manifest_name;
  unsigned char *manifest_bytes;
  unsigned char *payloads[ISO_CAPTURE_ENTRIES];
};

static int iso_capture_error(char *error, ctool_u32 capacity, const char *message) {
  ctool_u32 index = 0u;
  if (error != (char *)0 && capacity != 0u) {
    while (index + 1u < capacity && message[index] != 0) {
      error[index] = message[index]; index++;
    }
    error[index] = 0;
  }
  return 0;
}

static char *iso_capture_copy(const char *data, ctool_u32 size) {
  char *copy = (char *)malloc((size_t)size + 1u);
  if (copy != (char *)0) { (void)memcpy(copy, data, size); copy[size] = 0; }
  return copy;
}

static int iso_capture_host_length(const char *path, ctool_u32 *size) {
  ctool_u32 length = 0u;
  if (path == (const char *)0) return 0;
  while (length <= ISO_CAPTURE_HOST_PATH && path[length] != 0) length++;
  if (length > ISO_CAPTURE_HOST_PATH) return 0;
  *size = length; return 1;
}

/* Reject unsafe spellings before converting byte views to host C strings.
 * The typed inventory validator remains the complete graph/kind policy. */
static int iso_capture_name(const unsigned char *data, ctool_u32 size) {
  ctool_u32 index;
  ctool_u32 start = 0u;
  ctool_u32 components = 0u;
  if (size == 0u || size > ISO_CAPTURE_PATH) return 0;
  for (index = 0u; index <= size; index++) {
    if (index == size || data[index] == '/') {
      ctool_u32 length = index - start;
      if (length == 0u || length > 127u ||
          (length == 1u && data[start] == '.') ||
          (length == 2u && data[start] == '.' && data[start + 1u] == '.')) return 0;
      if (++components > 8u) return 0;
      start = index + 1u;
    } else {
      unsigned char byte = data[index];
      if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-')) return 0;
    }
  }
  return 1;
}

static int iso_capture_parse(cupidbuild_iso_capture_t *capture, ctool_u32 prefix,
                              char *error, ctool_u32 capacity) {
  ctool_u32 offset = 0u;
  while (offset < capture->manifest.contents.size) {
    ctool_u32 start = offset;
    ctool_u32 size;
    ctool_u32 index = capture->inventory.entry_count;
    char *logical;
    while (offset < capture->manifest.contents.size && capture->manifest.contents.data[offset] != '\n') offset++;
    size = offset - start;
    if (size != 0u && capture->manifest.contents.data[start + size - 1u] == '\r') size--;
    if (offset < capture->manifest.contents.size) offset++;
    if (index == ISO_CAPTURE_ENTRIES)
      return iso_capture_error(error, capacity, "ISO manifest exceeds 512 entries");
    if (!iso_capture_name(capture->manifest.contents.data + start, size))
      return iso_capture_error(error, capacity, "ISO manifest path spelling is invalid");
    if (prefix + (prefix != 0u ? 1u : 0u) + size > ISO_CAPTURE_HOST_PATH)
      return iso_capture_error(error, capacity, "ISO host input path exceeds observer storage");
    capture->names[index] = iso_capture_copy((const char *)capture->manifest.contents.data + start, size);
    logical = (char *)malloc((size_t)prefix + size + 2u);
    capture->logical[index] = logical;
    if (capture->names[index] == (char *)0 || logical == (char *)0)
      return iso_capture_error(error, capacity, "ISO capture allocation failed");
    if (prefix != 0u) {
      (void)memcpy(logical, capture->fixtures, prefix); logical[prefix] = '/';
      (void)memcpy(logical + prefix + 1u, capture->names[index], size + 1u);
    } else (void)memcpy(logical, capture->names[index], size + 1u);
    capture->entries[index].path.data = capture->names[index];
    capture->entries[index].path.size = size;
    capture->sources[index].path.text = ctool_string(logical);
    capture->inventory.entry_count++;
  }
  if (capture->inventory.entry_count == 0u)
    return iso_capture_error(error, capacity, "ISO manifest is empty");
  return 1;
}

static int iso_capture_children(cupidbuild_iso_capture_t *capture, const char *directory,
                                 const char *logical, char *error, ctool_u32 capacity) {
  const char *expected[ISO_CAPTURE_ENTRIES];
  ctool_u32 count = 0u;
  ctool_u32 size = (ctool_u32)strlen(directory);
  ctool_u32 index;
  for (index = 0u; index < capture->inventory.entry_count; index++) {
    const char *name = capture->names[index];
    const char *base;
    if (size != 0u) {
      if (capture->entries[index].path.size <= size ||
          memcmp(name, directory, size) != 0 || name[size] != '/') continue;
      base = name + size + 1u;
    } else base = name;
    if (strchr(base, '/') == (char *)0) expected[count++] = base;
  }
  if (!cupidbuild_host_observer_directory(capture->observer, logical,
          count == 0u ? (const char *const *)0 : expected, count))
    return iso_capture_error(error, capacity, cupidbuild_host_observer_error(capture->observer));
  return 1;
}

void cupidbuild_iso_capture_close(cupidbuild_iso_capture_t *capture) {
  ctool_u32 index;
  if (capture == (cupidbuild_iso_capture_t *)0) return;
  for (index = 0u; index < ISO_CAPTURE_ENTRIES; index++) {
    free(capture->names[index]); free(capture->logical[index]);
    free(capture->payloads[index]);
  }
  free(capture->manifest_bytes); free(capture->manifest_name);
  free(capture->fixtures); free(capture);
}

int cupidbuild_iso_capture_open(cupidbuild_host_observer_t *observer,
                                const char *manifest_logical, const char *fixtures_logical,
                                cupidbuild_iso_capture_t **capture_out,
                                char *error, ctool_u32 error_capacity) {
  cupidbuild_iso_capture_t *capture;
  ctool_u32 manifest_length;
  ctool_u32 fixtures_length;
  ctool_u32 index;
  unsigned char *bytes = (unsigned char *)0;
  uint64_t size = 0u;
  uint64_t total = 0u;
  cupidbuild_host_entry_kind_t kind;
  const char *failure = (const char *)0;
  if (capture_out != (cupidbuild_iso_capture_t **)0) *capture_out = (cupidbuild_iso_capture_t *)0;
  if (error != (char *)0 && error_capacity != 0u) error[0] = 0;
  if (observer == (cupidbuild_host_observer_t *)0 || capture_out == (cupidbuild_iso_capture_t **)0 ||
      (error == (char *)0 && error_capacity != 0u) ||
      !iso_capture_host_length(manifest_logical, &manifest_length) || manifest_length == 0u ||
      !iso_capture_host_length(fixtures_logical, &fixtures_length))
    return iso_capture_error(error, error_capacity, "ISO observer, logical paths and capture result are required");
  capture = (cupidbuild_iso_capture_t *)calloc(1u, sizeof(*capture));
  if (capture == (cupidbuild_iso_capture_t *)0)
    return iso_capture_error(error, error_capacity, "ISO capture allocation failed");
  capture->observer = observer; capture->inventory.entries = capture->entries;
  capture->manifest_name = iso_capture_copy(manifest_logical, manifest_length);
  capture->manifest.path.text.data = capture->manifest_name;
  capture->manifest.path.text.size = manifest_length;
  capture->fixtures = iso_capture_copy(fixtures_logical, fixtures_length);
  if (capture->manifest.path.text.data == (const char *)0 || capture->fixtures == (char *)0) {
    failure = "ISO capture allocation failed"; goto failed;
  }
  if (!cupidbuild_host_observer_file(observer, manifest_logical, ISO_CAPTURE_MANIFEST, &bytes, &size)) {
    failure = cupidbuild_host_observer_error(observer); goto failed;
  }
  capture->manifest_bytes = bytes;
  capture->manifest.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (!iso_capture_parse(capture, fixtures_length, error, error_capacity)) goto parsed_failure;
  if (!cupidbuild_host_observer_kind(observer, fixtures_logical, &kind)) {
    failure = cupidbuild_host_observer_error(observer); goto failed;
  }
  if (kind != CUPIDBUILD_ENTRY_DIRECTORY) { failure = "ISO fixture root must be a directory"; goto failed; }
  for (index = 0u; index < capture->inventory.entry_count; index++) {
    if (!cupidbuild_host_observer_kind(observer, capture->logical[index], &kind)) {
      failure = cupidbuild_host_observer_error(observer); goto failed;
    }
    if (kind == CUPIDBUILD_ENTRY_DIRECTORY) capture->entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    else {
      capture->entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
      capture->entries[index].source = &capture->sources[index];
    }
  }
  if (!cupidbuild_iso_inventory_validate(&capture->manifest, &capture->inventory,
          &capture->report, error, error_capacity)) goto parsed_failure;
  for (index = 0u; index < capture->inventory.entry_count; index++) {
    if (capture->entries[index].kind != CTOOL_OBJ_ISO_FIXTURE_FILE) continue;
    if (!cupidbuild_host_observer_file(observer, capture->logical[index], ISO_CAPTURE_PAYLOAD,
            (unsigned char **)0, &capture->sizes[index])) {
      failure = cupidbuild_host_observer_error(observer); goto failed;
    }
    if (capture->sizes[index] > ISO_CAPTURE_PAYLOAD) {
      failure = "ISO fixture file exceeds retained observer payload storage"; goto failed;
    }
    total += capture->sizes[index];
    if (total > 0xffffffffu) { failure = "ISO fixture payloads exceed 32-bit image storage"; goto failed; }
  }
  for (index = 0u; index < capture->inventory.entry_count; index++) {
    if (capture->entries[index].kind != CTOOL_OBJ_ISO_FIXTURE_FILE) continue;
    bytes = (unsigned char *)0;
    if (!cupidbuild_host_observer_file(observer, capture->logical[index], ISO_CAPTURE_PAYLOAD, &bytes, &size)) {
      failure = cupidbuild_host_observer_error(observer); goto failed;
    }
    capture->payloads[index] = bytes;
    capture->sources[index].contents = ctool_bytes(bytes, (ctool_u32)size);
    if (size != capture->sizes[index]) { failure = "ISO fixture file changed during capture"; goto failed; }
  }
  if (!cupidbuild_iso_inventory_validate(&capture->manifest, &capture->inventory,
          &capture->report, error, error_capacity)) goto parsed_failure;
  if (!iso_capture_children(capture, "", fixtures_logical, error, error_capacity)) goto parsed_failure;
  for (index = 0u; index < capture->inventory.entry_count; index++) {
    if (capture->entries[index].kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY &&
        !iso_capture_children(capture, capture->names[index], capture->logical[index], error, error_capacity))
      goto parsed_failure;
  }
  if (!cupidbuild_host_observer_require_unchanged(observer)) {
    failure = cupidbuild_host_observer_error(observer); goto failed;
  }
  *capture_out = capture; return 1;
failed:
  (void)iso_capture_error(error, error_capacity, failure);
parsed_failure:
  cupidbuild_iso_capture_close(capture); return 0;
}

const ctool_source_t *cupidbuild_iso_capture_manifest(const cupidbuild_iso_capture_t *capture) {
  return capture == (const cupidbuild_iso_capture_t *)0 ? (const ctool_source_t *)0 : &capture->manifest;
}

const ctool_obj_iso_fixture_request_t *cupidbuild_iso_capture_inventory(const cupidbuild_iso_capture_t *capture) {
  return capture == (const cupidbuild_iso_capture_t *)0 ? (const ctool_obj_iso_fixture_request_t *)0 : &capture->inventory;
}

const cupidbuild_iso_inventory_report_t *cupidbuild_iso_capture_report(const cupidbuild_iso_capture_t *capture) {
  return capture == (const cupidbuild_iso_capture_t *)0 ? (const cupidbuild_iso_inventory_report_t *)0 : &capture->report;
}

int cupidbuild_iso_capture_require_unchanged(cupidbuild_iso_capture_t *capture) {
  return capture != (cupidbuild_iso_capture_t *)0 && cupidbuild_host_observer_require_unchanged(capture->observer);
}
