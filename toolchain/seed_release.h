#ifndef CUPID_SEED_RELEASE_H
#define CUPID_SEED_RELEASE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t size;
  char sha256[65];
} cupid_seed_release_artifact_t;

typedef struct {
  char source_revision[41];
  char source_snapshot_sha256[65];
  uint32_t source_input_count;
  char parent_source_revision[41];
  char parent_linux_manifest_sha256[65];
  char parent_windows_manifest_sha256[65];
  char linux_plan_sha256[65];
  char windows_plan_sha256[65];
  /* Formats: ELF32, PE32. Roles: ASM, C, Dis, LD, Obj, Build. */
  cupid_seed_release_artifact_t artifacts[2][6];
} cupid_seed_release_t;

/* Parse caller-owned immutable release JSON, retaining no pointers. This
 * validates the record's shape, not its authority or any observed seed.
 * Failure clears result. Nonzero error_capacity requires writable storage;
 * zero permits a null error. Input and output storage must not overlap. */
int cupid_seed_release_parse(const unsigned char *bytes, size_t size,
                             cupid_seed_release_t *result,
                             char *error, size_t error_capacity);

/* Match a manifest's release-dependent claims against an immutable release
 * record. Format is 1 for ELF32 or 2 for PE32. This supplements full manifest,
 * build-plan, image, filesystem, and paired-manifest validation; it does not
 * replace them. In particular, a declared plan digest is matched here, while
 * the plan's actual semantics and digest must be independently verified.
 * Neither input pointer is retained. Diagnostics follow parse's contract. */
int cupid_seed_release_match_manifest(
    const unsigned char *release_bytes, size_t release_size,
    const unsigned char *manifest_bytes, size_t manifest_size,
    unsigned int format, char *error, size_t error_capacity);
#endif
