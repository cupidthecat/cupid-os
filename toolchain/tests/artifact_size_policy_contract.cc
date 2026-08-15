#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARTIFACT_COUNT 9u
#define SEED_ARTIFACT_COUNT 5u
#define FIXED_ARTIFACT_COUNT 4u
#define JSON_MAX_DEPTH 64u

typedef struct {
  unsigned char *bytes;
  size_t size;
} file_image_t;

typedef struct {
  const unsigned char *bytes;
  size_t size;
} byte_slice_t;

typedef struct {
  const unsigned char *bytes;
  size_t size;
  size_t position;
} binary_reader_t;

typedef struct {
  const unsigned char *bytes;
  size_t size;
  size_t position;
} json_reader_t;

typedef struct {
  unsigned char *bytes;
  size_t size;
} text_t;

typedef struct {
  text_t path;
  text_t producer;
  text_t reason;
  uint64_t exact_bytes;
} policy_entry_t;

typedef struct {
  policy_entry_t entries[ARTIFACT_COUNT];
  size_t count;
} policy_t;

typedef struct {
  uint64_t sizes[SEED_ARTIFACT_COUNT];
  int seen[SEED_ARTIFACT_COUNT];
} seed_manifest_t;

typedef struct {
  byte_slice_t path;
  uint32_t kind;
  uint64_t size;
} observation_t;

static const unsigned char request_magic[8] = {
    'C', 'U', 'P', 'S', 'I', 'Z', 'E', '1'};
static const char policy_schema[] = "cupid.artifact-size-policy.v1";
static const char seed_schema[] = "cupid.bootstrap-seed.v1";
static const char report_schema[] = "cupid.artifact-size-verification.v1";
static const char *const seed_names[SEED_ARTIFACT_COUNT] = {
    "cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj"};
static const char *const seed_files[SEED_ARTIFACT_COUNT] = {
    "cupidasm.elf", "cupidc.elf", "cupiddis.elf", "cupidld.elf",
    "cupidobj.elf"};
static const char *const seed_owners[SEED_ARTIFACT_COUNT] = {
    "CupidASM", "CupidC", "CupidDis", "CupidLD", "CupidObj"};
static const char *const fixed_paths[FIXED_ARTIFACT_COUNT] = {
    "boot/boot.bin", "kernel/kernel.bin", "kernel/kernel.elf",
    "kernel/kernel.elf.pass1"};
static const char *const fixed_owners[FIXED_ARTIFACT_COUNT] = {
    "CupidASM", "CupidObj", "CupidLD", "CupidLD"};
static char contract_error[512];

static int set_error(const char *message) {
  (void)snprintf(contract_error, sizeof(contract_error), "%s", message);
  return 0;
}

static void file_image_release(file_image_t *image) {
  free(image->bytes);
  image->bytes = (unsigned char *)0;
  image->size = 0u;
}

static void text_release(text_t *text) {
  free(text->bytes);
  text->bytes = (unsigned char *)0;
  text->size = 0u;
}

static int text_equals_literal(const text_t *text, const char *literal) {
  size_t length = strlen(literal);
  return text->size == length && memcmp(text->bytes, literal, length) == 0;
}

static int slice_equals_text(const byte_slice_t *slice, const text_t *text) {
  return slice->size == text->size &&
         memcmp(slice->bytes, text->bytes, text->size) == 0;
}

static int text_compare(const text_t *left, const text_t *right) {
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

static void json_skip_space(json_reader_t *reader) {
  while (reader->position < reader->size) {
    unsigned char value = reader->bytes[reader->position];
    if (value != (unsigned char)' ' && value != (unsigned char)'\t' &&
        value != (unsigned char)'\r' && value != (unsigned char)'\n') {
      break;
    }
    reader->position++;
  }
}

static int json_take(json_reader_t *reader, unsigned char expected) {
  json_skip_space(reader);
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != expected) {
    return set_error("JSON syntax differs from the required form");
  }
  reader->position++;
  return 1;
}

