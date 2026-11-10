#ifndef CUPID_TOOLCHAIN_BUILD_SOURCE_STAGE_H
#define CUPID_TOOLCHAIN_BUILD_SOURCE_STAGE_H

#include "cupidbuild_source_file.h"

typedef struct cupidbuild_source_stage cupidbuild_source_stage_t;
typedef struct {
  cupidbuild_host_entry_kind_t kind;
  cupidbuild_source_file_view_t file;
} cupidbuild_source_stage_view_t;

/* Select an optional image stage and own its normalized original request.
 * Present regular files retain the existing optional file owner's observation
 * and parent binding. Missing stages retain only the complete request: their
 * parents may change while that leaf stays absent, as in the image command.
 * Missing views have NONE and only file.requested_absolute; all other file
 * fields are null. Present views have FILE and the complete borrowed file view.
 * Caller strings may be released after success. Zero/SIZE_MAX quotas, linked,
 * special or directory leaves and lookup errors reject and clear owner_out.
 * Capture grants no publication authority and does not freeze payloads. */
int cupidbuild_source_stage_open_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out);
/* Temporary image-stage selection for a complete retained-root owner.
 * Present observers cannot bind publication transactions. Their leaf
 * and parent captures must match a complete retained observer validated
 * before and after use. Missing-path rules and sticky views are unchanged. */
int cupidbuild_source_stage_open_selection_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out);
/* Return the first borrowed view at successful temporary selection.
 * view_out is required; failure clears both supplied outputs. Existing
 * view calls retain their complete validation and permanent rejection. */
int cupidbuild_source_stage_open_selection_view_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_stage_t **owner_out,
    cupidbuild_source_stage_view_t *view_out);
/* Revalidate before exposing borrowed storage. A failed view clears the result
 * and poisons this owner permanently. The owner must outlive its views and any
 * transaction borrowing a present-file observer. Revalidate again after use. */
int cupidbuild_source_stage_view(cupidbuild_source_stage_t *owner,
                                cupidbuild_source_stage_view_t *view_out);
int cupidbuild_source_stage_close(cupidbuild_source_stage_t *owner);

#endif
