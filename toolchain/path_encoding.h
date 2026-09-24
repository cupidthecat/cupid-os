#ifndef CUPIDBUILD_PATH_ENCODING_H
#define CUPIDBUILD_PATH_ENCODING_H
#include <stddef.h>

/* Lengths exclude the terminator. Embedded NUL, invalid Unicode and overflow
 * fail. NULL output with zero capacity queries the payload length. Every other
 * output needs room for its terminator. Failure clears length and, when present,
 * the first output element. Inputs and outputs must not overlap. */
int cupidbuild_path_to_utf16(const char *input, size_t bytes,
    unsigned short *output, size_t capacity, size_t *units);
int cupidbuild_path_to_utf8(const unsigned short *input, size_t units,
    char *output, size_t capacity, size_t *bytes);
#endif
