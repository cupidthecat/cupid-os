#ifndef CUPID_TOOLCHAIN_BUILD_SOURCE_FILE_H
#define CUPID_TOOLCHAIN_BUILD_SOURCE_FILE_H

#include "cupidbuild_host.h"

typedef struct cupidbuild_source_file cupidbuild_source_file_t;
typedef struct {
  cupidbuild_host_observer_t *observer;
  const char *physical_root;
  const char *logical;
  const char *requested_absolute;
  const char *physical_absolute;
} cupidbuild_source_file_view_t;

/* Select one required regular input through the lexical source-path rules.
 * Resolve and retain its original parent request before observing the no-follow
 * file leaf. Both caller strings may be released after success. Zero/SIZE_MAX
 * quotas and invalid inputs reject; a supplied owner output clears on failure.
 * Capture starts with metadata. Retained streaming through the view's observer
 * adds payload custody; metadata alone cannot detect restored-time edits. */
int cupidbuild_source_file_open_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out);
/* Select a regular file or retain its first absence beneath the nearest existing
 * parent. Existing parent aliases resolve before no-follow suffix observation.
 * kind_out is required: FILE or NONE on success, NONE on failure. Failure clears
 * a supplied owner output. Both outputs remain valid only while views revalidate;
 * later appearance, disappearance or parent-binding drift poisons the owner.
 * Optional views check the root's directory identity, allowing unrelated root
 * writes unless exact membership was captured. Required views retain their
 * complete root-metadata checks.
 * Directory, linked and special leaves reject. View layout and required-file
 * selection retain their contracts. */
int cupidbuild_source_file_open_optional_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out,
    cupidbuild_host_entry_kind_t *kind_out);
/* Temporary selections for a caller that owns complete retained roots.
 * Their observers retain the selected parent and leaf, but cannot bind
 * publication transactions. Compare captured leaf/parent identities and
 * bracket their use with complete-root validation. Ordinary constructors
 * retain their original full physical-ancestor custody. */
int cupidbuild_source_file_open_selection_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out);
int cupidbuild_source_file_open_optional_selection_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out,
    cupidbuild_host_entry_kind_t *kind_out);
/* Return the first borrowed view at the successful selection-constructor
 * validation point. view_out is required; failure clears supplied view,
 * owner and kind outputs. No second identical validation is required.
 * This view has the same temporary-selection scope and lifetime above. */
int cupidbuild_source_file_open_selection_view_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out,
    cupidbuild_source_file_view_t *view_out);
int cupidbuild_source_file_open_optional_selection_view_quota(const char *working_root,
    const char *request, size_t entry_quota, cupidbuild_source_file_t **owner_out,
    cupidbuild_host_entry_kind_t *kind_out, cupidbuild_source_file_view_t *view_out);
/* Revalidate the original parent binding and all captured observations before
 * returning borrowed strings and observer. Failure clears the supplied view.
 * Strings and observer belong to the owner through close. Close borrowing
 * transactions before the owner; revalidate before and after consuming views. */
int cupidbuild_source_file_view(cupidbuild_source_file_t *owner,
                               cupidbuild_source_file_view_t *view_out);
int cupidbuild_source_file_close(cupidbuild_source_file_t *owner);

#endif
