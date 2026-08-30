#ifndef CUPID_TOOLCHAIN_BUILD_H
#define CUPID_TOOLCHAIN_BUILD_H

#if !defined(CUPID_HOSTED_SIZE_T_DEFINED)
#include <stddef.h>
#endif

typedef struct {
  const char *seed_manifest;
  const char *repository_root;
  const char *source;
  const char *output;
} cupidbuild_assembly_request_t;

typedef cupidbuild_assembly_request_t cupidbuild_object_request_t;
typedef cupidbuild_assembly_request_t cupidbuild_jpeg_request_t;
typedef cupidbuild_assembly_request_t cupidbuild_ksyms_request_t;
typedef cupidbuild_assembly_request_t cupidbuild_kernel_request_t;

typedef struct {
  const char *seed_manifest;
  const char *working_directory;
  const char *tool;
  const char *const *arguments;
  unsigned int timeout_seconds;
} cupidbuild_run_request_t;

int cupidbuild_assemble_object(const cupidbuild_assembly_request_t *request);
int cupidbuild_assemble_bootloader(
    const cupidbuild_assembly_request_t *request);
int cupidbuild_assemble_smp_trampoline(
    const cupidbuild_assembly_request_t *request);
int cupidbuild_embed_jpeg(const cupidbuild_jpeg_request_t *request);
int cupidbuild_generate_ksyms(const cupidbuild_ksyms_request_t *request);
int cupidbuild_flatten_kernel(const cupidbuild_kernel_request_t *request);
int cupidbuild_validate_jpeg_bytes(const unsigned char *bytes, size_t size,
                                   char *reason, size_t reason_capacity);
int cupidbuild_validate_jpeg_object_bytes(
    const unsigned char *object_bytes, size_t object_size,
    const unsigned char *jpeg_bytes, size_t jpeg_size,
    const char *source_identity);
int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request);

#endif
