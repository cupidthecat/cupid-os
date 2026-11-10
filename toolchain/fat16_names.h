#ifndef CUPID_TOOLCHAIN_FAT16_NAMES_H
#define CUPID_TOOLCHAIN_FAT16_NAMES_H

#include "fat16_stage.h"

typedef enum {
  CTOOL_FAT16_NAMES_UNICODE_15 = 15,
  CTOOL_FAT16_NAMES_UNICODE_16 = 16
} ctool_fat16_name_profile_t;

/* Preserve the existing hosted writer's uppercase, character filtering,
 * final-dot split, six-character/~1 long-stem rule and three-character extension.
 * The explicit profile freezes the Unicode behavior with the request.
 * A component contains no slash or backslash. Invalid UTF-8, embedded NUL,
 * empty/dot/parent components, empty filtered stems and retained non-ASCII
 * characters fail. Non-ASCII characters discarded by filtering or truncation
 * remain valid. Failure leaves the output unchanged.
 *
 * A guest destination starts with '/'. Either separator divides components;
 * repeated and trailing separators are ignored. Validate every component before
 * writing any output. NULL output with zero capacity queries the required count.
 * Insufficient capacity returns LIMIT with that count and leaves output unchanged.
 * Other failures clear the count. Views, output and count must be disjoint. */
ctool_status_t ctool_fat16_project_component(ctool_string_t component,
    ctool_fat16_name_profile_t profile, ctool_fat16_name_t *name_out);
ctool_status_t ctool_fat16_project_destination(ctool_string_t destination,
    ctool_fat16_name_profile_t profile, ctool_fat16_name_t *components_out,
    ctool_u32 capacity, ctool_u32 *count_out);

#endif
