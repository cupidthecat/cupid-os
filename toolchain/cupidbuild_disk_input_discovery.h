#ifndef CUPIDBUILD_DISK_INPUT_DISCOVERY_H
#define CUPIDBUILD_DISK_INPUT_DISCOVERY_H

#include "cupidbuild_disk_options.h"
#include "cupidbuild_source_stage.h"

typedef struct cupidbuild_disk_input_discovery cupidbuild_disk_input_discovery_t;
typedef struct {
  cupidbuild_host_entry_kind_t kind;
  cupidbuild_source_file_view_t file;
  const char *destination;
} cupidbuild_disk_input_stage_view_t;
typedef struct {
  const char *working_root_requested;
  const char *working_root_physical;
  cupidbuild_host_observer_t *primary_observer;
  const char *image_requested_absolute;
  cupidbuild_source_file_view_t seed_manifest;
  cupidbuild_source_file_view_t bootloader;
  cupidbuild_source_file_view_t kernel;
  const cupidbuild_disk_input_stage_view_t *stages;
  size_t stage_count;
  size_t external_root_count;
  cupidbuild_disk_integer_t hdd_mb;
  cupidbuild_disk_integer_t fat_start_lba;
  unsigned int force_format;
} cupidbuild_disk_input_discovery_view_t;

/* Copy a valid interpreted options view and retain its complete source paths.
 * The caller may release argv and its option owner after success. At most 4096
 * stages and 64 external retained roots are accepted. external_root_limit may
 * be zero; entry_quota is a per-observer budget and rejects zero/SIZE_MAX.
 * Primary descendants share the working-root observer. External roots reuse
 * retained directory identity. Every original request is separately rechecked.
 * Destination bytes and geometry are copied; their publication policy belongs
 * to the image caller. This neither creates output parents nor publishes,
 * freezes payloads, validates a release, or grants publication authority. */
int cupidbuild_disk_input_discovery_open_quota(const char *working_root,
    const cupidbuild_disk_options_view_t *options, size_t entry_quota,
    size_t external_root_limit, cupidbuild_disk_input_discovery_t **owner_out);
/* Complete observer checks bracket fresh selection of every original input.
 * Missing stages use the image command's complete-path absence contract.
 * Failure clears a supplied view and permanently poisons this owner.
 * All storage and observers are borrowed through close. Revalidate before
 * consuming the view and after use; close borrowing transactions first. */
int cupidbuild_disk_input_discovery_view(cupidbuild_disk_input_discovery_t *owner,
    cupidbuild_disk_input_discovery_view_t *view_out);
int cupidbuild_disk_input_discovery_close(cupidbuild_disk_input_discovery_t *owner);

#endif
