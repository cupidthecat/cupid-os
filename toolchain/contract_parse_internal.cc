#include "contract_parse_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const cupid_contract_seed_names[SEED_ARTIFACT_COUNT] = {
    "cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild"};

const char *const cupid_contract_seed_files[SEED_ARTIFACT_COUNT] = {
    "cupidasm.elf", "cupidc.elf", "cupiddis.elf", "cupidld.elf",
    "cupidobj.elf", "cupidbuild.elf"};

int cupid_contract_set_error(error_context_t *context, const char *message) {
  context->has_error = 1;
  if (context->capacity != 0u) {
    (void)snprintf(context->bytes, context->capacity, "%s", message);
  }
  return 0;
}

void cupid_contract_text_release(text_t *text) {
  free(text->bytes);
  text->bytes = (unsigned char *)0;
  text->size = 0u;
}

int cupid_contract_text_equals_literal(const text_t *text, const char *literal) {
  size_t length = strlen(literal);
  return text->size == length && memcmp(text->bytes, literal, length) == 0;
}

int cupid_contract_slice_equals_text(const byte_slice_t *slice, const text_t *text) {
  return slice->size == text->size &&
         memcmp(slice->bytes, text->bytes, text->size) == 0;
}

int cupid_contract_text_compare(const text_t *left, const text_t *right) {
  size_t shared = left->size < right->size ? left->size : right->size;
  int comparison = memcmp(left->bytes, right->bytes, shared);
  if (comparison != 0) {
    return comparison;
  }
  if (left->size < right->size) {
    return -1;
  }
  if (left->size > right->size) {
    return 1;
  }
  return 0;
}

void cupid_contract_json_skip_space(json_reader_t *reader) {
  while (reader->position < reader->size) {
    unsigned char value = reader->bytes[reader->position];
    if (value != (unsigned char)' ' && value != (unsigned char)'\t' &&
        value != (unsigned char)'\r' && value != (unsigned char)'\n') {
      break;
    }
    reader->position++;
  }
}

int cupid_contract_json_take(error_context_t *context, json_reader_t *reader, unsigned char expected) {
  cupid_contract_json_skip_space(reader);
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != expected) {
    return cupid_contract_set_error(context, "JSON syntax differs from the required form");
  }
  reader->position++;
  return 1;
}

int cupid_contract_hex_value(unsigned char value) {
  if (value >= (unsigned char)'0' && value <= (unsigned char)'9') {
    return (int)(value - (unsigned char)'0');
  }
  if (value >= (unsigned char)'a' && value <= (unsigned char)'f') {
    return (int)(value - (unsigned char)'a') + 10;
  }
  if (value >= (unsigned char)'A' && value <= (unsigned char)'F') {
    return (int)(value - (unsigned char)'A') + 10;
  }
  return -1;
}

int cupid_contract_json_hex_quad(error_context_t *context, json_reader_t *reader, uint32_t *value) {
  size_t index;
  uint32_t parsed = 0u;
  if (reader->size - reader->position < 4u) {
    return cupid_contract_set_error(context, "JSON Unicode escape is truncated");
  }
  for (index = 0u; index < 4u; index++) {
    int digit = cupid_contract_hex_value(reader->bytes[reader->position + index]);
    if (digit < 0) {
      return cupid_contract_set_error(context, "JSON Unicode escape is invalid");
    }
    parsed = (parsed << 4u) | (uint32_t)digit;
  }
  reader->position += 4u;
  *value = parsed;
  return 1;
}