static int hex_value(unsigned char value) {
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

static int json_hex_quad(json_reader_t *reader, uint32_t *value) {
  size_t index;
  uint32_t parsed = 0u;
  if (reader->size - reader->position < 4u) {
    return set_error("JSON Unicode escape is truncated");
  }
  for (index = 0u; index < 4u; index++) {
    int digit = hex_value(reader->bytes[reader->position + index]);
    if (digit < 0) {
      return set_error("JSON Unicode escape is invalid");
    }
    parsed = (parsed << 4u) | (uint32_t)digit;
  }
  reader->position += 4u;
  *value = parsed;
  return 1;
}

static int utf8_sequence_size(const unsigned char *bytes, size_t remaining,
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

static int utf8_valid(const unsigned char *bytes, size_t size) {
  size_t position = 0u;
  while (position < size) {
    size_t sequence_size;
    if (!utf8_sequence_size(bytes + position, size - position,
                            &sequence_size)) {
      return 0;
    }
    position += sequence_size;
  }
  return 1;
}

static int json_append_codepoint(unsigned char *output, size_t *written,
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

static int json_parse_string(json_reader_t *reader, text_t *result) {
  unsigned char *output = (unsigned char *)0;
  size_t written = 0u;
  json_skip_space(reader);
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != (unsigned char)'"') {
    return set_error("JSON string is required");
  }
  reader->position++;
  if (result != (text_t *)0) {
    size_t capacity = reader->size - reader->position + 1u;
    output = (unsigned char *)malloc(capacity);
    if (output == (unsigned char *)0) {
      return set_error("cannot allocate a JSON string");
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
      return set_error("JSON string contains a control byte");
    }
    if (value == (unsigned char)'\\') {
      unsigned char escaped;
      uint32_t codepoint;
      if (reader->position >= reader->size) {
        free(output);
        return set_error("JSON string escape is truncated");
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
          !json_hex_quad(reader, &codepoint)) {
        free(output);
        if (contract_error[0] == '\0') {
          return set_error("JSON string escape is invalid");
        }
        return 0;
      }
      if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
        uint32_t low;
        if (reader->size - reader->position < 2u ||
            reader->bytes[reader->position] != (unsigned char)'\\' ||
            reader->bytes[reader->position + 1u] != (unsigned char)'u') {
          free(output);
          return set_error("JSON Unicode surrogate is incomplete");
        }
        reader->position += 2u;
        if (!json_hex_quad(reader, &low) || low < 0xdc00u || low > 0xdfffu) {
          free(output);
          return set_error("JSON Unicode surrogate is invalid");
        }
        codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) +
                    (low - 0xdc00u);
      } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
        free(output);
        return set_error("JSON Unicode surrogate is invalid");
      }
      if (output != (unsigned char *)0) {
        (void)json_append_codepoint(output, &written, codepoint);
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
      if (!utf8_sequence_size(reader->bytes + start, reader->size - start,
                              &sequence_size)) {
        free(output);
        return set_error("JSON string is not valid UTF-8");
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
  return set_error("JSON string is truncated");
}

static int json_match_literal(json_reader_t *reader, const char *literal) {
  size_t length = strlen(literal);
  if (reader->size - reader->position < length ||
      memcmp(reader->bytes + reader->position, literal, length) != 0) {
    return 0;
  }
  reader->position += length;
  return 1;
}

static int json_skip_number(json_reader_t *reader) {
  size_t start = reader->position;
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'-') {
    reader->position++;
  }
  if (reader->position >= reader->size) {
    return set_error("JSON number is truncated");
  }
  if (reader->bytes[reader->position] == (unsigned char)'0') {
    reader->position++;
    if (reader->position < reader->size &&
        reader->bytes[reader->position] >= (unsigned char)'0' &&
        reader->bytes[reader->position] <= (unsigned char)'9') {
      return set_error("JSON number has a leading zero");
    }
  } else if (reader->bytes[reader->position] >= (unsigned char)'1' &&
             reader->bytes[reader->position] <= (unsigned char)'9') {
    while (reader->position < reader->size &&
           reader->bytes[reader->position] >= (unsigned char)'0' &&
           reader->bytes[reader->position] <= (unsigned char)'9') {
      reader->position++;
    }
  } else {
    return set_error("JSON number is invalid");
  }
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'.') {
    reader->position++;
    if (reader->position >= reader->size ||
        reader->bytes[reader->position] < (unsigned char)'0' ||
        reader->bytes[reader->position] > (unsigned char)'9') {
      return set_error("JSON number fraction is invalid");
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
      return set_error("JSON number exponent is invalid");
    }
    while (reader->position < reader->size &&
           reader->bytes[reader->position] >= (unsigned char)'0' &&
           reader->bytes[reader->position] <= (unsigned char)'9') {
      reader->position++;
    }
  }
  if (reader->position == start) {
    return set_error("JSON number is invalid");
  }
  return 1;
}

static int json_skip_value(json_reader_t *reader, unsigned int depth);

static int json_skip_array(json_reader_t *reader, unsigned int depth) {
  if (!json_take(reader, (unsigned char)'[')) {
    return 0;
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return 1;
  }
  for (;;) {
    if (!json_skip_value(reader, depth + 1u)) {
      return 0;
    }
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      return 1;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
}

static int json_skip_object(json_reader_t *reader, unsigned int depth) {
  if (!json_take(reader, (unsigned char)'{')) {
    return 0;
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return 1;
  }
  for (;;) {
    if (!json_parse_string(reader, (text_t *)0) ||
        !json_take(reader, (unsigned char)':') ||
        !json_skip_value(reader, depth + 1u)) {
      return 0;
    }
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      return 1;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
}

static int json_skip_value(json_reader_t *reader, unsigned int depth) {
  unsigned char value;
  if (depth > JSON_MAX_DEPTH) {
    return set_error("JSON nesting is too deep");
  }
  json_skip_space(reader);
  if (reader->position >= reader->size) {
    return set_error("JSON value is truncated");
  }
  value = reader->bytes[reader->position];
  if (value == (unsigned char)'"') {
    return json_parse_string(reader, (text_t *)0);
  }
  if (value == (unsigned char)'{') {
    return json_skip_object(reader, depth);
  }
  if (value == (unsigned char)'[') {
    return json_skip_array(reader, depth);
  }
  if (value == (unsigned char)'-' ||
      (value >= (unsigned char)'0' && value <= (unsigned char)'9')) {
    return json_skip_number(reader);
  }
  if (json_match_literal(reader, "true") ||
      json_match_literal(reader, "false") ||
      json_match_literal(reader, "null")) {
    return 1;
  }
  return set_error("JSON value is invalid");
}

static int json_parse_positive_u64(json_reader_t *reader, uint64_t *result) {
  uint64_t value = 0u;
  uint64_t maximum = ~(uint64_t)0u;
  size_t digits = 0u;
  json_skip_space(reader);
  while (reader->position < reader->size &&
         reader->bytes[reader->position] >= (unsigned char)'0' &&
         reader->bytes[reader->position] <= (unsigned char)'9') {
    uint32_t digit =
        (uint32_t)(reader->bytes[reader->position] - (unsigned char)'0');
    if (value > (maximum - (uint64_t)digit) / 10u) {
      return set_error("JSON integer exceeds the unsigned 64-bit range");
    }
    value = value * 10u + (uint64_t)digit;
    reader->position++;
    digits++;
  }
  if (digits == 0u || value == 0u) {
    return set_error("a positive JSON integer is required");
  }
  if ((digits > 1u &&
       reader->bytes[reader->position - digits] == (unsigned char)'0') ||
      (reader->position < reader->size &&
       (reader->bytes[reader->position] == (unsigned char)'.' ||
        reader->bytes[reader->position] == (unsigned char)'e' ||
        reader->bytes[reader->position] == (unsigned char)'E'))) {
    return set_error("an exact positive JSON integer is required");
  }
  *result = value;
  return 1;
}

static int json_finish(json_reader_t *reader) {
  json_skip_space(reader);
  if (reader->position != reader->size) {
    return set_error("JSON has trailing input");
  }
  return 1;
}

static int ascii_space(unsigned char value) {
  return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
         value == (unsigned char)'\n' || value == (unsigned char)'\r' ||
         value == (unsigned char)'\f' || value == (unsigned char)'\v';
}

static int logical_path_valid(const unsigned char *bytes, size_t size) {
  size_t position;
  size_t segment_start = 0u;
  if (size == 0u || bytes[0] == (unsigned char)'/' ||
      bytes[size - 1u] == (unsigned char)'/' || !utf8_valid(bytes, size)) {
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

static int seed_index_for_name(const text_t *name) {
  size_t index;
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    if (text_equals_literal(name, seed_names[index])) {
      return (int)index;
    }
  }
  return -1;
}

static int parse_manifest_artifact(json_reader_t *reader, text_t *name,
                                   text_t *file, uint64_t *size) {
  unsigned int fields = 0u;
  if (!json_take(reader, (unsigned char)'{')) {
    return 0;
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return set_error("seed manifest artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = json_parse_string(reader, &key) &&
             json_take(reader, (unsigned char)':');
    if (!ok) {
      text_release(&key);
      return 0;
    }
    if (text_equals_literal(&key, "name")) {
      if ((fields & 1u) != 0u) {
        text_release(&key);
        return set_error("seed manifest artifact name is duplicated");
      }
      fields |= 1u;
      ok = json_parse_string(reader, name);
    } else if (text_equals_literal(&key, "file")) {
      if ((fields & 2u) != 0u) {
        text_release(&key);
        return set_error("seed manifest artifact file is duplicated");
      }
      fields |= 2u;
      ok = json_parse_string(reader, file);
    } else if (text_equals_literal(&key, "size")) {
      if ((fields & 4u) != 0u) {
        text_release(&key);
        return set_error("seed manifest artifact size is duplicated");
      }
      fields |= 4u;
      ok = json_parse_positive_u64(reader, size);
    } else {
      ok = json_skip_value(reader, 1u);
    }
    text_release(&key);
    if (!ok) {
      return 0;
    }
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return set_error("seed manifest artifact fields are missing");
  }
  return 1;
}

static int parse_manifest_artifacts(json_reader_t *reader,
                                    seed_manifest_t *manifest) {
  size_t count = 0u;
  if (!json_take(reader, (unsigned char)'[')) {
    return 0;
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return set_error("seed manifest artifacts are missing");
  }
  for (;;) {
    text_t name = {(unsigned char *)0, 0u};
    text_t file = {(unsigned char *)0, 0u};
    uint64_t size = 0u;
    int seed_index;
    int ok;
    if (count >= SEED_ARTIFACT_COUNT) {
      return set_error("seed manifest has too many artifacts");
    }
    ok = parse_manifest_artifact(reader, &name, &file, &size);
    if (!ok) {
      text_release(&name);
      text_release(&file);
      return 0;
    }
    seed_index = seed_index_for_name(&name);
    if (seed_index < 0) {
      text_release(&name);
      text_release(&file);
      return set_error("seed manifest has an unknown tool artifact");
    }
    if (manifest->seen[(size_t)seed_index] != 0) {
      text_release(&name);
      text_release(&file);
      return set_error("seed manifest tool artifact is duplicated");
    }
    if (!text_equals_literal(&file, seed_files[(size_t)seed_index])) {
      text_release(&name);
      text_release(&file);
      return set_error("seed manifest artifact filename differs");
    }
    manifest->seen[(size_t)seed_index] = 1;
    manifest->sizes[(size_t)seed_index] = size;
    count++;
    text_release(&name);
    text_release(&file);
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != SEED_ARTIFACT_COUNT) {
    return set_error("seed manifest does not contain five artifacts");
  }
  return 1;
}

static int parse_seed_manifest(byte_slice_t source,
                               seed_manifest_t *manifest) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(manifest, 0, sizeof(*manifest));
  if (!json_take(&reader, (unsigned char)'{')) {
    return set_error("seed manifest is not a JSON object");
  }
  json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return set_error("seed manifest fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = json_parse_string(&reader, &key) &&
             json_take(&reader, (unsigned char)':');
    if (!ok) {
      text_release(&key);
      return 0;
    }
    if (text_equals_literal(&key, "schema")) {
      text_t schema = {(unsigned char *)0, 0u};
      if ((fields & 1u) != 0u) {
        text_release(&key);
        return set_error("seed manifest schema is duplicated");
      }
      fields |= 1u;
      ok = json_parse_string(&reader, &schema);
      if (ok && !text_equals_literal(&schema, seed_schema)) {
        ok = set_error("seed manifest schema differs");
      }
      text_release(&schema);
    } else if (text_equals_literal(&key, "artifacts")) {
      if ((fields & 2u) != 0u) {
        text_release(&key);
        return set_error("seed manifest artifacts are duplicated");
      }
      fields |= 2u;
      ok = parse_manifest_artifacts(&reader, manifest);
    } else {
      ok = json_skip_value(&reader, 1u);
    }
    text_release(&key);
    if (!ok) {
      return 0;
    }
    json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!json_take(&reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u) {
    return set_error("seed manifest fields are missing");
  }
  return json_finish(&reader);
}

static void policy_entry_release(policy_entry_t *entry) {
  text_release(&entry->path);
  text_release(&entry->producer);
  text_release(&entry->reason);
  entry->exact_bytes = 0u;
}

static void policy_release(policy_t *policy) {
  size_t index;
  for (index = 0u; index < policy->count; index++) {
    policy_entry_release(&policy->entries[index]);
  }
  policy->count = 0u;
}

static int policy_reason_valid(const text_t *reason) {
  size_t index;
  if (reason->size == 0u || ascii_space(reason->bytes[0]) ||
      ascii_space(reason->bytes[reason->size - 1u])) {
    return 0;
  }
  for (index = 0u; index < reason->size; index++) {
    if (reason->bytes[index] == (unsigned char)'\r' ||
        reason->bytes[index] == (unsigned char)'\n') {
      return 0;
    }
  }
  return 1;
}

static int parse_policy_entry(json_reader_t *reader, policy_entry_t *entry) {
  unsigned int fields = 0u;
  (void)memset(entry, 0, sizeof(*entry));
  if (!json_take(reader, (unsigned char)'{')) {
    return set_error("policy artifact is not a JSON object");
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return set_error("policy artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = json_parse_string(reader, &key) &&
             json_take(reader, (unsigned char)':');
    if (!ok) {
      text_release(&key);
      return 0;
    }
    if (text_equals_literal(&key, "path")) {
      if ((fields & 1u) != 0u) {
        text_release(&key);
        return set_error("policy artifact path is duplicated");
      }
      fields |= 1u;
      ok = json_parse_string(reader, &entry->path);
    } else if (text_equals_literal(&key, "producer")) {
      if ((fields & 2u) != 0u) {
        text_release(&key);
        return set_error("policy artifact producer is duplicated");
      }
      fields |= 2u;
      ok = json_parse_string(reader, &entry->producer);
    } else if (text_equals_literal(&key, "reason")) {
      if ((fields & 4u) != 0u) {
        text_release(&key);
        return set_error("policy artifact reason is duplicated");
      }
      fields |= 4u;
      ok = json_parse_string(reader, &entry->reason);
    } else if (text_equals_literal(&key, "exact_bytes")) {
      if ((fields & 8u) != 0u) {
        text_release(&key);
        return set_error("policy artifact exact size is duplicated");
      }
      fields |= 8u;
      ok = json_parse_positive_u64(reader, &entry->exact_bytes);
    } else {
      text_release(&key);
      return set_error("policy artifact has an unknown field");
    }
    text_release(&key);
    if (!ok) {
      return 0;
    }
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 15u) {
    return set_error("policy artifact fields are missing");
  }
  if (!logical_path_valid(entry->path.bytes, entry->path.size)) {
    return set_error("policy artifact path is unsafe");
  }
  if (entry->producer.size == 0u) {
    return set_error("policy artifact producer is empty");
  }
  if (!policy_reason_valid(&entry->reason)) {
    return set_error("policy artifact reason is invalid");
  }
  return 1;
}

static int parse_policy_artifacts(json_reader_t *reader, policy_t *policy) {
  if (!json_take(reader, (unsigned char)'[')) {
    return 0;
  }
  json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return set_error("policy artifacts are missing");
  }
  for (;;) {
    policy_entry_t entry;
    if (policy->count >= ARTIFACT_COUNT) {
      return set_error("policy has too many artifacts");
    }
    if (!parse_policy_entry(reader, &entry)) {
      policy_entry_release(&entry);
      return 0;
    }
    policy->entries[policy->count] = entry;
    policy->count++;
    json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!json_take(reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (policy->count != ARTIFACT_COUNT) {
    return set_error("policy does not contain nine artifacts");
  }
  return 1;
}

static int parse_policy(byte_slice_t source, policy_t *policy) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(policy, 0, sizeof(*policy));
  if (!json_take(&reader, (unsigned char)'{')) {
    return set_error("policy is not a JSON object");
  }
  json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return set_error("policy fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = json_parse_string(&reader, &key) &&
             json_take(&reader, (unsigned char)':');
    if (!ok) {
      text_release(&key);
      return 0;
    }
    if (text_equals_literal(&key, "schema")) {
      text_t schema = {(unsigned char *)0, 0u};
      if ((fields & 1u) != 0u) {
        text_release(&key);
        return set_error("policy schema is duplicated");
      }
      fields |= 1u;
      ok = json_parse_string(&reader, &schema);
      if (ok && !text_equals_literal(&schema, policy_schema)) {
        ok = set_error("policy schema differs");
      }
      text_release(&schema);
    } else if (text_equals_literal(&key, "artifacts")) {
      if ((fields & 2u) != 0u) {
        text_release(&key);
        return set_error("policy artifacts are duplicated");
      }
      fields |= 2u;
      ok = parse_policy_artifacts(&reader, policy);
    } else {
      text_release(&key);
      return set_error("policy has an unknown field");
    }
    text_release(&key);
    if (!ok) {
      return 0;
    }
    json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!json_take(&reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u) {
    return set_error("policy fields are missing");
  }
  return json_finish(&reader);
}

static size_t manifest_parent_size(const byte_slice_t *manifest_path) {
  size_t position = manifest_path->size;
  while (position > 0u) {
    position--;
    if (manifest_path->bytes[position] == (unsigned char)'/') {
      return position;
    }
  }
  return 0u;
}

static int text_matches_seed_path(const text_t *path,
                                  const byte_slice_t *manifest_path,
                                  const char *filename) {
  size_t parent_size = manifest_parent_size(manifest_path);
  size_t filename_size = strlen(filename);
  if (parent_size == 0u) {
    return path->size == filename_size &&
           memcmp(path->bytes, filename, filename_size) == 0;
  }
  return path->size == parent_size + 1u + filename_size &&
         memcmp(path->bytes, manifest_path->bytes, parent_size) == 0 &&
         path->bytes[parent_size] == (unsigned char)'/' &&
         memcmp(path->bytes + parent_size + 1u, filename, filename_size) == 0;
}

static int validate_policy(policy_t *policy, const seed_manifest_t *manifest,
                           const byte_slice_t *manifest_path,
                           uint64_t *total) {
  int matched[ARTIFACT_COUNT];
  uint64_t sum = 0u;
  uint64_t maximum = ~(uint64_t)0u;
  size_t policy_index;
  (void)memset(matched, 0, sizeof(matched));
  for (policy_index = 0u; policy_index < policy->count; policy_index++) {
    policy_entry_t *entry = &policy->entries[policy_index];
    int expected_index = -1;
    const char *expected_owner = (const char *)0;
    uint64_t seed_size = 0u;
    size_t index;
    if (policy_index > 0u &&
        text_compare(&policy->entries[policy_index - 1u].path, &entry->path) >=
            0) {
      return set_error("policy artifacts are not in canonical order");
    }
    for (index = 0u; index < FIXED_ARTIFACT_COUNT; index++) {
      if (text_equals_literal(&entry->path, fixed_paths[index])) {
        expected_index = (int)index;
        expected_owner = fixed_owners[index];
        break;
      }
    }
    if (expected_index < 0) {
      for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
        if (text_matches_seed_path(&entry->path, manifest_path,
                                   seed_files[index])) {
          expected_index = (int)(FIXED_ARTIFACT_COUNT + index);
          expected_owner = seed_owners[index];
          seed_size = manifest->sizes[index];
          break;
        }
      }
    }
    if (expected_index < 0 || expected_owner == (const char *)0) {
      return set_error("policy has an unknown artifact path");
    }
    if (matched[(size_t)expected_index] != 0) {
      return set_error("policy artifact path is duplicated");
    }
    matched[(size_t)expected_index] = 1;
    if (!text_equals_literal(&entry->producer, expected_owner)) {
      return set_error("policy artifact producer differs");
    }
    if ((size_t)expected_index >= FIXED_ARTIFACT_COUNT &&
        entry->exact_bytes != seed_size) {
      return set_error("policy seed size differs from the selected manifest");
    }
    if (sum > maximum - entry->exact_bytes) {
      return set_error("policy exact byte total exceeds the unsigned range");
    }
    sum += entry->exact_bytes;
  }
  for (policy_index = 0u; policy_index < ARTIFACT_COUNT; policy_index++) {
    if (matched[policy_index] == 0) {
      return set_error("policy is missing a required artifact");
    }
  }
  *total = sum;
  return 1;
}

static int binary_read_u32(binary_reader_t *reader, uint32_t *value) {
  const unsigned char *bytes;
  if (reader->size - reader->position < 4u) {
    return set_error("request is truncated while reading a 32-bit value");
  }
  bytes = reader->bytes + reader->position;
  *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
  reader->position += 4u;
  return 1;
}

static int binary_read_u64(binary_reader_t *reader, uint64_t *value) {
  const unsigned char *bytes;
  uint64_t low;
  uint64_t high;
  if (reader->size - reader->position < 8u) {
    return set_error("request is truncated while reading a 64-bit value");
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

static int binary_read_slice(binary_reader_t *reader, byte_slice_t *slice) {
  uint32_t length;
  if (!binary_read_u32(reader, &length)) {
    return 0;
  }
  if (reader->size - reader->position < (size_t)length) {
    return set_error("request byte string is truncated");
  }
  slice->bytes = reader->bytes + reader->position;
  slice->size = (size_t)length;
  reader->position += (size_t)length;
  return 1;
}

static int validate_observations(const observation_t *observations,
                                 const policy_t *policy) {
  int matched[ARTIFACT_COUNT];
  size_t observation_index;
  (void)memset(matched, 0, sizeof(matched));
  for (observation_index = 0u; observation_index < ARTIFACT_COUNT;
       observation_index++) {
    const observation_t *observation = &observations[observation_index];
    size_t policy_index;
    int found = -1;
    if (observation->kind != 1u) {
      return set_error("artifact observation is not a regular file");
    }
    for (policy_index = 0u; policy_index < policy->count; policy_index++) {
      if (slice_equals_text(&observation->path,
                            &policy->entries[policy_index].path)) {
        found = (int)policy_index;
        break;
      }
    }
    if (found < 0) {
      return set_error("artifact observation has an unknown path");
    }
    if (matched[(size_t)found] != 0) {
      return set_error("artifact observation path is duplicated");
    }
    matched[(size_t)found] = 1;
    if (observation->size != policy->entries[(size_t)found].exact_bytes) {
      return set_error("artifact observation size differs from policy");
    }
  }
  for (observation_index = 0u; observation_index < ARTIFACT_COUNT;
       observation_index++) {
    if (matched[observation_index] == 0) {
      return set_error("artifact observation is missing");
    }
  }
  return 1;
}

static int validate_request(const file_image_t *request, uint64_t *total) {
  binary_reader_t reader = {request->bytes, request->size, 0u};
  byte_slice_t policy_source;
  byte_slice_t manifest_path;
  byte_slice_t manifest_source;
  observation_t observations[ARTIFACT_COUNT];
  seed_manifest_t manifest;
  policy_t policy;
  uint32_t observation_count;
  size_t index;
  int ok = 0;
  (void)memset(&policy, 0, sizeof(policy));
  if (reader.size < sizeof(request_magic) ||
      memcmp(reader.bytes, request_magic, sizeof(request_magic)) != 0) {
    return set_error("request magic differs from CUPSIZE1");
  }
  reader.position = sizeof(request_magic);
  if (!binary_read_slice(&reader, &policy_source) ||
      !binary_read_slice(&reader, &manifest_path) ||
      !binary_read_slice(&reader, &manifest_source) ||
      !binary_read_u32(&reader, &observation_count)) {
    return 0;
  }
  if (!logical_path_valid(manifest_path.bytes, manifest_path.size)) {
    return set_error("seed manifest logical path is unsafe");
  }
  if (observation_count != ARTIFACT_COUNT) {
    return set_error("request does not contain nine artifact observations");
  }
  for (index = 0u; index < ARTIFACT_COUNT; index++) {
    if (!binary_read_slice(&reader, &observations[index].path) ||
        !binary_read_u32(&reader, &observations[index].kind) ||
        !binary_read_u64(&reader, &observations[index].size)) {
      return 0;
    }
    if (!logical_path_valid(observations[index].path.bytes,
                            observations[index].path.size)) {
      return set_error("artifact observation path is unsafe");
    }
  }
  if (reader.position != reader.size) {
    return set_error("request has trailing input");
  }
  if (!parse_seed_manifest(manifest_source, &manifest) ||
      !parse_policy(policy_source, &policy)) {
    policy_release(&policy);
    return 0;
  }
  if (validate_policy(&policy, &manifest, &manifest_path, total) &&
      validate_observations(observations, &policy)) {
    ok = 1;
  }
  policy_release(&policy);
  return ok;
}

static int read_request_file(const char *path, file_image_t *image) {
  FILE *stream;
  long length;
  size_t read_size;
  image->bytes = (unsigned char *)0;
  image->size = 0u;
  stream = fopen(path, "rb");
  if (stream == (FILE *)0) {
    return set_error("request file is unavailable");
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    (void)fclose(stream);
    return set_error("cannot measure the request file");
  }
  length = ftell(stream);
  if (length < 0L || fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    return set_error("request file has an unsupported size");
  }
  image->size = (size_t)length;
  image->bytes = (unsigned char *)malloc(image->size == 0u ? 1u : image->size);
  if (image->bytes == (unsigned char *)0) {
    (void)fclose(stream);
    image->size = 0u;
    return set_error("cannot allocate the request file");
  }
  read_size = fread(image->bytes, 1u, image->size, stream);
  if (read_size != image->size || fclose(stream) != 0) {
    file_image_release(image);
    return set_error("cannot read the request file");
  }
  return 1;
}

static int run_check(const char *path) {
  file_image_t first;
  file_image_t second;
  uint64_t total = 0u;
  int ok;
  contract_error[0] = '\0';
  if (!read_request_file(path, &first)) {
    (void)fprintf(stderr, "Cupid artifact-size contract failed: %s\n",
                  contract_error);
    return 1;
  }
  ok = validate_request(&first, &total);
  if (ok) {
    ok = read_request_file(path, &second);
    if (ok && (second.size != first.size ||
               memcmp(second.bytes, first.bytes, first.size) != 0)) {
      ok = set_error("request changed while it was checked");
    }
    file_image_release(&second);
  }
  file_image_release(&first);
  if (!ok) {
    (void)fprintf(stderr, "Cupid artifact-size contract failed: %s\n",
                  contract_error);
    return 1;
  }
  (void)printf("{\"artifact_count\":9,\"schema\":\"%s\","
               "\"total_exact_bytes\":%llu}\n",
               report_schema, (unsigned long long)total);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "check") == 0) {
    return run_check(argv[2]);
  }
  (void)fprintf(stderr,
                "Cupid artifact-size contract failed: usage: "
                "artifact-size-policy-contract check REQUEST\n");
  return 2;
}
