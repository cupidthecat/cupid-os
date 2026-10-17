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
int cupidbuild_compile_user(const cupidbuild_compile_request_t *request);

typedef cupidbuild_object_request_t cupidbuild_user_link_request_t;
/* Link an already normalized, physical user object/output pair. Root is
 * absolute; source and output are relative with forward-slash separators.
 * Both leaves share an existing directory below user/, source is output + .o,
 * and output is cat, hello or ls. No directories are created. Filesystem
 * aliases must be resolved and pinned by a higher-level path adapter before
 * calling this boundary; this operation rejects links in the physical chain.
 * The promoted six-tool seed, object, candidate and entire output-parent chain
 * remain retained through instruction validation and atomic publication.
 */
int cupidbuild_link_user_object(const cupidbuild_user_link_request_t *request);
/* Resolve existing repository and parent aliases, then approve the physical
 * cat/hello/ls object pair and keep its retained chain through publication. */
int cupidbuild_link_user(const cupidbuild_user_link_request_t *request);

/* Explicit release authority for typed seeded transactions.
 * Existing requests and entry points retain their ABI and historical rules.
 * The caller authorizes the release record; its frozen bytes and observations
 * stay in the transaction through validation and publication.
 * A null release path selects the historical reader. */
int cupidbuild_assemble_object_with_release(
    const cupidbuild_assembly_request_t *request, const char *seed_release);
int cupidbuild_assemble_bootloader_with_release(
    const cupidbuild_assembly_request_t *request, const char *seed_release);
int cupidbuild_assemble_smp_trampoline_with_release(
    const cupidbuild_assembly_request_t *request, const char *seed_release);
int cupidbuild_assemble_iso_pattern_with_release(
    const cupidbuild_assembly_request_t *request, const char *seed_release);
int cupidbuild_embed_jpeg_with_release(
    const cupidbuild_jpeg_request_t *request, const char *seed_release);
int cupidbuild_generate_ksyms_with_release(
    const cupidbuild_ksyms_request_t *request, const char *seed_release);
int cupidbuild_flatten_kernel_with_release(
    const cupidbuild_kernel_request_t *request, const char *seed_release);
int cupidbuild_generate_profile_manifest_with_release(
    const cupidbuild_profile_request_t *request, const char *seed_release);
int cupidbuild_compile_kernel_with_release(
    const cupidbuild_compile_request_t *request, const char *seed_release);
int cupidbuild_compile_doom_with_release(
    const cupidbuild_compile_request_t *request, const char *seed_release);
int cupidbuild_compile_production_with_release(
    const cupidbuild_compile_request_t *request, const char *seed_release);
int cupidbuild_compile_user_with_release(
    const cupidbuild_compile_request_t *request, const char *seed_release);
int cupidbuild_link_user_object_with_release(
    const cupidbuild_user_link_request_t *request, const char *seed_release);
int cupidbuild_link_user_with_release(
    const cupidbuild_user_link_request_t *request, const char *seed_release);

#define CUPIDBUILD_USER_PATH_BYTES 8192u
typedef struct {
  char repository_root[CUPIDBUILD_USER_PATH_BYTES];
  char source[CUPIDBUILD_USER_PATH_BYTES];
  char output[CUPIDBUILD_USER_PATH_BYTES];
} cupidbuild_user_compile_paths_t;
/* Resolve lexical aliases and enforce the existing user source/output binding.
 * windows selects Windows (1) or POSIX (0) path syntax, independent of this host.
 * Root must be absolute; source/output may be absolute or relative to it.
 * No filesystem access occurs: callers must separately pin every component,
 * reject links/aliases, and retain those observations through publication.
 * Inputs and writable output storage must be disjoint. Failure clears result.
 * Zero error capacity permits NULL; otherwise error must be writable.
 */
int cupidbuild_resolve_user_compile_paths(
    const char *root, const char *source, const char *output, int windows,
    cupidbuild_user_compile_paths_t *result, char *error, size_t error_capacity);

typedef enum {
  CUPIDBUILD_SEED_ELF32 = 1,
  CUPIDBUILD_SEED_PE32 = 2
} cupidbuild_seed_image_format_t;

/* Validate immutable captured bytes without selecting or launching a tool.
 * Artifact order is CupidASM, CupidC, CupidDis, CupidLD, CupidObj, CupidBuild.
 * The legacy cohort contains the first five roles. promoted must be zero or
 * one. current_windows_plan selects legacy (0), current ANSI (1), UTF-8 (2),
 * UTF-8 long paths (3), or directory-alias profiles (4 default, 5 long paths).
 * Each selection requires exact role imports. Nonzero selections require promoted PE32 input.
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
/* Explicit release authority for a promoted checked-tool seed. The request ABI
 * is unchanged. The caller authorizes the release record; both files and the
 * six tool images are frozen and retained through launch and final validation.
 * A null release path preserves run_checked_tool's historical manifest rules. */
int cupidbuild_run_checked_tool_with_release(
    const cupidbuild_run_request_t *request, const char *seed_release);

#endif