int cupid_contract_utf8_sequence_size(const unsigned char *bytes, size_t remaining,
                              size_t *sequence_size) {
  unsigned char first;
  unsigned char second;
  size_t needed;
  size_t index;
  if (remaining == 0u) {
    return 0;
  }
  first = bytes[0];
  if (first < 0x80u) {
    *sequence_size = 1u;
    return 1;
  }
  if (first >= 0xc2u && first <= 0xdfu) {
    needed = 2u;
  } else if (first >= 0xe0u && first <= 0xefu) {
    needed = 3u;
  } else if (first >= 0xf0u && first <= 0xf4u) {
    needed = 4u;
  } else {
    return 0;
  }
  if (remaining < needed) {
    return 0;
  }
  second = bytes[1];
  if ((second & 0xc0u) != 0x80u) {
    return 0;
  }
  if ((first == 0xe0u && second < 0xa0u) ||
      (first == 0xedu && second >= 0xa0u) ||
      (first == 0xf0u && second < 0x90u) ||
      (first == 0xf4u && second >= 0x90u)) {
    return 0;
  }
  for (index = 2u; index < needed; index++) {
    if ((bytes[index] & 0xc0u) != 0x80u) {
      return 0;
    }
  }
  *sequence_size = needed;
  return 1;
}

int cupid_contract_utf8_valid(const unsigned char *bytes, size_t size) {
  size_t position = 0u;
  while (position < size) {
    size_t sequence_size;
    if (!cupid_contract_utf8_sequence_size(bytes + position, size - position,
                            &sequence_size)) {
      return 0;
    }
    position += sequence_size;
  }
  return 1;
}

int cupid_contract_json_append_codepoint(unsigned char *output, size_t *written,
                                 uint32_t codepoint) {
  if (codepoint <= 0x7fu) {
    output[*written] = (unsigned char)codepoint;
    *written += 1u;
  } else if (codepoint <= 0x7ffu) {
    output[*written] = (unsigned char)(0xc0u | (codepoint >> 6u));
    output[*written + 1u] =
        (unsigned char)(0x80u | (codepoint & 0x3fu));
    *written += 2u;
  } else if (codepoint <= 0xffffu) {
    output[*written] = (unsigned char)(0xe0u | (codepoint >> 12u));
    output[*written + 1u] =
        (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
    output[*written + 2u] =
        (unsigned char)(0x80u | (codepoint & 0x3fu));
    *written += 3u;
  } else {
    output[*written] = (unsigned char)(0xf0u | (codepoint >> 18u));
    output[*written + 1u] =
        (unsigned char)(0x80u | ((codepoint >> 12u) & 0x3fu));
    output[*written + 2u] =
        (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
    output[*written + 3u] =
        (unsigned char)(0x80u | (codepoint & 0x3fu));
    *written += 4u;
  }
  return 1;
}

int cupid_contract_json_parse_string(error_context_t *context, json_reader_t *reader, text_t *result) {
  unsigned char *output = (unsigned char *)0;
  size_t written = 0u;
  cupid_contract_json_skip_space(reader);
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != (unsigned char)'"') {
    return cupid_contract_set_error(context, "JSON string is required");
  }
  reader->position++;
  if (result != (text_t *)0) {
    size_t capacity = reader->size - reader->position + 1u;
    output = (unsigned char *)malloc(capacity);
    if (output == (unsigned char *)0) {
      return cupid_contract_set_error(context, "cannot allocate a JSON string");
    }
  }
  while (reader->position < reader->size) {
    unsigned char value = reader->bytes[reader->position++];
    if (value == (unsigned char)'"') {
      if (result != (text_t *)0) {
        output[written] = 0u;
        result->bytes = output;
        result->size = written;
      }
      return 1;
    }
    if (value < 0x20u) {
      free(output);
      return cupid_contract_set_error(context, "JSON string contains a control byte");
    }
    if (value == (unsigned char)'\\') {
      unsigned char escaped;
      uint32_t codepoint;
      if (reader->position >= reader->size) {
        free(output);
        return cupid_contract_set_error(context, "JSON string escape is truncated");
      }
      escaped = reader->bytes[reader->position++];
      if (escaped == (unsigned char)'"' ||
          escaped == (unsigned char)'\\' ||
          escaped == (unsigned char)'/') {
        if (output != (unsigned char *)0) {
          output[written] = escaped;
        }
        written++;
        continue;
      }
      if (escaped == (unsigned char)'b' ||
          escaped == (unsigned char)'f' ||
          escaped == (unsigned char)'n' ||
          escaped == (unsigned char)'r' ||
          escaped == (unsigned char)'t') {
        unsigned char decoded = (unsigned char)'\b';
        if (escaped == (unsigned char)'f') {
          decoded = (unsigned char)'\f';
        } else if (escaped == (unsigned char)'n') {
          decoded = (unsigned char)'\n';
        } else if (escaped == (unsigned char)'r') {
          decoded = (unsigned char)'\r';
        } else if (escaped == (unsigned char)'t') {
          decoded = (unsigned char)'\t';
        }
        if (output != (unsigned char *)0) {
          output[written] = decoded;
        }
        written++;
        continue;
      }
      if (escaped != (unsigned char)'u' ||
          !cupid_contract_json_hex_quad(context, reader, &codepoint)) {
        free(output);
        if (!context->has_error) {
          return cupid_contract_set_error(context, "JSON string escape is invalid");
        }
        return 0;
      }
      if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
        uint32_t low;
        if (reader->size - reader->position < 2u ||
            reader->bytes[reader->position] != (unsigned char)'\\' ||
            reader->bytes[reader->position + 1u] != (unsigned char)'u') {
          free(output);
          return cupid_contract_set_error(context, "JSON Unicode surrogate is incomplete");
        }
        reader->position += 2u;
        if (!cupid_contract_json_hex_quad(context, reader, &low) || low < 0xdc00u || low > 0xdfffu) {
          free(output);
          return cupid_contract_set_error(context, "JSON Unicode surrogate is invalid");
        }
        codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) +
                    (low - 0xdc00u);
      } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
        free(output);
        return cupid_contract_set_error(context, "JSON Unicode surrogate is invalid");
      }
      if (output != (unsigned char *)0) {
        (void)cupid_contract_json_append_codepoint(output, &written, codepoint);
      } else if (codepoint <= 0x7fu) {
        written += 1u;
      } else if (codepoint <= 0x7ffu) {
        written += 2u;
      } else if (codepoint <= 0xffffu) {
        written += 3u;
      } else {
        written += 4u;
      }
      continue;
    }
    if (value < 0x80u) {
      if (output != (unsigned char *)0) {
        output[written] = value;
      }
      written++;
    } else {
      size_t sequence_size;
      size_t start = reader->position - 1u;
      size_t index;
      if (!cupid_contract_utf8_sequence_size(reader->bytes + start, reader->size - start,
                              &sequence_size)) {
        free(output);
        return cupid_contract_set_error(context, "JSON string is not valid UTF-8");
      }
      if (output != (unsigned char *)0) {
        for (index = 0u; index < sequence_size; index++) {
          output[written + index] = reader->bytes[start + index];
        }
      }
      written += sequence_size;
      reader->position = start + sequence_size;
    }
  }
  free(output);
  return cupid_contract_set_error(context, "JSON string is truncated");
}

