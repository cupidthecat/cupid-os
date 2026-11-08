#ifndef CUPIDBUILD_DISK_INTEGER_H
#define CUPIDBUILD_DISK_INTEGER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  CUPIDBUILD_DISK_INTEGER_OK = 0,
  CUPIDBUILD_DISK_INTEGER_SYNTAX = 1,
  CUPIDBUILD_DISK_INTEGER_UTF8 = 2,
  CUPIDBUILD_DISK_INTEGER_PROFILE = 3,
  CUPIDBUILD_DISK_INTEGER_DIGIT_LIMIT = 4,
  CUPIDBUILD_DISK_INTEGER_ARGUMENT = 5
} cupidbuild_disk_integer_status_t;

typedef struct {
  uint32_t magnitude;
  uint32_t digits;
  unsigned int magnitude_fits_u32;
  unsigned int negative;
  unsigned int zero;
} cupidbuild_disk_integer_t;

/* Interpret a complete UTF-8 decimal spelling, including signs, digit
 * separators and the selected Unicode 15 or 16 digit/space profile. A zero
 * digit_limit is unlimited. Valid larger magnitudes return OK with
 * magnitude_fits_u32 clear and magnitude saturated; the caller decides when
 * to reject geometry, after repeated options have selected their final value.
 * Failure clears the result. This function performs no allocation or I/O. */
cupidbuild_disk_integer_status_t cupidbuild_disk_integer_parse(
    const unsigned char *text, size_t size, unsigned int unicode_major,
    uint32_t digit_limit, cupidbuild_disk_integer_t *result_out);

#endif
