#include "cupidbuild_source_stage.h"
#include "cupidbuild_source_path.h"
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_SOURCE_STAGE_PATH_BYTES 8192u

struct cupidbuild_source_stage {
  cupidbuild_source_file_t *file;
  char *requested;
  int failed;
};

static int source_stage_open(const char *working_root,
    const char *request, size_t entry_quota, int selection, cupidbuild_source_stage_t **owner_out,
    cupidbuild_source_stage_view_t *view_out) {
  char absolute[CUPIDBUILD_SOURCE_STAGE_PATH_BYTES];
  cupidbuild_source_stage_t *owner;
  cupidbuild_host_entry_kind_t kind = CUPIDBUILD_ENTRY_NONE;
  size_t length;
  int valid;
  if (view_out) (void)memset(view_out, 0, sizeof(*view_out));
  if (owner_out) *owner_out = (cupidbuild_source_stage_t *)0;
  if (!owner_out || !entry_quota || entry_quota == (size_t)-1 ||
      !cupidbuild_source_path_absolute(working_root, request, absolute, sizeof(absolute))) return 0;
  owner = (cupidbuild_source_stage_t *)calloc(1u, sizeof(*owner));
  if (!owner) return 0;
  length = strlen(absolute);
  owner->requested = (char *)malloc(length + 1u);
  if (!owner->requested) {
    (void)cupidbuild_source_stage_close(owner);
    return 0;
  }
  (void)memcpy(owner->requested, absolute, length + 1u);
  if (cupidbuild_host_source_missing(absolute)) {
    valid = cupidbuild_host_source_missing(owner->requested);
  } else {
    if (selection && view_out) {
      valid = cupidbuild_source_file_open_optional_selection_view_quota(
          working_root, request, entry_quota, &owner->file, &kind, &view_out->file);
    } else {
      valid = selection ? cupidbuild_source_file_open_optional_selection_quota(
          working_root, request, entry_quota, &owner->file, &kind)
          : cupidbuild_source_file_open_optional_quota(working_root, request,
              entry_quota, &owner->file, &kind);
    }
    valid = valid && kind == CUPIDBUILD_ENTRY_FILE;
  }
  if (!valid) {
    if (view_out) (void)memset(view_out, 0, sizeof(*view_out));
    (void)cupidbuild_source_stage_close(owner);
    return 0;
  }
  if (view_out) {
    if (owner->file) view_out->kind = CUPIDBUILD_ENTRY_FILE;
    else view_out->file.requested_absolute = owner->requested;
  }
  *owner_out = owner;
  return 1;
}

int cupidbuild_source_stage_open_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out) {
  return source_stage_open(working_root, request, entry_quota, 0, owner_out, (cupidbuild_source_stage_view_t *)0);
}

int cupidbuild_source_stage_open_selection_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out) {
  return source_stage_open(working_root, request, entry_quota, 1, owner_out, (cupidbuild_source_stage_view_t *)0);
}

int cupidbuild_source_stage_open_selection_view_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out,
    cupidbuild_source_stage_view_t *view_out) {
  if (!view_out) {
    if (owner_out) *owner_out = (cupidbuild_source_stage_t *)0;
    return 0;
  }
  return source_stage_open(working_root, request, entry_quota, 1, owner_out, view_out);
}

int cupidbuild_source_stage_view(cupidbuild_source_stage_t *owner,
                                cupidbuild_source_stage_view_t *view_out) {
  if (view_out) (void)memset(view_out, 0, sizeof(*view_out));
  if (!owner || owner->failed) return 0;
  if (!view_out) {
    owner->failed = 1;
    return 0;
  }
  if (owner->file) {
    if (!cupidbuild_source_file_view(owner->file, &view_out->file)) {
      owner->failed = 1;
      return 0;
    }
    view_out->kind = CUPIDBUILD_ENTRY_FILE;
  } else {
    if (!cupidbuild_host_source_missing(owner->requested)) {
      owner->failed = 1;
      return 0;
    }
    view_out->file.requested_absolute = owner->requested;
  }
  return 1;
}

int cupidbuild_source_stage_close(cupidbuild_source_stage_t *owner) {
  int valid;
  if (!owner) return 1;
  valid = cupidbuild_source_file_close(owner->file);
  free(owner->requested);
  free(owner);
  return valid;
}
