#ifndef CUPID_TOOLCHAIN_BUILD_HOST_H
#define CUPID_TOOLCHAIN_BUILD_HOST_H

#include <stdint.h>
#if !defined(CUPID_HOSTED_SIZE_T_DEFINED)
#include <stddef.h>
#endif

typedef struct cupidbuild_host_transaction cupidbuild_host_transaction_t;

typedef struct {
  size_t size;
  unsigned char sha256[32];
  unsigned int identity[4];
  unsigned int modified[2];
  int present;
} cupidbuild_host_snapshot_t;

int cupidbuild_host_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical, cupidbuild_host_transaction_t **transaction_out);
int cupidbuild_host_runner_open(
    const char *working_directory,
    cupidbuild_host_transaction_t **transaction_out);
int cupidbuild_host_transaction_close(
    cupidbuild_host_transaction_t *transaction);

int cupidbuild_host_freeze_input(cupidbuild_host_transaction_t *transaction,
                                 const char *live_path,
                                 const char *private_name,
                                 const char **frozen_path_out,
                                 cupidbuild_host_snapshot_t *snapshot_out);
unsigned char *cupidbuild_host_read_frozen_input(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path,
    size_t limit, size_t *size_out);
int cupidbuild_host_make_input_executable(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path);
int cupidbuild_host_seed_members_exact(const char *directory,
                                       const char *suffix,
                                       const char *const *expected,
                                       size_t expected_count);
const char *cupidbuild_host_frozen_source(
    const cupidbuild_host_transaction_t *transaction);
const char *cupidbuild_host_candidate(
    const cupidbuild_host_transaction_t *transaction);
const char *cupidbuild_host_private_output(
    const cupidbuild_host_transaction_t *transaction);

int cupidbuild_host_run(cupidbuild_host_transaction_t *transaction,
                        const char *tool, const char *const *arguments,
                        unsigned int timeout_milliseconds);
int cupidbuild_host_run_to_private_output(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds);
int cupidbuild_host_run_captured(cupidbuild_host_transaction_t *transaction,
                                 const char *tool,
                                 const char *const *arguments,
                                 unsigned int timeout_milliseconds);
int cupidbuild_host_forward_captured(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_capture_candidate(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out,
    unsigned char **bytes_out);
int cupidbuild_host_require_candidate(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected);
int cupidbuild_host_capture_private_output(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out,
    unsigned char **bytes_out);
int cupidbuild_host_require_private_output(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected);
int cupidbuild_host_require_inputs(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_require_frozen_inputs(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_require_publication_boundary(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_publish(cupidbuild_host_transaction_t *transaction);

const char *cupidbuild_host_error(
    const cupidbuild_host_transaction_t *transaction);

#endif
