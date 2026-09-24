#ifndef CUPID_SEED_MANIFEST_H
#define CUPID_SEED_MANIFEST_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
  char file[32];
  char sha256[65];
  uint32_t size;
} cupid_seed_manifest_artifact_t;
typedef struct {
  uint32_t artifact_count;
  /* PE32 import generation: legacy 0, current ANSI 1, UTF-8 2. ELF32 uses 0. */
  uint32_t current_windows_plan;
  /* Role order: ASM, C, Dis, LD, Obj, Build. Unused legacy rows are zero. */
  cupid_seed_manifest_artifact_t artifacts[6];
} cupid_seed_manifest_result_t;
/* Validate immutable manifest structure, target, supported plan, and provenance
 * for explicit format 1 (ELF32) or 2 (PE32). Both formats work on either host.
 * Release authorization, image bytes, paired manifest binding, and filesystem
 * lifetime are separate checks. Failure clears result; zero diagnostic capacity
 * permits null storage. No pointers are retained or mutable state shared. */
int cupid_seed_manifest_validate(const unsigned char *bytes, size_t size,
    unsigned int format, cupid_seed_manifest_result_t *result,
    char *error, size_t error_capacity);

/* Require both promoted manifests to satisfy their complete supported semantic
 * contracts and the supplied release record. Hash the actual Linux manifest
 * bytes and bind the Windows plan reference to them. Inputs must remain
 * immutable for the call. Image bytes, release authority and retained filesystem
 * identities are separate obligations. Zero diagnostic capacity permits null. */
int cupid_seed_pair_validate(
    const unsigned char *release_bytes, size_t release_size,
    const unsigned char *linux_bytes, size_t linux_size,
    const unsigned char *windows_bytes, size_t windows_size,
    char *error, size_t error_capacity);
#endif
