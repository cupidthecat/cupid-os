#ifndef CUPID_TOOLCHAIN_BUILD_H
#define CUPID_TOOLCHAIN_BUILD_H

typedef struct {
  const char *seed_manifest;
  const char *repository_root;
  const char *source;
  const char *output;
} cupidbuild_assembly_request_t;

typedef cupidbuild_assembly_request_t cupidbuild_object_request_t;

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
int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request);

#endif