int cupid_contract_json_match_literal(json_reader_t *reader, const char *literal) {
  size_t length = strlen(literal);
  if (reader->size - reader->position < length ||
      memcmp(reader->bytes + reader->position, literal, length) != 0) {
    return 0;
  }
  reader->position += length;
  return 1;
}

int cupid_contract_json_skip_number(error_context_t *context, json_reader_t *reader) {
  size_t start = reader->position;
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'-') {
    reader->position++;
  }
  if (reader->position >= reader->size) {
    return cupid_contract_set_error(context, "JSON number is truncated");
  }
  if (reader->bytes[reader->position] == (unsigned char)'0') {
    reader->position++;
    if (reader->position < reader->size &&
        reader->bytes[reader->position] >= (unsigned char)'0' &&
        reader->bytes[reader->position] <= (unsigned char)'9') {
      return cupid_contract_set_error(context, "JSON number has a leading zero");
    }
  } else if (reader->bytes[reader->position] >= (unsigned char)'1' &&
             reader->bytes[reader->position] <= (unsigned char)'9') {
    while (reader->position < reader->size &&
           reader->bytes[reader->position] >= (unsigned char)'0' &&
           reader->bytes[reader->position] <= (unsigned char)'9') {
      reader->position++;
    }
  } else {
    return cupid_contract_set_error(context, "JSON number is invalid");
  }
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'.') {
    reader->position++;
    if (reader->position >= reader->size ||
        reader->bytes[reader->position] < (unsigned char)'0' ||
        reader->bytes[reader->position] > (unsigned char)'9') {
      return cupid_contract_set_error(context, "JSON number fraction is invalid");
    }
    while (reader->position < reader->size &&
           reader->bytes[reader->position] >= (unsigned char)'0' &&
           reader->bytes[reader->position] <= (unsigned char)'9') {
      reader->position++;
    }
  }
  if (reader->position < reader->size &&
      (reader->bytes[reader->position] == (unsigned char)'e' ||
       reader->bytes[reader->position] == (unsigned char)'E')) {
    reader->position++;
    if (reader->position < reader->size &&
        (reader->bytes[reader->position] == (unsigned char)'+' ||
         reader->bytes[reader->position] == (unsigned char)'-')) {
      reader->position++;
    }
    if (reader->position >= reader->size ||
        reader->bytes[reader->position] < (unsigned char)'0' ||
        reader->bytes[reader->position] > (unsigned char)'9') {
      return cupid_contract_set_error(context, "JSON number exponent is invalid");
    }
    while (reader->position < reader->size &&
           reader->bytes[reader->position] >= (unsigned char)'0' &&
           reader->bytes[reader->position] <= (unsigned char)'9') {
      reader->position++;
    }
  }
  if (reader->position == start) {
    return cupid_contract_set_error(context, "JSON number is invalid");
  }
  return 1;
}

