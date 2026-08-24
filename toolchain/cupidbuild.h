#ifndef CUPID_TOOLCHAIN_BUILD_H
#define CUPID_TOOLCHAIN_BUILD_H

typedef struct {
  const char *seed_manifest;
  const char *repository_root;
  const char *source;
  const char *output;
} cupidbuild_object_request_t;

int cupidbuild_assemble_object(const cupidbuild_object_request_t *request);

#endif
