/**
 * @file cupidbuild_jpeg.h
 * @brief Check the JPEG stream and the ELF payload before publication.
 */

#ifndef CUPIDBUILD_JPEG_H
#define CUPIDBUILD_JPEG_H

#include "elf32.h"

/* The guest has no hosted memory or formatting functions. */
#ifdef CUPIDBUILD_JPEG_GUEST
#define CUPIDBUILD_JPEG_COMPARE(a, b, n) jpeg_bytes_compare(a, b, n)
#define CUPIDBUILD_JPEG_SIZE ctool_u32

static int jpeg_bytes_compare(const void *left, const void *right, ctool_u32 size) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (ctool_u32 index = 0u; index < size; index++) {
        if (a[index] != b[index]) {
            return (int)a[index] - (int)b[index];
        }
    }
    return 0;
}

static int cupidbuild_jpeg_symbol_name_matches(ctool_string_t actual, ctool_string_t identity,
                                               const char *suffix) {
    static const char prefix[] = "_binary_";
    ctool_u32 prefix_size = sizeof(prefix) - 1u;
    ctool_u32 identity_size = identity.size;
    ctool_u32 suffix_size = ctool_string(suffix).size;
    ctool_u32 index;
    if (identity_size > (ctool_u32)-1 - prefix_size - suffix_size ||
        prefix_size + identity_size + suffix_size != (ctool_u32)actual.size ||
        jpeg_bytes_compare(actual.data, prefix, prefix_size) != 0 ||
        jpeg_bytes_compare(actual.data + prefix_size + identity_size, suffix, suffix_size) != 0) {
        return 0;
    }
    for (index = 0u; index < identity_size; index++) {
        unsigned char character = (unsigned char)identity.data[index];
        char expected = ((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
                         (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
                         (character >= (unsigned char)'0' && character <= (unsigned char)'9'))
                            ? (char)character
                            : '_';
        if (actual.data[prefix_size + index] != expected) {
            return 0;
        }
    }
    return 1;
}

#else
#define CUPIDBUILD_JPEG_COMPARE(a, b, n) memcmp(a, b, n)
#define CUPIDBUILD_JPEG_SIZE size_t
#endif

#ifdef CUPIDBUILD_JPEG_GUEST
static int cupidbuild_jpeg_validate_object(ctool_job_t *job, ctool_bytes_t candidate,
                                           ctool_bytes_t jpeg, ctool_string_t source_identity) {
    const ctool_u8 *jpeg_bytes = jpeg.data;
    ctool_u32 jpeg_size = jpeg.size;
#else
int cupidbuild_validate_jpeg_object_bytes(const unsigned char *object_bytes, size_t object_size,
                                          const unsigned char *jpeg_bytes, size_t jpeg_size,
                                          const char *source_identity) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
#endif
    ctool_source_t source;
    ctool_elf32_object_t object;
    const ctool_elf32_section_t *data_section = (const ctool_elf32_section_t *)0;
    const ctool_elf32_symbol_t *start_symbol = (const ctool_elf32_symbol_t *)0;
    const ctool_elf32_symbol_t *end_symbol = (const ctool_elf32_symbol_t *)0;
    const ctool_elf32_symbol_t *size_symbol = (const ctool_elf32_symbol_t *)0;
    ctool_u32 index;
    int extra_allocated_data = 0;
    int valid = 0;
#ifdef CUPIDBUILD_JPEG_GUEST
    source.path.text = ctool_string("/candidate.o");
    source.contents = candidate;
#else
    if (object_bytes == (const unsigned char *)0 || jpeg_bytes == (const unsigned char *)0 ||
        source_identity == (const char *)0 || source_identity[0] == '\0' ||
        object_size > 4294967295u || jpeg_size > 4294967295u ||
        ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
        return 0;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    if (ctool_job_open(&config, &job) != CTOOL_OK) {
        return 0;
    }
    source.path.text = ctool_string("/candidate.o");
    source.contents = ctool_bytes(object_bytes, (ctool_u32)object_size);
#endif
    if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
        object.file_type != CTOOL_ELF32_ET_REL) {
        goto done;
    }
    for (index = 0u; index < object.section_count; index++) {
        const ctool_elf32_section_t *section = &object.sections[index];
        if (section->name.size == 5u && CUPIDBUILD_JPEG_COMPARE(section->name.data, ".data", 5u) == 0) {
            if (data_section != (const ctool_elf32_section_t *)0) {
                goto done;
            }
            data_section = section;
        } else if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u && section->size != 0u) {
            extra_allocated_data = 1;
        }
    }
    for (index = 0u; index < object.symbol_count; index++) {
        const ctool_elf32_symbol_t *symbol = &object.symbols[index];
        if (cupidbuild_jpeg_symbol_name_matches(symbol->name, source_identity, "_start")) {
            if (start_symbol != (const ctool_elf32_symbol_t *)0) {
                goto done;
            }
            start_symbol = symbol;
        } else if (cupidbuild_jpeg_symbol_name_matches(symbol->name, source_identity, "_end")) {
            if (end_symbol != (const ctool_elf32_symbol_t *)0) {
                goto done;
            }
            end_symbol = symbol;
        } else if (cupidbuild_jpeg_symbol_name_matches(symbol->name, source_identity, "_size")) {
            if (size_symbol != (const ctool_elf32_symbol_t *)0) {
                goto done;
            }
            size_symbol = symbol;
        }
    }
    if (object.symbol_count != 4u || data_section == (const ctool_elf32_section_t *)0 ||
        data_section->type != CTOOL_ELF32_SHT_PROGBITS ||
        data_section->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
        data_section->alignment != 1u || data_section->entry_size != 0u ||
        data_section->size != (ctool_u32)jpeg_size ||
        data_section->contents.size != (ctool_u32)jpeg_size ||
        (jpeg_size != 0u && CUPIDBUILD_JPEG_COMPARE(data_section->contents.data, jpeg_bytes, jpeg_size) != 0) ||
        extra_allocated_data != 0 || object.relocation_count != 0u ||
        start_symbol == (const ctool_elf32_symbol_t *)0 ||
        end_symbol == (const ctool_elf32_symbol_t *)0 ||
        size_symbol == (const ctool_elf32_symbol_t *)0 ||
        start_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
        start_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
        start_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
        start_symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        start_symbol->section_file_index != data_section->file_index || start_symbol->value != 0u ||
        start_symbol->size != 0u || end_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
        end_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
        end_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
        end_symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        end_symbol->section_file_index != data_section->file_index ||
        end_symbol->value != (ctool_u32)jpeg_size || end_symbol->size != 0u ||
        size_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
        size_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
        size_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
        size_symbol->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE ||
        size_symbol->section_file_index != CTOOL_ELF32_NO_SECTION ||
        size_symbol->value != (ctool_u32)jpeg_size || size_symbol->size != 0u) {
        goto done;
    }
    valid = 1;

done:
#ifndef CUPIDBUILD_JPEG_GUEST
    ctool_job_close(job);
#endif
    return valid;
}

static int cupidbuild_jpeg_frame_marker(unsigned char marker) {
    switch (marker) {
        case 0xc0u:
        case 0xc1u:
        case 0xc2u:
        case 0xc3u:
        case 0xc5u:
        case 0xc6u:
        case 0xc7u:
        case 0xc9u:
        case 0xcau:
        case 0xcbu:
        case 0xcdu:
        case 0xceu:
        case 0xcfu:
            return 1;
        default:
            return 0;
    }
}

#ifdef CUPIDBUILD_JPEG_GUEST
static void cupidbuild_jpeg_append(char **reason, ctool_u32 *capacity, const char *text) {
    while (*capacity > 1u && *text != '\0') {
        **reason = *text++;
        (*reason)++;
        (*capacity)--;
    }
    **reason = '\0';
}

static int cupidbuild_jpeg_failure(char *reason, ctool_u32 reason_capacity, const char *message) {
    cupidbuild_jpeg_append(&reason, &reason_capacity, message);
    return 0;
}

static int cupidbuild_jpeg_marker_failure(char *reason, ctool_u32 reason_capacity, const char *prefix,
                                          unsigned char marker, const char *suffix) {
    static const char digits[] = "0123456789abcdef";
    const char hex[] = {'0', 'x', digits[marker >> 4u], digits[marker & 0x0fu], '\0'};
    cupidbuild_jpeg_append(&reason, &reason_capacity, prefix);
    cupidbuild_jpeg_append(&reason, &reason_capacity, hex);
    cupidbuild_jpeg_append(&reason, &reason_capacity, suffix);
    return 0;
}

#else
static int cupidbuild_jpeg_failure(char *reason, size_t reason_capacity, const char *message) {
    if (reason != (char *)0 && reason_capacity != 0u) {
        (void)snprintf(reason, reason_capacity, "%s", message);
    }
    return 0;
}

static int cupidbuild_jpeg_marker_failure(char *reason, size_t reason_capacity, const char *prefix,
                                          unsigned char marker, const char *suffix) {
    if (reason != (char *)0 && reason_capacity != 0u) {
        (void)snprintf(reason, reason_capacity, "%s0x%02x%s", prefix, (unsigned int)marker, suffix);
    }
    return 0;
}

#endif

#ifdef CUPIDBUILD_JPEG_GUEST
static int cupidbuild_jpeg_validate_stream(const unsigned char *bytes, ctool_u32 size,
                                           char *reason, ctool_u32 reason_capacity) {
#else
int cupidbuild_validate_jpeg_bytes(const unsigned char *bytes, size_t size, char *reason,
                                   size_t reason_capacity) {
#endif
    CUPIDBUILD_JPEG_SIZE offset = 2u;
    unsigned char frame_marker = 0u;
    int saw_scan = 0;
    int saw_eoi = 0;
    if (reason == (char *)0 || reason_capacity == 0u) {
        return 0;
    }
    reason[0] = '\0';
    if (bytes == (const unsigned char *)0 || size < 2u || bytes[0] != 0xffu || bytes[1] != 0xd8u) {
        return cupidbuild_jpeg_failure(reason, reason_capacity, "JPEG input has no SOI marker");
    }
    while (offset < size) {
        CUPIDBUILD_JPEG_SIZE segment_size;
        unsigned char marker;
        if (bytes[offset] != 0xffu) {
            return cupidbuild_jpeg_failure(reason, reason_capacity,
                                           "JPEG marker stream is malformed outside a scan");
        }
        while (offset < size && bytes[offset] == 0xffu) {
            offset++;
        }
        if (offset >= size) {
            break;
        }
        marker = bytes[offset++];
        if (marker == 0x00u) {
            return cupidbuild_jpeg_failure(
                reason, reason_capacity, "JPEG marker stream contains stuffed data before a scan");
        }
        if (marker == 0xd9u) {
            saw_eoi = 1;
            if (offset != size) {
                return cupidbuild_jpeg_failure(
                    reason, reason_capacity, "JPEG input has trailing bytes after the EOI marker");
            }
            break;
        }
        if (marker == 0x01u || marker == 0xd8u || (marker >= 0xd0u && marker <= 0xd7u)) {
            if (marker != 0x01u) {
                return cupidbuild_jpeg_marker_failure(
                    reason, reason_capacity, "unexpected standalone JPEG marker ", marker, "");
            }
            continue;
        }
        if (size - offset < 2u) {
            return cupidbuild_jpeg_failure(reason, reason_capacity,
                                           "JPEG marker length is truncated");
        }
        segment_size = ((CUPIDBUILD_JPEG_SIZE)bytes[offset] << 8u) | (CUPIDBUILD_JPEG_SIZE)bytes[offset + 1u];
        if (segment_size < 2u || segment_size > size - offset) {
            return cupidbuild_jpeg_failure(reason, reason_capacity,
                                           "JPEG marker length is invalid");
        }
        if (cupidbuild_jpeg_frame_marker(marker)) {
            CUPIDBUILD_JPEG_SIZE component_count;
            if (frame_marker != 0u) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG input contains more than one frame header");
            }
            if (segment_size < 8u) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG frame header is truncated");
            }
            component_count = (CUPIDBUILD_JPEG_SIZE)bytes[offset + 7u];
            if (component_count == 0u || segment_size != 8u + 3u * component_count) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG frame header has an invalid component table");
            }
            if (bytes[offset + 2u] == 0u) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG frame header has an invalid sample precision");
            }
            if ((bytes[offset + 3u] == 0u && bytes[offset + 4u] == 0u) ||
                (bytes[offset + 5u] == 0u && bytes[offset + 6u] == 0u)) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG frame header has an invalid image size");
            }
            frame_marker = marker;
        }
        if (marker == 0xdau) {
            CUPIDBUILD_JPEG_SIZE scan_components;
            if (frame_marker == 0u) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG scan appears before its frame header");
            }
            if (segment_size < 6u) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG scan header is truncated");
            }
            scan_components = (CUPIDBUILD_JPEG_SIZE)bytes[offset + 2u];
            if (scan_components == 0u || segment_size != 6u + 2u * scan_components) {
                return cupidbuild_jpeg_failure(reason, reason_capacity,
                                               "JPEG scan header has an invalid component table");
            }
            saw_scan = 1;
            offset += segment_size;
            while (offset < size) {
                CUPIDBUILD_JPEG_SIZE scan_marker_offset;
                unsigned char scan_marker;
                if (bytes[offset] != 0xffu) {
                    offset++;
                    continue;
                }
                scan_marker_offset = offset;
                while (offset < size && bytes[offset] == 0xffu) {
                    offset++;
                }
                if (offset >= size) {
                    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                                   "JPEG entropy data ends with a partial marker");
                }
                scan_marker = bytes[offset++];
                if (scan_marker == 0x00u || (scan_marker >= 0xd0u && scan_marker <= 0xd7u)) {
                    continue;
                }
                offset = scan_marker_offset;
                break;
            }
            continue;
        }
        offset += segment_size;
    }
    if (frame_marker == 0xc2u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "unsupported progressive JPEG frame; check in a baseline SOF0/SOF1 "
            "asset");
    }
    if (frame_marker != 0xc0u && frame_marker != 0xc1u) {
        if (frame_marker == 0u) {
            return cupidbuild_jpeg_failure(reason, reason_capacity,
                                           "JPEG input has no supported SOF0/SOF1 frame");
        }
        return cupidbuild_jpeg_marker_failure(reason, reason_capacity,
                                              "unsupported JPEG frame marker ", frame_marker,
                                              "; check in a baseline SOF0/SOF1 asset");
    }
    if (!saw_scan) {
        return cupidbuild_jpeg_failure(reason, reason_capacity, "JPEG input has no scan");
    }
    if (!saw_eoi) {
        return cupidbuild_jpeg_failure(reason, reason_capacity, "JPEG input has no EOI marker");
    }
    return 1;
}

#undef CUPIDBUILD_JPEG_COMPARE
#undef CUPIDBUILD_JPEG_SIZE

#endif