int cupid_contract_json_skip_array(error_context_t *context, json_reader_t *reader, unsigned int depth) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return 1;
  }
  for (;;) {
    if (!cupid_contract_json_skip_value(context, reader, depth + 1u)) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      return 1;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
}

int cupid_contract_json_skip_object(error_context_t *context, json_reader_t *reader, unsigned int depth) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return 1;
  }
  for (;;) {
    if (!cupid_contract_json_parse_string(context, reader, (text_t *)0) ||
        !cupid_contract_json_take(context, reader, (unsigned char)':') ||
        !cupid_contract_json_skip_value(context, reader, depth + 1u)) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      return 1;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
}

int cupid_contract_json_skip_value(error_context_t *context, json_reader_t *reader, unsigned int depth) {
  unsigned char value;
  if (depth > JSON_MAX_DEPTH) {
    return cupid_contract_set_error(context, "JSON nesting is too deep");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position >= reader->size) {
    return cupid_contract_set_error(context, "JSON value is truncated");
  }
  value = reader->bytes[reader->position];
  if (value == (unsigned char)'"') {
    return cupid_contract_json_parse_string(context, reader, (text_t *)0);
  }
  if (value == (unsigned char)'{') {
    return cupid_contract_json_skip_object(context, reader, depth);
  }
  if (value == (unsigned char)'[') {
    return cupid_contract_json_skip_array(context, reader, depth);
  }
  if (value == (unsigned char)'-' ||
      (value >= (unsigned char)'0' && value <= (unsigned char)'9')) {
    return cupid_contract_json_skip_number(context, reader);
  }
  if (cupid_contract_json_match_literal(reader, "true") ||
      cupid_contract_json_match_literal(reader, "false") ||
      cupid_contract_json_match_literal(reader, "null")) {
    return 1;
  }
  return cupid_contract_set_error(context, "JSON value is invalid");
}

