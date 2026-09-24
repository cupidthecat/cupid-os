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
typedef cupidbuild_assembly_request_t cupidbuild_compile_request_t;

typedef struct {
  const char *seed_manifest;
  const char *repository_root;
  const char *input_manifest;
  const char *output;
} cupidbuild_kernel_request_t;

typedef struct {
  const char *seed_manifest;
  const char *repository_root;
  const char *output;
} cupidbuild_profile_request_t;

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
int cupidbuild_assemble_iso_pattern(
    const cupidbuild_assembly_request_t *request);
int cupidbuild_embed_jpeg(const cupidbuild_jpeg_request_t *request);
int cupidbuild_generate_ksyms(const cupidbuild_ksyms_request_t *request);
int cupidbuild_compile_kernel(const cupidbuild_compile_request_t *request);
int cupidbuild_compile_doom(const cupidbuild_compile_request_t *request);
int cupidbuild_compile_production(const cupidbuild_compile_request_t *request);
typedef enum {
  CUPIDBUILD_SEED_ELF32 = 1,
  CUPIDBUILD_SEED_PE32 = 2
} cupidbuild_seed_image_format_t;

/* Validate immutable captured bytes without selecting or launching a tool.
 * Artifact order is CupidASM, CupidC, CupidDis, CupidLD, CupidObj, CupidBuild.
 * The legacy cohort contains the first five roles. promoted must be zero or
 * one. current_windows_plan selects legacy (0), current ANSI (1), or UTF-8 (2)
 * imports. Nonzero selections require promoted PE32 input.
 * The caller retains ownership and proves capture identity and release trust.
 * Returns one only for the exact format, entry point, and role import profile.
 * This does not validate a manifest, digest, filesystem, or release identity. */
int cupidbuild_validate_seed_image_bytes(
    const unsigned char *bytes, size_t size,
    cupidbuild_seed_image_format_t format, size_t artifact_index,
    int promoted, int current_windows_plan);

int cupidbuild_validate_compiler_object_bytes(const unsigned char *bytes,
                                              size_t size);
/* Validate a borrowed, immutable candidate against the external user loader.
 * No filesystem, allocation, publication, or instruction inspection occurs.
 * The caller keeps bytes alive for this call and supplies a nonempty reason
 * buffer distinct from the input. Success clears reason; failure terminates it.
 */
int cupidbuild_validate_user_executable_bytes(const unsigned char *bytes,
                                             size_t size, char *reason,
                                             size_t reason_capacity);
int cupidbuild_flatten_kernel(const cupidbuild_kernel_request_t *request);
int cupidbuild_generate_profile_manifest(
    const cupidbuild_profile_request_t *request);
int cupidbuild_validate_jpeg_bytes(const unsigned char *bytes, size_t size,
                                   char *reason, size_t reason_capacity);
int cupidbuild_validate_jpeg_object_bytes(
    const unsigned char *object_bytes, size_t object_size,
    const unsigned char *jpeg_bytes, size_t jpeg_size,
    const char *source_identity);
int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request);

#endif
