#include "cupidbuild_disk_integer.h"

#include <string.h>

typedef struct {
    uint32_t first;
    unsigned int since;
} cupidbuild_disk_integer_digit_range_t;

typedef struct {
    uint32_t codepoint;
    unsigned int profiles;
} cupidbuild_disk_integer_space_t;

#include "cupidbuild_disk_integer_tables.inc"

_Static_assert(sizeof(uint32_t) == 4, "image integer magnitude needs 32 bits");

static int cupidbuild_disk_integer_next(const unsigned char *text, size_t size, size_t *offset,
                                        uint32_t *value_out) {
    size_t position = *offset;
    uint32_t value;
    unsigned int count;
    unsigned int index;
    unsigned int first;
    if (position >= size)
        return 0;
    first = text[position++];
    if (first < 0x80u) {
        *offset = position;
        *value_out = first;
        return 1;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        count = 1u;
        value = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        count = 2u;
        value = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        count = 3u;
        value = first & 0x07u;
    } else {
        return 0;
    }
    if (size - position < count)
        return 0;
    for (index = 0; index < count; index++) {
        unsigned int byte = text[position++];
        if ((byte & 0xc0u) != 0x80u)
            return 0;
        value = (value << 6) | (byte & 0x3fu);
    }
    if ((count == 1u && value < 0x80u) || (count == 2u && value < 0x800u) ||
        (count == 3u && value < 0x10000u) || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu))
        return 0;
    *offset = position;
    *value_out = value;
    return 1;
}

static int cupidbuild_disk_integer_digit(uint32_t value, unsigned int unicode_major) {
    size_t index;
    for (index = 0;
         index < sizeof(cupidbuild_disk_integer_digits) / sizeof(cupidbuild_disk_integer_digits[0]);
         index++) {
        const cupidbuild_disk_integer_digit_range_t *range = &cupidbuild_disk_integer_digits[index];
        if (range->since <= unicode_major && value >= range->first && value - range->first < 10u)
            return (int)(value - range->first);
    }
    return -1;
}

static int cupidbuild_disk_integer_space(uint32_t value, unsigned int unicode_major) {
    unsigned int profile = unicode_major == 15u ? 1u : 2u;
    size_t index;
    for (index = 0;
         index < sizeof(cupidbuild_disk_integer_spaces) / sizeof(cupidbuild_disk_integer_spaces[0]);
         index++) {
        const cupidbuild_disk_integer_space_t *space = &cupidbuild_disk_integer_spaces[index];
        if (space->codepoint == value && (space->profiles & profile) != 0u)
            return 1;
    }
    return 0;
}

cupidbuild_disk_integer_status_t
cupidbuild_disk_integer_parse(const unsigned char *text, size_t size, unsigned int unicode_major,
                              uint32_t digit_limit, cupidbuild_disk_integer_t *result_out) {
    cupidbuild_disk_integer_t parsed;
    size_t offset = 0;
    unsigned int phase = 0;
    unsigned int last_was_digit = 0;
    unsigned int negative = 0;
    uint32_t digit_count = 0;
    uint32_t magnitude = 0;
    unsigned int overflow = 0;
    unsigned int nonzero = 0;
    unsigned int syntax_error = 0;
    if (!result_out)
        return CUPIDBUILD_DISK_INTEGER_ARGUMENT;
    memset(result_out, 0, sizeof(*result_out));
    if (!text && size != 0)
        return CUPIDBUILD_DISK_INTEGER_ARGUMENT;
    if (unicode_major != 15u && unicode_major != 16u)
        return CUPIDBUILD_DISK_INTEGER_PROFILE;
    while (offset < size) {
        uint32_t value;
        int digit;
        int whitespace;
        if (!cupidbuild_disk_integer_next(text, size, &offset, &value))
            return CUPIDBUILD_DISK_INTEGER_UTF8;
        if (syntax_error)
            continue;
        whitespace = cupidbuild_disk_integer_space(value, unicode_major);
        if (phase == 0u) {
            if (whitespace)
                continue;
            phase = 1u;
            if (value == (uint32_t)'+' || value == (uint32_t)'-') {
                negative = value == (uint32_t)'-';
                continue;
            }
        }
        if (phase == 2u) {
            if (!whitespace)
                syntax_error = 1u;
            continue;
        }
        digit = cupidbuild_disk_integer_digit(value, unicode_major);
        if (digit >= 0) {
            if (digit_count == UINT32_MAX)
                return CUPIDBUILD_DISK_INTEGER_ARGUMENT;
            digit_count++;
            if (digit != 0)
                nonzero = 1u;
            if (!overflow) {
                if (magnitude > (UINT32_MAX - (uint32_t)digit) / 10u) {
                    overflow = 1u;
                    magnitude = UINT32_MAX;
                } else {
                    magnitude = magnitude * 10u + (uint32_t)digit;
                }
            }
            last_was_digit = 1u;
        } else if (value == (uint32_t)'_' && last_was_digit) {
            last_was_digit = 0u;
        } else if (whitespace && digit_count != 0u && last_was_digit) {
            phase = 2u;
        } else {
            syntax_error = 1u;
        }
    }
    if (syntax_error || digit_count == 0u || !last_was_digit)
        return CUPIDBUILD_DISK_INTEGER_SYNTAX;
    if (digit_limit != 0u && digit_count > digit_limit)
        return CUPIDBUILD_DISK_INTEGER_DIGIT_LIMIT;
    parsed.magnitude = magnitude;
    parsed.digits = digit_count;
    parsed.magnitude_fits_u32 = !overflow;
    parsed.negative = negative && nonzero;
    parsed.zero = !nonzero;
    *result_out = parsed;
    return CUPIDBUILD_DISK_INTEGER_OK;
}
