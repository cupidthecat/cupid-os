#ifndef CUPID_ARTIFACT_SIZE_POLICY_H
#define CUPID_ARTIFACT_SIZE_POLICY_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t artifact_count;
  uint64_t total_exact_bytes;
} artifact_size_policy_result_t;

/* The caller owns immutable CUPSIZE2 bytes throughout this synchronous call.
 * This verifies observations, not files or release pins. No pointer is retained.
 * Returns one on success, zero on failure; failure clears a supplied result.
 * A nonzero error_capacity requires writable error storage and guarantees a
 * terminated diagnostic. Zero capacity permits a null error pointer.
 * Calls share no mutable state. The caller must keep output storage separate
 * from input bytes and other concurrently executing calls' output storage. */
int artifact_size_policy_validate(const unsigned char *bytes, size_t size,
                                  artifact_size_policy_result_t *result,
                                  char *error, size_t error_capacity);
#endif