int cupid_contract_json_parse_positive_u64(error_context_t *context, json_reader_t *reader, uint64_t *result) {
  uint64_t value = 0u;
  uint64_t maximum = ~(uint64_t)0u;
  size_t digits = 0u;
  cupid_contract_json_skip_space(reader);
  while (reader->position < reader->size &&
         reader->bytes[reader->position] >= (unsigned char)'0' &&
         reader->bytes[reader->position] <= (unsigned char)'9') {
    uint32_t digit =
        (uint32_t)(reader->bytes[reader->position] - (unsigned char)'0');
    if (value > (maximum - (uint64_t)digit) / 10u) {
      return cupid_contract_set_error(context, "JSON integer exceeds the unsigned 64-bit range");
    }
    value = value * 10u + (uint64_t)digit;
    reader->position++;
    digits++;
  }
  if (digits == 0u || value == 0u) {
    return cupid_contract_set_error(context, "a positive JSON integer is required");
  }
  if ((digits > 1u &&
       reader->bytes[reader->position - digits] == (unsigned char)'0') ||
      (reader->position < reader->size &&
       (reader->bytes[reader->position] == (unsigned char)'.' ||
        reader->bytes[reader->position] == (unsigned char)'e' ||
        reader->bytes[reader->position] == (unsigned char)'E'))) {
    return cupid_contract_set_error(context, "an exact positive JSON integer is required");
  }
  *result = value;
  return 1;
}

int cupid_contract_json_finish(error_context_t *context, json_reader_t *reader) {
  cupid_contract_json_skip_space(reader);
  if (reader->position != reader->size) {
    return cupid_contract_set_error(context, "JSON has trailing input");
  }
  return 1;
}

int cupid_contract_logical_path_valid(const unsigned char *bytes, size_t size) {
  size_t position;
  size_t segment_start = 0u;
  if (size == 0u || bytes[0] == (unsigned char)'/' ||
      bytes[size - 1u] == (unsigned char)'/' || !cupid_contract_utf8_valid(bytes, size)) {
    return 0;
  }
  for (position = 0u; position <= size; position++) {
    if (position < size && bytes[position] == (unsigned char)'\\') {
      return 0;
    }
    if (position == size || bytes[position] == (unsigned char)'/') {
      size_t segment_size = position - segment_start;
      if (segment_size == 0u ||
          (segment_size == 1u &&
           bytes[segment_start] == (unsigned char)'.') ||
          (segment_size == 2u &&
           bytes[segment_start] == (unsigned char)'.' &&
           bytes[segment_start + 1u] == (unsigned char)'.')) {
        return 0;
      }
      segment_start = position + 1u;
    } else if (bytes[position] == 0u) {
      return 0;
    }
  }
  return 1;
}

int cupid_contract_seed_index_for_name(const text_t *name) {
  size_t index;
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    if (cupid_contract_text_equals_literal(name, cupid_contract_seed_names[index])) {
      return (int)index;
    }
  }
  return -1;
}

int cupid_contract_binary_read_u32(error_context_t *context, binary_reader_t *reader, uint32_t *value) {
  const unsigned char *bytes;
  if (reader->size - reader->position < 4u) {
    return cupid_contract_set_error(context, "request is truncated while reading a 32-bit value");
  }
  bytes = reader->bytes + reader->position;
  *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
  reader->position += 4u;
  return 1;
}

int cupid_contract_binary_read_u64(error_context_t *context, binary_reader_t *reader, uint64_t *value) {
  const unsigned char *bytes;
  uint64_t low;
  uint64_t high;
  if (reader->size - reader->position < 8u) {
    return cupid_contract_set_error(context, "request is truncated while reading a 64-bit value");
  }
  bytes = reader->bytes + reader->position;
  low = (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8u) |
        ((uint64_t)bytes[2] << 16u) | ((uint64_t)bytes[3] << 24u);
  high = (uint64_t)bytes[4] | ((uint64_t)bytes[5] << 8u) |
         ((uint64_t)bytes[6] << 16u) | ((uint64_t)bytes[7] << 24u);
  *value = low | (high << 32u);
  reader->position += 8u;
  return 1;
}

int cupid_contract_binary_read_slice(error_context_t *context, binary_reader_t *reader, byte_slice_t *slice) {
  uint32_t length;
  if (!cupid_contract_binary_read_u32(context, reader, &length)) {
    return 0;
  }
  if (reader->size - reader->position < (size_t)length) {
    return cupid_contract_set_error(context, "request byte string is truncated");
  }
  slice->bytes = reader->bytes + reader->position;
  slice->size = (size_t)length;
  reader->position += (size_t)length;
  return 1;
}
