#include "cupidbuild.h"
#include "ctool.h"
#include "ctool_host.h"
#include "cupidbuild_host.h"
#include "elf32.h"
#if defined(_WIN32)
#include "pe32_impl.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_PATH_BYTES 8192u
#define CUPIDBUILD_MANIFEST_BYTES 1048576u
#define CUPIDBUILD_TOOL_BYTES 67108864u
#define CUPIDBUILD_JSON_TOKENS 2048u
#define CUPIDBUILD_SEED_ARTIFACTS 6u
typedef struct {
  char file[CUPIDBUILD_PATH_BYTES];
  char sha256[65];
  size_t size;
} cupidbuild_seed_artifact_t;

typedef struct {
  char manifest_path[CUPIDBUILD_PATH_BYTES];
  char directory[CUPIDBUILD_PATH_BYTES];
  cupidbuild_seed_artifact_t artifacts[CUPIDBUILD_SEED_ARTIFACTS];
  const char *frozen_tools[CUPIDBUILD_SEED_ARTIFACTS];
  cupidbuild_host_snapshot_t tool_snapshots[CUPIDBUILD_SEED_ARTIFACTS];
  const char *expected_files[CUPIDBUILD_SEED_ARTIFACTS];
  unsigned char *manifest;
  size_t manifest_size;
  size_t artifact_count;
} cupidbuild_seed_capture_t;

static const char *const cupidbuild_seed_names[CUPIDBUILD_SEED_ARTIFACTS] = {
    "CupidASM", "CupidC", "CupidDis", "CupidLD", "CupidObj", "CupidBuild"};

typedef enum {
  CUPIDBUILD_JSON_OBJECT,
  CUPIDBUILD_JSON_ARRAY,
  CUPIDBUILD_JSON_STRING,
  CUPIDBUILD_JSON_PRIMITIVE
} cupidbuild_json_type_t;

typedef struct {
  cupidbuild_json_type_t type;
  size_t start;
  size_t end;
  size_t count;
  size_t parent;
} cupidbuild_json_token_t;

typedef struct {
  const unsigned char *bytes;
  size_t size;
  size_t position;
  cupidbuild_json_token_t *tokens;
  size_t token_count;
} cupidbuild_json_reader_t;

typedef struct {
  const char *name;
  const char *path;
  int gnu_extensions;
} cupidbuild_seed_source_t;

typedef struct {
  unsigned int address;
  size_t order;
  size_t name_start;
  size_t name_size;
} cupidbuild_ksyms_symbol_t;

typedef struct {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
} cupidbuild_ksyms_buffer_t;

typedef enum {
  CUPIDBUILD_KSYMS_EMPTY = 0,
  CUPIDBUILD_KSYMS_IGNORED,
  CUPIDBUILD_KSYMS_TEXT,
  CUPIDBUILD_KSYMS_OMITTED_ADDRESS,
  CUPIDBUILD_KSYMS_MALFORMED,
  CUPIDBUILD_KSYMS_INVALID_ADDRESS,
  CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386
} cupidbuild_ksyms_row_kind_t;

static int cupidbuild_path_safe(const char *path, int relative) {
  const char *cursor;
  if (path == (const char *)0 || path[0] == '\0' || strchr(path, '"') != 0) {
    return 0;
  }
  if (relative != 0 && (path[0] == '/' || path[0] == '\\' || path[1] == ':')) {
    return 0;
  }
  cursor = path;
  while (*cursor != '\0') {
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      cursor++;
    }
    if ((cursor - start == 1 && start[0] == '.') ||
        (cursor - start == 2 && start[0] == '.' && start[1] == '.')) {
      return 0;
    }
    if (*cursor != '\0') {
      cursor++;
    }
  }
  return 1;
}

static int cupidbuild_join(char *destination, size_t capacity, const char *left,
                           const char *right) {
  size_t left_size = strlen(left);
  size_t right_size = strlen(right);
  int separator = left_size != 0u && left[left_size - 1u] != '/' &&
                          left[left_size - 1u] != '\\'
                      ? 1
                      : 0;
  if (left_size + (size_t)separator + right_size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, left, left_size);
  if (separator != 0) {
    destination[left_size++] = '/';
  }
  (void)memcpy(destination + left_size, right, right_size + 1u);
  return 1;
}

static int cupidbuild_repository_prefix(const char *path, const char *root) {
  size_t index;
  size_t root_size = strlen(root);
  for (index = 0u; index < root_size; index++) {
    char left = path[index];
    char right = root[index];
#if defined(_WIN32)
    if (left == '\\') {
      left = '/';
    }
    if (right == '\\') {
      right = '/';
    }
    if (left >= 'A' && left <= 'Z') {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (char)(right - 'A' + 'a');
    }
#endif
    if (left != right) {
      return 0;
    }
  }
  return path[root_size] == '/' || path[root_size] == '\\';
}

static void cupidbuild_json_space(cupidbuild_json_reader_t *reader) {
  while (reader->position < reader->size &&
         (reader->bytes[reader->position] == ' ' ||
          reader->bytes[reader->position] == '\t' ||
          reader->bytes[reader->position] == '\r' ||
          reader->bytes[reader->position] == '\n')) {
    reader->position++;
  }
}

static int cupidbuild_json_token(cupidbuild_json_reader_t *reader,
                                 cupidbuild_json_type_t type, size_t parent,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (reader->token_count >= CUPIDBUILD_JSON_TOKENS) {
    return 0;
  }
  *index_out = reader->token_count++;
  token = &reader->tokens[*index_out];
  token->type = type;
  token->start = reader->position;
  token->end = reader->position;
  token->count = 0u;
  token->parent = parent;
  return 1;
}

static int cupidbuild_json_hex(unsigned char byte) {
  return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
         (byte >= 'A' && byte <= 'F');
}

static int cupidbuild_json_string(cupidbuild_json_reader_t *reader,
                                  size_t parent, size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (reader->position >= reader->size ||
      reader->bytes[reader->position] != '"' ||
      !cupidbuild_json_token(reader, CUPIDBUILD_JSON_STRING, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  token->start = reader->position;
  while (reader->position < reader->size) {
    unsigned char byte = reader->bytes[reader->position++];
    if (byte == '"') {
      token->end = reader->position - 1u;
      return 1;
    }
    if (byte < 0x20u) {
      return 0;
    }
    if (byte == '\\') {
      size_t remaining;
      unsigned char escape;
      if (reader->position >= reader->size) {
        return 0;
      }
      escape = reader->bytes[reader->position++];
      if (escape == 'u') {
        remaining = reader->size - reader->position;
        if (remaining < 4u ||
            !cupidbuild_json_hex(reader->bytes[reader->position]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 1u]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 2u]) ||
            !cupidbuild_json_hex(reader->bytes[reader->position + 3u])) {
          return 0;
        }
        reader->position += 4u;
      } else if (escape != '"' && escape != '\\' && escape != '/' &&
                 escape != 'b' && escape != 'f' && escape != 'n' &&
                 escape != 'r' && escape != 't') {
        return 0;
      }
    }
  }
  return 0;
}

static int cupidbuild_json_number(const unsigned char *bytes, size_t size) {
  size_t position = 0u;
  if (position < size && bytes[position] == '-') {
    position++;
  }
  if (position >= size) {
    return 0;
  }
  if (bytes[position] == '0') {
    position++;
  } else if (bytes[position] >= '1' && bytes[position] <= '9') {
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  } else {
    return 0;
  }
  if (position < size && bytes[position] == '.') {
    position++;
    if (position >= size || bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  }
  if (position < size && (bytes[position] == 'e' || bytes[position] == 'E')) {
    position++;
    if (position < size && (bytes[position] == '+' || bytes[position] == '-')) {
      position++;
    }
    if (position >= size || bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    do {
      position++;
    } while (position < size && bytes[position] >= '0' &&
             bytes[position] <= '9');
  }
  return position == size;
}

static int cupidbuild_json_value(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out);

static int cupidbuild_json_object(cupidbuild_json_reader_t *reader,
                                  size_t parent, unsigned int depth,
                                  size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_OBJECT, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  cupidbuild_json_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == '}') {
    reader->position++;
    token->end = reader->position;
    return 1;
  }
  for (;;) {
    size_t key;
    size_t value;
    if (!cupidbuild_json_string(reader, *index_out, &key)) {
      return 0;
    }
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size ||
        reader->bytes[reader->position++] != ':') {
      return 0;
    }
    cupidbuild_json_space(reader);
    if (!cupidbuild_json_value(reader, *index_out, depth + 1u, &value)) {
      return 0;
    }
    token->count++;
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size) {
      return 0;
    }
    if (reader->bytes[reader->position] == '}') {
      reader->position++;
      token->end = reader->position;
      return 1;
    }
    if (reader->bytes[reader->position++] != ',') {
      return 0;
    }
    cupidbuild_json_space(reader);
  }
}

static int cupidbuild_json_array(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_ARRAY, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  reader->position++;
  cupidbuild_json_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == ']') {
    reader->position++;
    token->end = reader->position;
    return 1;
  }
  for (;;) {
    size_t value;
    if (!cupidbuild_json_value(reader, *index_out, depth + 1u, &value)) {
      return 0;
    }
    token->count++;
    cupidbuild_json_space(reader);
    if (reader->position >= reader->size) {
      return 0;
    }
    if (reader->bytes[reader->position] == ']') {
      reader->position++;
      token->end = reader->position;
      return 1;
    }
    if (reader->bytes[reader->position++] != ',') {
      return 0;
    }
    cupidbuild_json_space(reader);
  }
}

static int cupidbuild_json_value(cupidbuild_json_reader_t *reader,
                                 size_t parent, unsigned int depth,
                                 size_t *index_out) {
  cupidbuild_json_token_t *token;
  size_t start;
  if (depth > 64u || reader->position >= reader->size) {
    return 0;
  }
  if (reader->bytes[reader->position] == '{') {
    return cupidbuild_json_object(reader, parent, depth, index_out);
  }
  if (reader->bytes[reader->position] == '[') {
    return cupidbuild_json_array(reader, parent, depth, index_out);
  }
  if (reader->bytes[reader->position] == '"') {
    return cupidbuild_json_string(reader, parent, index_out);
  }
  if (!cupidbuild_json_token(reader, CUPIDBUILD_JSON_PRIMITIVE, parent,
                             index_out)) {
    return 0;
  }
  token = &reader->tokens[*index_out];
  start = reader->position;
  while (reader->position < reader->size &&
         reader->bytes[reader->position] != ' ' &&
         reader->bytes[reader->position] != '\t' &&
         reader->bytes[reader->position] != '\r' &&
         reader->bytes[reader->position] != '\n' &&
         reader->bytes[reader->position] != ',' &&
         reader->bytes[reader->position] != ']' &&
         reader->bytes[reader->position] != '}') {
    reader->position++;
  }
  token->end = reader->position;
  if (token->end == start) {
    return 0;
  }
  if ((token->end - start == 4u &&
       memcmp(reader->bytes + start, "true", 4u) == 0) ||
      (token->end - start == 5u &&
       memcmp(reader->bytes + start, "false", 5u) == 0) ||
      (token->end - start == 4u &&
       memcmp(reader->bytes + start, "null", 4u) == 0)) {
    return 1;
  }
  return cupidbuild_json_number(reader->bytes + start, token->end - start);
}

static int cupidbuild_json_parse(const unsigned char *bytes, size_t size,
                                 cupidbuild_json_token_t *tokens,
                                 size_t *count_out) {
  cupidbuild_json_reader_t reader;
  size_t root;
  reader.bytes = bytes;
  reader.size = size;
  reader.position = 0u;
  reader.tokens = tokens;
  reader.token_count = 0u;
  cupidbuild_json_space(&reader);
  if (!cupidbuild_json_value(&reader, (size_t)-1, 0u, &root)) {
    return 0;
  }
  cupidbuild_json_space(&reader);
  if (root != 0u || reader.position != reader.size) {
    return 0;
  }
  *count_out = reader.token_count;
  return 1;
}

static int cupidbuild_json_text(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                const char *expected) {
  size_t size = token->end - token->start;
  return strlen(expected) == size &&
         memcmp(bytes + token->start, expected, size) == 0;
}

static size_t cupidbuild_json_next(const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t index) {
  size_t next = index + 1u;
  while (next < count && tokens[next].start < tokens[index].end) {
    next++;
  }
  return next;
}

static size_t cupidbuild_json_field(const unsigned char *bytes,
                                    const cupidbuild_json_token_t *tokens,
                                    size_t count, size_t object,
                                    const char *name, size_t *matches_out) {
  size_t cursor = object + 1u;
  size_t matches = 0u;
  size_t value = count;
  size_t pair;
  for (pair = 0u; pair < tokens[object].count && cursor + 1u < count; pair++) {
    size_t candidate = cursor + 1u;
    if (cupidbuild_json_text(bytes, &tokens[cursor], name)) {
      matches++;
      value = candidate;
    }
    cursor = cupidbuild_json_next(tokens, count, candidate);
  }
  *matches_out = matches;
  return value;
}

static int cupidbuild_json_exact(const unsigned char *bytes,
                                 const cupidbuild_json_token_t *tokens,
                                 size_t count, size_t object,
                                 const char *const *names, size_t name_count) {
  size_t index;
  if (tokens[object].type != CUPIDBUILD_JSON_OBJECT ||
      tokens[object].count != name_count) {
    return 0;
  }
  for (index = 0u; index < name_count; index++) {
    size_t matches;
    (void)cupidbuild_json_field(bytes, tokens, count, object, names[index],
                                &matches);
    if (matches != 1u) {
      return 0;
    }
  }
  return 1;
}

static size_t cupidbuild_json_required(const unsigned char *bytes,
                                       const cupidbuild_json_token_t *tokens,
                                       size_t count, size_t object,
                                       const char *name) {
  size_t matches;
  size_t value =
      cupidbuild_json_field(bytes, tokens, count, object, name, &matches);
  return matches == 1u ? value : count;
}

static int cupidbuild_json_size(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                size_t *value_out) {
  size_t value = 0u;
  size_t position;
  if (token->type != CUPIDBUILD_JSON_PRIMITIVE || token->start == token->end) {
    return 0;
  }
  for (position = token->start; position < token->end; position++) {
    unsigned int digit;
    if (bytes[position] < '0' || bytes[position] > '9') {
      return 0;
    }
    digit = (unsigned int)(bytes[position] - '0');
    if (value > ((size_t)-1 - digit) / 10u) {
      return 0;
    }
    value = value * 10u + digit;
  }
  *value_out = value;
  return 1;
}

static int cupidbuild_json_boolean(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *token,
                                   int *value_out) {
  if (token->type != CUPIDBUILD_JSON_PRIMITIVE) {
    return 0;
  }
  if (cupidbuild_json_text(bytes, token, "true")) {
    *value_out = 1;
    return 1;
  }
  if (cupidbuild_json_text(bytes, token, "false")) {
    *value_out = 0;
    return 1;
  }
  return 0;
}

static int cupidbuild_json_copy(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                char *destination, size_t capacity) {
  size_t size = token->end - token->start;
  if (token->type != CUPIDBUILD_JSON_STRING || size + 1u > capacity ||
      memchr(bytes + token->start, '\\', size) != (void *)0) {
    return 0;
  }
  (void)memcpy(destination, bytes + token->start, size);
  destination[size] = '\0';
  return 1;
}

static int cupidbuild_json_string_field(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t object,
                                        const char *name,
                                        const char *expected) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  return value < count && tokens[value].type == CUPIDBUILD_JSON_STRING &&
         cupidbuild_json_text(bytes, &tokens[value], expected);
}

static int cupidbuild_json_lower_hex_field(
    const unsigned char *bytes, const cupidbuild_json_token_t *tokens,
    size_t count, size_t object, const char *name, size_t expected_size) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t index;
  if (value >= count || tokens[value].type != CUPIDBUILD_JSON_STRING ||
      tokens[value].end - tokens[value].start != expected_size) {
    return 0;
  }
  for (index = tokens[value].start; index < tokens[value].end; index++) {
    unsigned char digit = bytes[index];
    if (!((digit >= '0' && digit <= '9') ||
          (digit >= 'a' && digit <= 'f'))) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_json_number_field(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t object,
                                        const char *name, size_t expected) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t actual;
  return value < count &&
         cupidbuild_json_size(bytes, &tokens[value], &actual) &&
         actual == expected;
}

#if !defined(_WIN32)
static int cupidbuild_json_string_array(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t array,
                                        const char *const *expected,
                                        size_t expected_count) {
  size_t cursor;
  size_t index;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (index = 0u; index < expected_count; index++) {
    if (cursor >= count || tokens[cursor].type != CUPIDBUILD_JSON_STRING ||
        !cupidbuild_json_text(bytes, &tokens[cursor], expected[index])) {
      return 0;
    }
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  return 1;
}
#endif

static int cupidbuild_json_lineage(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t object, int windows) {
  static const char *const names[] = {"assembly", "c", "link"};
  const char *assembly =
      windows ? "native stage-three CupidASM from the checked i386 Windows "
                "bootstrap"
              : "stage-three CupidASM from the checked-seed bootstrap";
  const char *c =
      windows
          ? "native stage-three CupidC from the checked i386 Windows bootstrap"
          : "stage-three CupidC from the checked-seed bootstrap";
  const char *link =
      windows
          ? "native stage-three CupidLD from the checked i386 Windows bootstrap"
          : "stage-three CupidLD from the checked-seed bootstrap";
  return object < count &&
         cupidbuild_json_exact(bytes, tokens, count, object, names, 3u) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "assembly",
                                      assembly) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "c", c) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "link",
                                      link);
}

static int cupidbuild_json_provenance(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object,
                                      int windows, int promoted) {
  static const char legacy_revision[] =
      "a17c9465911da41d59b7ada71733d36c39faa5ea";
  static const char legacy_snapshot[] =
      "46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67";
  static const char legacy_linux_manifest[] =
      "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b";
  static const char legacy_windows_manifest[] =
      "751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef";
  static const char *const linux_v1_names[] = {
      "fixed_point_command",   "fixed_point_result", "producer_lineage",
      "seed_generation",       "source_input_count", "source_revision",
      "source_snapshot_sha256"};
  static const char *const linux_v2_names[] = {
      "artifact_generation",         "fixed_point_command",
      "fixed_point_result",          "parent_seed_manifest_sha256",
      "parent_seed_source_revision", "producer_lineage",
      "seed_generation",             "source_input_count",
      "source_revision",             "source_snapshot_sha256"};
  static const char *const windows_v1_names[] = {
      "artifact_generation",         "fixed_point_command",
      "fixed_point_result",          "parent_seed_manifest_sha256",
      "parent_seed_source_revision", "producer_lineage",
      "source_input_count",          "source_revision",
      "source_snapshot_sha256"};
  static const char *const windows_v2_names[] = {
      "artifact_generation",
      "fixed_point_command",
      "fixed_point_result",
      "parent_execution_seed_manifest_sha256",
      "parent_execution_seed_source_revision",
      "linux_candidate_build_plan_sha256",
      "native_build_plan_sha256",
      "plan_seed_manifest_sha256",
      "parent_plan_seed_manifest_sha256",
      "parent_plan_seed_source_revision",
      "producer_lineage",
      "source_input_count",
      "source_revision",
      "source_snapshot_sha256"};
  const char *const *names =
      windows ? (promoted ? windows_v2_names : windows_v1_names)
              : (promoted ? linux_v2_names : linux_v1_names);
  size_t name_count = windows ? (promoted ? 14u : 9u)
                              : (promoted ? 10u : 7u);
  size_t lineage;
  int source_count_matches;
  if (object >= count)
    return 0;
  source_count_matches =
      promoted
          ? (cupidbuild_json_number_field(bytes, tokens, count, object,
                                           "source_input_count", 58u) ||
             cupidbuild_json_number_field(bytes, tokens, count, object,
                                           "source_input_count", 59u))
          : cupidbuild_json_number_field(bytes, tokens, count, object,
                                         "source_input_count", 50u);
  if (!cupidbuild_json_exact(bytes, tokens, count, object, names,
                             name_count) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "fixed_point_result", "pass") ||
      !source_count_matches ||
      (promoted
           ? (!cupidbuild_json_lower_hex_field(
                  bytes, tokens, count, object, "source_revision", 40u) ||
              !cupidbuild_json_lower_hex_field(
                  bytes, tokens, count, object, "source_snapshot_sha256",
                  64u))
           : (!cupidbuild_json_string_field(
                  bytes, tokens, count, object, "source_revision",
                  legacy_revision) ||
              !cupidbuild_json_string_field(
                  bytes, tokens, count, object, "source_snapshot_sha256",
                  legacy_snapshot)))) {
    return 0;
  }
  lineage = cupidbuild_json_required(bytes, tokens, count, object,
                                     "producer_lineage");
  if (!cupidbuild_json_lineage(bytes, tokens, count, lineage, windows)) {
    return 0;
  }
  if (windows) {
    if (!cupidbuild_json_string_field(
            bytes, tokens, count, object, "artifact_generation",
            promoted ? "paired-stage-four-six-tool-native-windows"
                     : "paired-stage-four-native-windows") ||
        !cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "fixed_point_command",
                                      "make bootstrap-windows-from-seed") ||
        !(promoted
              ? (cupidbuild_json_string_field(
                     bytes, tokens, count, object,
                     "parent_execution_seed_manifest_sha256",
                     legacy_windows_manifest) &&
                 cupidbuild_json_string_field(
                     bytes, tokens, count, object,
                     "parent_execution_seed_source_revision",
                     legacy_revision))
              : (cupidbuild_json_string_field(
                     bytes, tokens, count, object,
                     "parent_seed_manifest_sha256",
                     legacy_linux_manifest) &&
                 cupidbuild_json_string_field(
                     bytes, tokens, count, object,
                     "parent_seed_source_revision", legacy_revision)))) {
      return 0;
    }
    return !promoted ||
           (cupidbuild_json_string_field(
                bytes, tokens, count, object,
                "linux_candidate_build_plan_sha256",
                "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd") &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object, "native_build_plan_sha256",
                "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14") &&
            cupidbuild_json_lower_hex_field(
                bytes, tokens, count, object, "plan_seed_manifest_sha256",
                64u) &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object,
                "parent_plan_seed_manifest_sha256", legacy_linux_manifest) &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "parent_plan_seed_source_revision",
                                        legacy_revision));
  }
  if (!cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "fixed_point_command",
                                    "make bootstrap-from-seed") ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "seed_generation", "stage-four")) {
    return 0;
  }
  return !promoted ||
         (cupidbuild_json_string_field(
              bytes, tokens, count, object, "artifact_generation",
              "paired-stage-four-six-tool") &&
          cupidbuild_json_string_field(
              bytes, tokens, count, object, "parent_seed_manifest_sha256",
              legacy_linux_manifest) &&
          cupidbuild_json_string_field(
              bytes, tokens, count, object, "parent_seed_source_revision",
              legacy_revision));
}

static int cupidbuild_json_target(const unsigned char *bytes,
                                  const cupidbuild_json_token_t *tokens,
                                  size_t count, size_t object, int windows) {
  static const char *const linux_names[] = {
      "abi",   "architecture", "byte_order",      "elf_class",
      "entry", "linkage",      "operating_system"};
  static const char *const windows_names[] = {
      "abi",     "architecture",     "byte_order", "entry",
      "linkage", "operating_system", "pe_class"};
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object,
                             windows ? windows_names : linux_names, 7u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "architecture", "i386") ||
      !cupidbuild_json_string_field(bytes, tokens, count, object, "byte_order",
                                    "little")) {
    return 0;
  }
  if (windows) {
    return cupidbuild_json_string_field(bytes, tokens, count, object, "abi",
                                        "windows-stdcall-imports") &&
           cupidbuild_json_number_field(bytes, tokens, count, object, "entry",
                                        4198400u) &&
           cupidbuild_json_string_field(bytes, tokens, count, object, "linkage",
                                        "kernel32-imports") &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "operating_system", "windows") &&
           cupidbuild_json_number_field(bytes, tokens, count, object,
                                        "pe_class", 32u);
  }
  return cupidbuild_json_string_field(bytes, tokens, count, object, "abi",
                                      "linux-int80") &&
         cupidbuild_json_number_field(bytes, tokens, count, object, "entry",
                                      134512640u) &&
         cupidbuild_json_string_field(bytes, tokens, count, object, "linkage",
                                      "static") &&
         cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "operating_system", "linux") &&
         cupidbuild_json_number_field(bytes, tokens, count, object, "elf_class",
                                      32u);
}

#if !defined(_WIN32)
static int cupidbuild_json_sources(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t array, int promoted) {
  static const char *const names[] = {"gnu_extensions", "name", "path"};
  static const cupidbuild_seed_source_t expected[] = {
      {"runtime", "/toolchain/hosted/i386-linux/runtime.cc", 1},
      {"ctool", "/toolchain/ctool.cc", 0},
      {"ctool_host", "/toolchain/ctool_host.cc", 0},
      {"elf32", "/toolchain/elf32.cc", 0},
      {"x86", "/toolchain/x86.cc", 0},
      {"cupidasm", "/toolchain/cupidasm.cc", 0},
      {"cupidasm_main", "/toolchain/cupidasm_main.cc", 0},
      {"cupiddis", "/toolchain/cupiddis.cc", 0},
      {"cupiddis_main", "/toolchain/cupiddis_main.cc", 0},
      {"cupidobj", "/toolchain/cupidobj.cc", 0},
      {"cupidobj_main", "/toolchain/cupidobj_main.cc", 0},
      {"cupidld", "/toolchain/cupidld.cc", 0},
      {"cupidld_main", "/toolchain/cupidld_main.cc", 0},
      {"cupidc_pp", "/toolchain/cupidc_pp.cc", 0},
      {"cupidc_type", "/toolchain/cupidc_type.cc", 0},
      {"cupidc_frontend", "/toolchain/cupidc_frontend.cc", 0},
      {"cupidc_ir", "/toolchain/cupidc_ir.cc", 0},
      {"cupidc_emit", "/toolchain/cupidc_emit.cc", 0},
      {"cupidc_main", "/toolchain/cupidc_main.cc", 0},
      {"cupidbuild", "/toolchain/cupidbuild.cc", 0},
      {"cupidbuild_host", "/toolchain/cupidbuild_host.cc", 0},
      {"cupidbuild_main", "/toolchain/cupidbuild_main.cc", 0}};
  size_t cursor;
  size_t index;
  size_t expected_count = promoted ? 22u : 19u;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (index = 0u; index < expected_count; index++) {
    size_t extensions;
    int actual_extensions;
    if (cursor >= count ||
        !cupidbuild_json_exact(bytes, tokens, count, cursor, names, 3u) ||
        !cupidbuild_json_string_field(bytes, tokens, count, cursor, "name",
                                      expected[index].name) ||
        !cupidbuild_json_string_field(bytes, tokens, count, cursor, "path",
                                      expected[index].path)) {
      return 0;
    }
    extensions = cupidbuild_json_required(bytes, tokens, count, cursor,
                                          "gnu_extensions");
    if (extensions >= count ||
        !cupidbuild_json_boolean(bytes, &tokens[extensions],
                                 &actual_extensions) ||
        actual_extensions != expected[index].gnu_extensions) {
      return 0;
    }
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  return 1;
}

static int cupidbuild_json_links(const unsigned char *bytes,
                                 const cupidbuild_json_token_t *tokens,
                                 size_t count, size_t object, int promoted) {
  static const char *const names[] = {"cupidasm", "cupiddis", "cupidld",
                                      "cupidobj", "cupidc", "cupidbuild"};
  static const char *const cupidasm[] = {
      "start", "cupidasm_main", "cupidasm", "ctool_host",
      "ctool", "elf32",         "x86",      "runtime"};
  static const char *const cupiddis[] = {
      "start", "cupiddis_main", "cupiddis", "ctool_host",
      "ctool", "elf32",         "x86",      "runtime"};
  static const char *const cupidld[] = {"start",      "cupidld_main", "cupidld",
                                        "ctool_host", "ctool",        "elf32",
                                        "runtime"};
  static const char *const cupidobj[] = {
      "start", "cupidobj_main", "cupidobj", "ctool_host",
      "ctool", "elf32",         "runtime"};
  static const char *const cupidc[] = {
      "start",           "cupidc_main", "cupidc_emit", "cupidc_ir",
      "cupidc_frontend", "cupidc_type", "cupidc_pp",   "ctool_host",
      "ctool",           "elf32",       "x86",         "runtime"};
  static const char *const cupidbuild[] = {
      "start", "cupidbuild_main", "cupidbuild", "cupidbuild_host",
      "ctool_host", "ctool", "elf32", "runtime"};
  size_t value;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object, names,
                             promoted ? 6u : 5u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidasm");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidasm,
                                    8u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupiddis");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupiddis,
                                    8u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidld");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidld, 7u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidobj");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidobj,
                                    7u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidc");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, cupidc,
                                    12u)) {
    return 0;
  }
  if (!promoted) {
    return 1;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "cupidbuild");
  return cupidbuild_json_string_array(bytes, tokens, count, value,
                                      cupidbuild, 8u);
}

static int cupidbuild_json_build_plan(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object,
                                      int promoted) {
  static const char *const names[] = {"include_arguments", "links",
                                      "producer_tools",    "sources",
                                      "startup",           "workers"};
  static const char *const includes[] = {
      "-I", "/toolchain", "--include-angle",
      "/toolchain/hosted/i386-linux/include"};
  static const char *const producers[] = {"cupidc", "cupidasm", "cupidld"};
  size_t value;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object, names, 6u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object, "startup",
                                    "/toolchain/hosted/i386-linux/start.asm") ||
      !cupidbuild_json_number_field(bytes, tokens, count, object, "workers",
                                    2u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object,
                                   "include_arguments");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, includes,
                                    4u)) {
    return 0;
  }
  value =
      cupidbuild_json_required(bytes, tokens, count, object, "producer_tools");
  if (!cupidbuild_json_string_array(bytes, tokens, count, value, producers,
                                    3u)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "sources");
  if (!cupidbuild_json_sources(bytes, tokens, count, value, promoted)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "links");
  return cupidbuild_json_links(bytes, tokens, count, value, promoted);
}
#endif

static int cupidbuild_json_artifacts(const unsigned char *bytes,
                                     const cupidbuild_json_token_t *tokens,
                                     size_t count, size_t array, int windows,
                                     int promoted,
                                     cupidbuild_seed_artifact_t
                                         artifacts[CUPIDBUILD_SEED_ARTIFACTS]) {
  static const char *const fields[] = {"file", "name", "producer", "sha256",
                                       "size"};
  static const char *const names[] = {"cupidasm", "cupidc", "cupiddis",
                                      "cupidld", "cupidobj", "cupidbuild"};
  static const int producers[] = {1, 1, 0, 1, 0, 0};
  int seen[CUPIDBUILD_SEED_ARTIFACTS] = {0, 0, 0, 0, 0, 0};
  size_t cursor;
  size_t item;
  size_t expected_count = promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != expected_count) {
    return 0;
  }
  cursor = array + 1u;
  for (item = 0u; item < expected_count; item++) {
    size_t name_token;
    size_t file_token;
    size_t digest_token;
    size_t producer_token;
    size_t size_token;
    size_t index;
    size_t actual_size;
    int actual_producer;
    char expected_file[32];
    if (cursor >= count ||
        !cupidbuild_json_exact(bytes, tokens, count, cursor, fields, 5u)) {
      return 0;
    }
    name_token = cupidbuild_json_required(bytes, tokens, count, cursor, "name");
    for (index = 0u; index < CUPIDBUILD_SEED_ARTIFACTS; index++) {
      if (name_token < count &&
          cupidbuild_json_text(bytes, &tokens[name_token], names[index])) {
        break;
      }
    }
    if (index == CUPIDBUILD_SEED_ARTIFACTS || seen[index] != 0) {
      return 0;
    }
    seen[index] = 1;
    file_token = cupidbuild_json_required(bytes, tokens, count, cursor, "file");
    digest_token =
        cupidbuild_json_required(bytes, tokens, count, cursor, "sha256");
    producer_token =
        cupidbuild_json_required(bytes, tokens, count, cursor, "producer");
    size_token = cupidbuild_json_required(bytes, tokens, count, cursor, "size");
    if (snprintf(expected_file, sizeof(expected_file), "%s.%s", names[index],
                 windows ? "exe" : "elf") < 0 ||
        file_token >= count || digest_token >= count ||
        producer_token >= count || size_token >= count ||
        !cupidbuild_json_text(bytes, &tokens[file_token], expected_file) ||
        !cupidbuild_json_copy(bytes, &tokens[file_token], artifacts[index].file,
                              sizeof(artifacts[index].file)) ||
        !cupidbuild_json_copy(bytes, &tokens[digest_token],
                              artifacts[index].sha256,
                              sizeof(artifacts[index].sha256)) ||
        strlen(artifacts[index].sha256) != 64u ||
        !cupidbuild_json_boolean(bytes, &tokens[producer_token],
                                 &actual_producer) ||
        actual_producer != producers[index] ||
        !cupidbuild_json_size(bytes, &tokens[size_token], &actual_size) ||
        actual_size == 0u) {
      return 0;
    }
    for (size_token = 0u; size_token < 64u; size_token++) {
      char digit = artifacts[index].sha256[size_token];
      if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
        return 0;
      }
    }
    artifacts[index].size = actual_size;
    cursor = cupidbuild_json_next(tokens, count, cursor);
  }
  for (item = 0u; item < CUPIDBUILD_SEED_ARTIFACTS; item++) {
    if (seen[item] != (item < expected_count ? 1 : 0)) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_json_manifest(const unsigned char *manifest,
                                    size_t manifest_size,
                                    cupidbuild_seed_artifact_t
                                        artifacts[CUPIDBUILD_SEED_ARTIFACTS],
                                    size_t *artifact_count_out,
                                    const char **reason_out) {
  cupidbuild_json_token_t *tokens;
  size_t count = 0u;
  size_t artifacts_token;
  size_t provenance;
  size_t schema;
  size_t target;
  int windows;
  int promoted;
#if defined(_WIN32)
  static const char legacy_schema[] = "cupid.execution-seed.v1";
  static const char promoted_schema[] = "cupid.execution-seed.v2";
  static const char *const top_names[] = {"artifacts", "provenance", "schema",
                                          "target"};
  windows = 1;
#else
  static const char legacy_schema[] = "cupid.bootstrap-seed.v1";
  static const char promoted_schema[] = "cupid.bootstrap-seed.v2";
  static const char *const top_names[] = {
      "artifacts",  "build_plan", "build_plan_sha256",
      "provenance", "schema",     "target"};
  windows = 0;
#endif
  tokens = (cupidbuild_json_token_t *)malloc(CUPIDBUILD_JSON_TOKENS *
                                             sizeof(cupidbuild_json_token_t));
  if (tokens == (cupidbuild_json_token_t *)0) {
    *reason_out = "JSON token storage is unavailable";
    return 0;
  }
  if (!cupidbuild_json_parse(manifest, manifest_size, tokens, &count) ||
      count == 0u) {
    *reason_out = "JSON syntax is malformed";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_exact(manifest, tokens, count, 0u, top_names,
                             sizeof(top_names) / sizeof(top_names[0]))) {
    *reason_out = "top-level fields differ";
    free(tokens);
    return 0;
  }
  schema = cupidbuild_json_required(manifest, tokens, count, 0u, "schema");
  artifacts_token =
      cupidbuild_json_required(manifest, tokens, count, 0u, "artifacts");
  provenance =
      cupidbuild_json_required(manifest, tokens, count, 0u, "provenance");
  target = cupidbuild_json_required(manifest, tokens, count, 0u, "target");
  if (schema >= count || tokens[schema].type != CUPIDBUILD_JSON_STRING) {
    *reason_out = "schema differs";
    free(tokens);
    return 0;
  }
  promoted = cupidbuild_json_text(manifest, &tokens[schema], promoted_schema);
  if (!promoted &&
      !cupidbuild_json_text(manifest, &tokens[schema], legacy_schema)) {
    *reason_out = "schema differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_artifacts(manifest, tokens, count, artifacts_token,
                                 windows, promoted, artifacts)) {
    *reason_out = "artifact inventory differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_provenance(manifest, tokens, count, provenance,
                                  windows, promoted)) {
    *reason_out = "fixed-point provenance differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_target(manifest, tokens, count, target, windows)) {
    *reason_out = "target contract differs";
    free(tokens);
    return 0;
  }
#if !defined(_WIN32)
  {
    size_t plan =
        cupidbuild_json_required(manifest, tokens, count, 0u, "build_plan");
    const char *expected_plan_sha256 =
        promoted
            ? "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
            : "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc";
    if (!cupidbuild_json_string_field(manifest, tokens, count, 0u,
                                      "build_plan_sha256",
                                      expected_plan_sha256) ||
        !cupidbuild_json_build_plan(manifest, tokens, count, plan,
                                    promoted)) {
      *reason_out = "build plan differs";
      free(tokens);
      return 0;
    }
  }
#endif
  *artifact_count_out = promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u;
  free(tokens);
  return 1;
}

static int cupidbuild_manifest_directory(const char *manifest, char *directory,
                                         size_t capacity) {
  const char *cursor = manifest;
  const char *last = (const char *)0;
  size_t size;
  while (*cursor != '\0') {
    if (*cursor == '/' || *cursor == '\\') {
      last = cursor;
    }
    cursor++;
  }
  if (last == (const char *)0) {
    return 0;
  }
  size = (size_t)(last - manifest);
  if (size == 0u || size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(directory, manifest, size);
  directory[size] = '\0';
  return 1;
}

static int cupidbuild_digest_matches_hex(const unsigned char digest[32],
                                         const char *hex) {
  static const char digits[] = "0123456789abcdef";
  size_t index;
  for (index = 0u; index < 32u; index++) {
    if (hex[index * 2u] != digits[digest[index] >> 4u] ||
        hex[index * 2u + 1u] != digits[digest[index] & 15u]) {
      return 0;
    }
  }
  return hex[64] == '\0';
}

static int
cupidbuild_artifact_matches(const cupidbuild_host_snapshot_t *snapshot,
                            const cupidbuild_seed_artifact_t *artifact) {
  return snapshot->present != 0 && snapshot->size == artifact->size &&
         cupidbuild_digest_matches_hex(snapshot->sha256, artifact->sha256);
}

static int cupidbuild_validate_relocatable(const unsigned char *bytes,
                                           size_t size,
                                           int require_executable) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 index;
  int relocatable = 0;
  int executable = 0;
  if (size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    return 0;
  }
  source.path.text = ctool_string("/candidate.o");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
      object.file_type == CTOOL_ELF32_ET_REL) {
    relocatable = 1;
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u &&
          section->size != 0u) {
        executable = 1;
      }
    }
  }
  ctool_job_close(job);
  return relocatable != 0 &&
         (require_executable == 0 || executable != 0);
}

static int cupidbuild_jpeg_symbol_name_matches(ctool_string_t actual,
                                                const char *identity,
                                                const char *suffix) {
  static const char prefix[] = "_binary_";
  size_t prefix_size = sizeof(prefix) - 1u;
  size_t identity_size = strlen(identity);
  size_t suffix_size = strlen(suffix);
  size_t index;
  if (identity_size > (size_t)-1 - prefix_size - suffix_size ||
      prefix_size + identity_size + suffix_size != (size_t)actual.size ||
      memcmp(actual.data, prefix, prefix_size) != 0 ||
      memcmp(actual.data + prefix_size + identity_size, suffix,
             suffix_size) != 0) {
    return 0;
  }
  for (index = 0u; index < identity_size; index++) {
    unsigned char character = (unsigned char)identity[index];
    char expected = ((character >= (unsigned char)'a' &&
                      character <= (unsigned char)'z') ||
                     (character >= (unsigned char)'A' &&
                      character <= (unsigned char)'Z') ||
                     (character >= (unsigned char)'0' &&
                      character <= (unsigned char)'9'))
                        ? (char)character
                        : '_';
    if (actual.data[prefix_size + index] != expected) {
      return 0;
    }
  }
  return 1;
}

int cupidbuild_validate_jpeg_object_bytes(
    const unsigned char *object_bytes, size_t object_size,
    const unsigned char *jpeg_bytes, size_t jpeg_size,
    const char *source_identity) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *data_section =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *start_symbol =
      (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *end_symbol =
      (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *size_symbol =
      (const ctool_elf32_symbol_t *)0;
  ctool_u32 index;
  int extra_allocated_data = 0;
  int valid = 0;
  if (object_bytes == (const unsigned char *)0 ||
      jpeg_bytes == (const unsigned char *)0 ||
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
  if (ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
      object.file_type != CTOOL_ELF32_ET_REL) {
    goto done;
  }
  for (index = 0u; index < object.section_count; index++) {
    const ctool_elf32_section_t *section = &object.sections[index];
    if (section->name.size == 5u &&
        memcmp(section->name.data, ".data", 5u) == 0) {
      if (data_section != (const ctool_elf32_section_t *)0) {
        goto done;
      }
      data_section = section;
    } else if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
               section->size != 0u) {
      extra_allocated_data = 1;
    }
  }
  for (index = 0u; index < object.symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object.symbols[index];
    if (cupidbuild_jpeg_symbol_name_matches(symbol->name, source_identity,
                                            "_start")) {
      if (start_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      start_symbol = symbol;
    } else if (cupidbuild_jpeg_symbol_name_matches(
                   symbol->name, source_identity, "_end")) {
      if (end_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      end_symbol = symbol;
    } else if (cupidbuild_jpeg_symbol_name_matches(
                   symbol->name, source_identity, "_size")) {
      if (size_symbol != (const ctool_elf32_symbol_t *)0) {
        goto done;
      }
      size_symbol = symbol;
    }
  }
  if (object.symbol_count != 4u ||
      data_section == (const ctool_elf32_section_t *)0 ||
      data_section->type != CTOOL_ELF32_SHT_PROGBITS ||
      data_section->flags !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      data_section->alignment != 1u || data_section->entry_size != 0u ||
      data_section->size != (ctool_u32)jpeg_size ||
      data_section->contents.size != (ctool_u32)jpeg_size ||
      (jpeg_size != 0u &&
       memcmp(data_section->contents.data, jpeg_bytes, jpeg_size) != 0) ||
      extra_allocated_data != 0 || object.relocation_count != 0u ||
      start_symbol == (const ctool_elf32_symbol_t *)0 ||
      end_symbol == (const ctool_elf32_symbol_t *)0 ||
      size_symbol == (const ctool_elf32_symbol_t *)0 ||
      start_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
      start_symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      start_symbol->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      start_symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      start_symbol->section_file_index != data_section->file_index ||
      start_symbol->value != 0u || start_symbol->size != 0u ||
      end_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
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
  ctool_job_close(job);
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

static int cupidbuild_jpeg_failure(char *reason, size_t reason_capacity,
                                   const char *message) {
  if (reason != (char *)0 && reason_capacity != 0u) {
    (void)snprintf(reason, reason_capacity, "%s", message);
  }
  return 0;
}

static int cupidbuild_jpeg_marker_failure(char *reason,
                                          size_t reason_capacity,
                                          const char *prefix,
                                          unsigned char marker,
                                          const char *suffix) {
  if (reason != (char *)0 && reason_capacity != 0u) {
    (void)snprintf(reason, reason_capacity, "%s0x%02x%s", prefix,
                   (unsigned int)marker, suffix);
  }
  return 0;
}

int cupidbuild_validate_jpeg_bytes(const unsigned char *bytes, size_t size,
                                   char *reason, size_t reason_capacity) {
  size_t offset = 2u;
  unsigned char frame_marker = 0u;
  int saw_scan = 0;
  int saw_eoi = 0;
  if (reason == (char *)0 || reason_capacity == 0u) {
    return 0;
  }
  reason[0] = '\0';
  if (bytes == (const unsigned char *)0 || size < 2u || bytes[0] != 0xffu ||
      bytes[1] != 0xd8u) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no SOI marker");
  }
  while (offset < size) {
    size_t segment_size;
    unsigned char marker;
    if (bytes[offset] != 0xffu) {
      return cupidbuild_jpeg_failure(
          reason, reason_capacity,
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
          reason, reason_capacity,
          "JPEG marker stream contains stuffed data before a scan");
    }
    if (marker == 0xd9u) {
      saw_eoi = 1;
      if (offset != size) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG input has trailing bytes after the EOI marker");
      }
      break;
    }
    if (marker == 0x01u || marker == 0xd8u ||
        (marker >= 0xd0u && marker <= 0xd7u)) {
      if (marker != 0x01u) {
        return cupidbuild_jpeg_marker_failure(
            reason, reason_capacity, "unexpected standalone JPEG marker ",
            marker, "");
      }
      continue;
    }
    if (size - offset < 2u) {
      return cupidbuild_jpeg_failure(reason, reason_capacity,
                                     "JPEG marker length is truncated");
    }
    segment_size = ((size_t)bytes[offset] << 8u) |
                   (size_t)bytes[offset + 1u];
    if (segment_size < 2u || segment_size > size - offset) {
      return cupidbuild_jpeg_failure(reason, reason_capacity,
                                     "JPEG marker length is invalid");
    }
    if (cupidbuild_jpeg_frame_marker(marker)) {
      size_t component_count;
      if (frame_marker != 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG input contains more than one frame header");
      }
      if (segment_size < 8u) {
        return cupidbuild_jpeg_failure(reason, reason_capacity,
                                       "JPEG frame header is truncated");
      }
      component_count = (size_t)bytes[offset + 7u];
      if (component_count == 0u ||
          segment_size != 8u + 3u * component_count) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid component table");
      }
      if (bytes[offset + 2u] == 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid sample precision");
      }
      if ((bytes[offset + 3u] == 0u && bytes[offset + 4u] == 0u) ||
          (bytes[offset + 5u] == 0u && bytes[offset + 6u] == 0u)) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG frame header has an invalid image size");
      }
      frame_marker = marker;
    }
    if (marker == 0xdau) {
      size_t scan_components;
      if (frame_marker == 0u) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG scan appears before its frame header");
      }
      if (segment_size < 6u) {
        return cupidbuild_jpeg_failure(reason, reason_capacity,
                                       "JPEG scan header is truncated");
      }
      scan_components = (size_t)bytes[offset + 2u];
      if (scan_components == 0u ||
          segment_size != 6u + 2u * scan_components) {
        return cupidbuild_jpeg_failure(
            reason, reason_capacity,
            "JPEG scan header has an invalid component table");
      }
      saw_scan = 1;
      offset += segment_size;
      while (offset < size) {
        size_t scan_marker_offset;
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
          return cupidbuild_jpeg_failure(
              reason, reason_capacity,
              "JPEG entropy data ends with a partial marker");
        }
        scan_marker = bytes[offset++];
        if (scan_marker == 0x00u ||
            (scan_marker >= 0xd0u && scan_marker <= 0xd7u)) {
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
      return cupidbuild_jpeg_failure(
          reason, reason_capacity,
          "JPEG input has no supported SOF0/SOF1 frame");
    }
    return cupidbuild_jpeg_marker_failure(
        reason, reason_capacity, "unsupported JPEG frame marker ",
        frame_marker, "; check in a baseline SOF0/SOF1 asset");
  }
  if (!saw_scan) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no scan");
  }
  if (!saw_eoi) {
    return cupidbuild_jpeg_failure(reason, reason_capacity,
                                   "JPEG input has no EOI marker");
  }
  return 1;
}

#if defined(_WIN32)
static int cupidbuild_string_equals(ctool_string_t actual,
                                    const char *expected) {
  size_t expected_size = strlen(expected);
  return expected_size <= 4294967295u &&
         actual.size == (ctool_u32)expected_size &&
         memcmp(actual.data, expected, expected_size) == 0;
}
#endif

static int cupidbuild_validate_execution_profile(
                                                  cupidbuild_host_transaction_t *transaction,
                                                  const char *path,
                                                  size_t artifact_index,
                                                 int promoted) {
  unsigned char *bytes;
  size_t size = 0u;
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  int valid = 0;
  bytes = cupidbuild_host_read_frozen_input(
      transaction, path, CUPIDBUILD_TOOL_BYTES, &size);
  if (bytes == (unsigned char *)0 || size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    free(bytes);
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    free(bytes);
    return 0;
  }
  source.path.text = ctool_string("/checked-seed-tool");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
#if defined(_WIN32)
  {
    static const char *const ordinary_imports[] = {
        "CloseHandle",       "CreateFileA",      "ExitProcess",
        "GetCommandLineA",   "GetCurrentDirectoryA",
        "GetLastError",      "GetStdHandle",     "ReadFile",
        "SetFilePointer",    "VirtualAlloc",     "VirtualFree",
        "WriteFile"};
    static const char *const linker_imports[] = {
        "CloseHandle",       "CreateFileA",       "DeleteFileA",
        "ExitProcess",       "FlushFileBuffers",  "GetCommandLineA",
        "GetCurrentDirectoryA", "GetFullPathNameA", "GetLastError",
        "GetStdHandle",      "MoveFileExA",       "ReadFile",
        "SetFilePointer",    "VirtualAlloc",      "VirtualFree",
        "WriteFile"};
    static const char *const cupidbuild_imports[] = {
        "CloseHandle",
        "CreateDirectoryA",
        "CreateFileA",
        "CreateProcessA",
        "DeleteFileA",
        "ExitProcess",
        "FindClose",
        "FindFirstFileA",
        "FindNextFileA",
        "FlushFileBuffers",
        "GetCommandLineA",
        "GetCurrentDirectoryA",
        "GetCurrentProcessId",
        "GetExitCodeProcess",
        "GetFileAttributesA",
        "GetFileInformationByHandle",
        "GetFullPathNameA",
        "GetLastError",
        "GetStdHandle",
        "MoveFileExA",
        "OpenProcess",
        "ReadFile",
        "RemoveDirectoryA",
        "SetFilePointer",
        "TerminateProcess",
        "VirtualAlloc",
        "VirtualFree",
        "WaitForSingleObject",
        "WriteFile"};
    const char *const *expected_imports =
        artifact_index == 5u
            ? cupidbuild_imports
            : ((promoted && artifact_index == 0u) || artifact_index == 3u
                   ? linker_imports
                   : ordinary_imports);
    size_t expected_count =
        artifact_index == 5u
            ? sizeof(cupidbuild_imports) / sizeof(cupidbuild_imports[0])
            : (((promoted && artifact_index == 0u) || artifact_index == 3u)
                   ? sizeof(linker_imports) / sizeof(linker_imports[0])
                   : sizeof(ordinary_imports) / sizeof(ordinary_imports[0]));
    size_t expected_library_count = artifact_index == 5u ? 2u : 1u;
    size_t expected_total_count =
        expected_count + (artifact_index == 5u ? 1u : 0u);
    ctool_pe32_image_t image;
    ctool_u32 index;
    if (ctool_pe32_read(job, &source, &image) == CTOOL_OK &&
        image.entry_point == 0x00401000u &&
        image.import_library_count == (ctool_u32)expected_library_count &&
        image.import_count == (ctool_u32)expected_total_count &&
        cupidbuild_string_equals(image.import_libraries[0].name,
                                 "KERNEL32.dll")) {
      valid = 1;
      if (artifact_index == 5u &&
          !cupidbuild_string_equals(image.import_libraries[1].name,
                                    "NTDLL.dll")) {
        valid = 0;
      }
      for (index = 0u; valid && index < (ctool_u32)expected_count; index++) {
        if (!cupidbuild_string_equals(image.imports[index].library_name,
                                      "KERNEL32.dll") ||
            !cupidbuild_string_equals(image.imports[index].procedure_name,
                                      expected_imports[index])) {
          valid = 0;
          break;
        }
      }
      if (valid && artifact_index == 5u &&
          (!cupidbuild_string_equals(
               image.imports[expected_count].library_name, "NTDLL.dll") ||
           !cupidbuild_string_equals(
               image.imports[expected_count].procedure_name,
               "NtSetInformationFile"))) {
        valid = 0;
      }
    }
  }
#else
  {
    ctool_elf32_object_t object;
    ctool_u32 index;
    ctool_u32 load_count = 0u;
    int entry_in_code = 0;
    (void)artifact_index;
    (void)promoted;
    if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
        object.file_type == CTOOL_ELF32_ET_EXEC &&
        object.entry_point == 0x08048000u &&
        object.program_header_count != 0u) {
      valid = 1;
      for (index = 0u; index < object.program_header_count; index++) {
        const ctool_elf32_program_header_t *header =
            &object.program_headers[index];
        if (header->type == CTOOL_ELF32_PT_DYNAMIC ||
            header->type == CTOOL_ELF32_PT_INTERP) {
          valid = 0;
          break;
        }
        if (header->type != CTOOL_ELF32_PT_LOAD) {
          continue;
        }
        load_count++;
        if ((header->flags & (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) ==
            (CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)) {
          valid = 0;
          break;
        }
        if ((header->flags & CTOOL_ELF32_PF_X) != 0u &&
            object.entry_point >= header->virtual_address &&
            object.entry_point - header->virtual_address < header->file_size) {
          entry_in_code = 1;
        }
      }
      if (load_count == 0u || entry_in_code == 0) {
        valid = 0;
      }
    }
  }
#endif
  ctool_job_close(job);
  free(bytes);
  return valid;
}

static void cupidbuild_seed_capture_close(cupidbuild_seed_capture_t *seed) {
  if (seed != (cupidbuild_seed_capture_t *)0) {
    free(seed->manifest);
    seed->manifest = (unsigned char *)0;
  }
}

static int cupidbuild_seed_manifest_path(
    const char *working_directory, const char *requested, int require_inside,
    char *resolved, size_t capacity) {
  size_t requested_size = strlen(requested);
  int absolute = requested[0] == '/' || requested[0] == '\\' ||
                 (requested_size > 1u && requested[1] == ':');
  if (absolute != 0) {
    if ((require_inside != 0 &&
         (requested_size <= strlen(working_directory) ||
          !cupidbuild_repository_prefix(requested, working_directory))) ||
        requested_size + 1u > capacity) {
      return 0;
    }
    (void)memcpy(resolved, requested, requested_size + 1u);
    return 1;
  }
  return cupidbuild_join(resolved, capacity, working_directory, requested);
}

static int cupidbuild_seed_freeze(
    cupidbuild_host_transaction_t *transaction, const char *working_directory,
    const char *requested_manifest, int require_inside, int require_promoted,
    cupidbuild_seed_capture_t *seed) {
  const char *frozen_manifest = (const char *)0;
  const char *manifest_reason = "manifest path is invalid";
  size_t index;
  (void)memset(seed, 0, sizeof(*seed));
  if (!cupidbuild_seed_manifest_path(
          working_directory, requested_manifest, require_inside,
          seed->manifest_path, sizeof(seed->manifest_path))) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed manifest is outside the working "
                  "directory\n");
    return 0;
  }
  if (!cupidbuild_host_freeze_input(
          transaction, seed->manifest_path, "manifest.json", &frozen_manifest,
          (cupidbuild_host_snapshot_t *)0)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    return 0;
  }
  seed->manifest = cupidbuild_host_read_frozen_input(
      transaction, frozen_manifest, CUPIDBUILD_MANIFEST_BYTES,
      &seed->manifest_size);
  if (seed->manifest == (unsigned char *)0) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked seed manifest is invalid: unreadable bytes\n");
    return 0;
  }
  if (!cupidbuild_json_manifest(seed->manifest, seed->manifest_size,
                                seed->artifacts, &seed->artifact_count,
                                &manifest_reason)) {
    (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: %s\n",
                  manifest_reason);
    return 0;
  }
  if (require_promoted != 0 &&
      seed->artifact_count != CUPIDBUILD_SEED_ARTIFACTS) {
    (void)fprintf(stderr,
                  "cupidbuild: checked tool runner requires a promoted "
                  "six-tool seed\n");
    return 0;
  }
  if (!cupidbuild_manifest_directory(seed->manifest_path, seed->directory,
                                     sizeof(seed->directory))) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed manifest is invalid: artifact "
                  "path is invalid\n");
    return 0;
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    seed->expected_files[index] = seed->artifacts[index].file;
  }
  if (!cupidbuild_host_seed_members_exact(
          seed->directory,
#if defined(_WIN32)
          ".exe",
#else
          ".elf",
#endif
          seed->expected_files, seed->artifact_count)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed directory contains an unlisted "
                  "executable file\n");
    return 0;
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    if (!cupidbuild_join(live_path, sizeof(live_path), seed->directory,
                         seed->artifacts[index].file) ||
        !cupidbuild_host_freeze_input(
            transaction, live_path, seed->artifacts[index].file,
            &seed->frozen_tools[index], &seed->tool_snapshots[index]) ||
        !cupidbuild_host_make_input_executable(
            transaction, seed->frozen_tools[index]) ||
        !cupidbuild_artifact_matches(&seed->tool_snapshots[index],
                                     &seed->artifacts[index])) {
      (void)fprintf(stderr, "cupidbuild: checked %s digest mismatch\n",
                    cupidbuild_seed_names[index]);
      return 0;
    }
  }
  for (index = 0u; index < seed->artifact_count; index++) {
    if (!cupidbuild_validate_execution_profile(
            transaction, seed->frozen_tools[index], index,
            seed->artifact_count == CUPIDBUILD_SEED_ARTIFACTS)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked seed execution profile mismatch\n");
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_seed_require_live(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_seed_capture_t *seed) {
  if (!cupidbuild_host_seed_members_exact(
          seed->directory,
#if defined(_WIN32)
          ".exe",
#else
          ".elf",
#endif
          seed->expected_files, seed->artifact_count)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked seed directory membership changed "
                  "while checked tools ran\n");
    return 0;
  }
  if (!cupidbuild_host_require_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    return 0;
  }
  return 1;
}

typedef enum {
  CUPIDBUILD_ASSEMBLY_OBJECT,
  CUPIDBUILD_ASSEMBLY_BOOTLOADER,
  CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE
} cupidbuild_assembly_kind_t;

static const unsigned char cupidbuild_smp_trampoline_map[] =
    "cupid.raw-map.v2\n"
    "size 4096\n"
    "base 0x00008000\n"
    "edges 6\n"
    "range 0x00000000 code16\n"
    "range 0x0000001f data\n"
    "range 0x00000210 code32\n"
    "range 0x00000254 data\n"
    "edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    "edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    "edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    "edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    "edge 0x00000250 indirect unprovable - - unknown -\n"
    "edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n";

static int cupidbuild_assemble(
    const cupidbuild_assembly_request_t *request,
    cupidbuild_assembly_kind_t kind) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  const char *frozen_assembler = (const char *)0;
  const char *frozen_inspector = (const char *)0;
  cupidbuild_host_snapshot_t candidate_snapshot;
  cupidbuild_host_snapshot_t map_snapshot;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *map = (unsigned char *)0;
  const char *assembler_arguments[8];
  const char *inspector_arguments[9];
  int assembler_status;
  int inspector_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_assembly_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid guarded assembly request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 0, &seed)) {
    goto done;
  }
  frozen_assembler = seed.frozen_tools[0];
  frozen_inspector = seed.frozen_tools[2];
  assembler_arguments[0] = "-f";
  assembler_arguments[1] =
      kind == CUPIDBUILD_ASSEMBLY_OBJECT ? "elf32" : "bin";
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    assembler_arguments[2] = "-o";
    assembler_arguments[3] = cupidbuild_host_candidate(transaction);
    assembler_arguments[4] = cupidbuild_host_frozen_source(transaction);
    assembler_arguments[5] = (const char *)0;
  } else {
    assembler_arguments[2] = "--map";
    assembler_arguments[3] = cupidbuild_host_private_output(transaction);
    assembler_arguments[4] = "-o";
    assembler_arguments[5] = cupidbuild_host_candidate(transaction);
    assembler_arguments[6] = cupidbuild_host_frozen_source(transaction);
    assembler_arguments[7] = (const char *)0;
  }
  assembler_status = cupidbuild_host_run(
      transaction, frozen_assembler, assembler_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (assembler_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    if (!cupidbuild_validate_relocatable(candidate, candidate_snapshot.size,
                                         1)) {
      (void)fprintf(
          stderr,
          "cupidbuild: checked CupidASM relocatable object validation failed\n");
      goto done;
    }
  } else if ((kind == CUPIDBUILD_ASSEMBLY_BOOTLOADER &&
              candidate_snapshot.size != 2560u) ||
             (kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE &&
              candidate_snapshot.size != 4096u)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidASM raw output validation failed\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (kind != CUPIDBUILD_ASSEMBLY_OBJECT) {
    if (!cupidbuild_host_capture_private_output(transaction, &map_snapshot,
                                                &map) ||
        map_snapshot.size == 0u) {
      (void)fprintf(stderr,
                    "cupidbuild: checked CupidASM range map is missing or empty\n");
      goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE &&
        (map_snapshot.size != sizeof(cupidbuild_smp_trampoline_map) - 1u ||
         memcmp(map, cupidbuild_smp_trampoline_map,
                sizeof(cupidbuild_smp_trampoline_map) - 1u) != 0)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked CupidASM range map does not match "
                    "the SMP layout policy\n");
      goto done;
    }
    free(map);
    map = (unsigned char *)0;
    if (!cupidbuild_host_require_private_output(transaction, &map_snapshot)) {
      (void)fprintf(stderr, "cupidbuild: %s\n",
                    cupidbuild_host_error(transaction));
      goto done;
    }
  }
  if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
    inspector_arguments[0] = "--require-known";
    inspector_arguments[1] = "--require-local-targets";
    inspector_arguments[2] = "--require-code-anchors";
    inspector_arguments[3] = cupidbuild_host_candidate(transaction);
    inspector_arguments[4] = (const char *)0;
  } else {
    inspector_arguments[0] = "--raw";
    inspector_arguments[1] = "--range-map";
    inspector_arguments[2] = cupidbuild_host_private_output(transaction);
    inspector_arguments[3] = "--require-known";
    inspector_arguments[4] = "--require-local-targets";
    inspector_arguments[5] = "--require-source-edges";
    inspector_arguments[6] = cupidbuild_host_candidate(transaction);
    inspector_arguments[7] = (const char *)0;
  }
  inspector_status = cupidbuild_host_run(
      transaction, frozen_inspector, inspector_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      (kind != CUPIDBUILD_ASSEMBLY_OBJECT &&
       !cupidbuild_host_require_private_output(transaction, &map_snapshot))) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (inspector_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(map);
  free(candidate);
  cupidbuild_seed_capture_close(&seed);
  cupidbuild_host_transaction_close(transaction);
  return result;
}

int cupidbuild_assemble_object(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_OBJECT);
}

int cupidbuild_assemble_bootloader(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_BOOTLOADER);
}

int cupidbuild_assemble_smp_trampoline(
    const cupidbuild_assembly_request_t *request) {
  return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE);
}

int cupidbuild_embed_jpeg(const cupidbuild_jpeg_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *source = (unsigned char *)0;
  size_t source_size = 0u;
  char jpeg_reason[160];
  const char *object_arguments[7];
  int object_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_jpeg_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid JPEG embed request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  object_arguments[0] = "wrap-jpeg";
  object_arguments[1] = cupidbuild_host_frozen_source(transaction);
  object_arguments[2] = "--identity";
  object_arguments[3] = request->source;
  object_arguments[4] = "-o";
  object_arguments[5] = cupidbuild_host_candidate(transaction);
  object_arguments[6] = (const char *)0;
  object_status = cupidbuild_host_run(transaction, seed.frozen_tools[4],
                                      object_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (object_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  source = cupidbuild_host_read_frozen_input(
      transaction, cupidbuild_host_frozen_source(transaction),
      CUPIDBUILD_TOOL_BYTES, &source_size);
  if (source == (unsigned char *)0) {
    (void)fprintf(stderr,
                  "cupidbuild: frozen JPEG input cannot be inspected\n");
    goto done;
  }
  if (!cupidbuild_validate_jpeg_object_bytes(
          candidate, candidate_snapshot.size, source, source_size,
          request->source)) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked CupidObj JPEG object validation failed\n");
    goto done;
  }
  if (!cupidbuild_validate_jpeg_bytes(source, source_size, jpeg_reason,
                                      sizeof(jpeg_reason))) {
    (void)fprintf(stderr,
                  "cupidbuild: independent JPEG validation failed: %s\n",
                  jpeg_reason);
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  free(source);
  source = (unsigned char *)0;
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(source);
  free(candidate);
  cupidbuild_seed_capture_close(&seed);
  cupidbuild_host_transaction_close(transaction);
  return result;
}

static int cupidbuild_ksyms_space(unsigned char character) {
  return character == (unsigned char)' ' ||
         character == (unsigned char)'\t' ||
         character == (unsigned char)'\r' ||
         character == (unsigned char)'\v' ||
         character == (unsigned char)'\f';
}

static cupidbuild_ksyms_row_kind_t cupidbuild_ksyms_address(
    const unsigned char *bytes, size_t size, unsigned int *address_out) {
  unsigned int value = 0u;
  size_t index = 0u;
  if (size >= 2u && bytes[0] == (unsigned char)'0' &&
      (bytes[1] == (unsigned char)'x' || bytes[1] == (unsigned char)'X')) {
    index = 2u;
  }
  if (index == size) {
    return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
  }
  while (index < size) {
    unsigned char character = bytes[index];
    unsigned int digit;
    if (character >= (unsigned char)'0' &&
        character <= (unsigned char)'9') {
      digit = (unsigned int)(character - (unsigned char)'0');
    } else if (character >= (unsigned char)'a' &&
               character <= (unsigned char)'f') {
      digit = 10u + (unsigned int)(character - (unsigned char)'a');
    } else if (character >= (unsigned char)'A' &&
               character <= (unsigned char)'F') {
      digit = 10u + (unsigned int)(character - (unsigned char)'A');
    } else {
      return CUPIDBUILD_KSYMS_INVALID_ADDRESS;
    }
    if (value > (0xffffffffu - digit) / 16u) {
      return CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386;
    }
    value = value * 16u + digit;
    index++;
  }
  *address_out = value;
  return CUPIDBUILD_KSYMS_TEXT;
}

static cupidbuild_ksyms_row_kind_t cupidbuild_ksyms_parse_row(
    const unsigned char *bytes, size_t start, size_t end, size_t order,
    cupidbuild_ksyms_symbol_t *symbol_out) {
  size_t field_start[3];
  size_t field_size[3];
  size_t field_count = 0u;
  size_t index = start;
  unsigned int address = 0u;
  cupidbuild_ksyms_row_kind_t address_kind;
  while (index < end) {
    size_t token_start;
    while (index < end && cupidbuild_ksyms_space(bytes[index])) {
      index++;
    }
    if (index == end) {
      break;
    }
    if (field_count == 3u) {
      return CUPIDBUILD_KSYMS_MALFORMED;
    }
    token_start = index;
    while (index < end && !cupidbuild_ksyms_space(bytes[index])) {
      if (bytes[index] == 0u) {
        return CUPIDBUILD_KSYMS_MALFORMED;
      }
      index++;
    }
    field_start[field_count] = token_start;
    field_size[field_count] = index - token_start;
    field_count++;
  }
  if (field_count == 0u) {
    return CUPIDBUILD_KSYMS_EMPTY;
  }
  if (field_count == 2u) {
    if (field_size[0] == 1u &&
        (bytes[field_start[0]] == (unsigned char)'U' ||
         bytes[field_start[0]] == (unsigned char)'u' ||
         bytes[field_start[0]] == (unsigned char)'v' ||
         bytes[field_start[0]] == (unsigned char)'w')) {
      return CUPIDBUILD_KSYMS_IGNORED;
    }
    return CUPIDBUILD_KSYMS_OMITTED_ADDRESS;
  }
  if (field_count != 3u || field_size[1] != 1u) {
    return CUPIDBUILD_KSYMS_MALFORMED;
  }
  address_kind = cupidbuild_ksyms_address(
      bytes + field_start[0], field_size[0], &address);
  if (address_kind != CUPIDBUILD_KSYMS_TEXT) {
    return address_kind;
  }
  if (!(bytes[field_start[1]] == (unsigned char)'t' ||
        bytes[field_start[1]] == (unsigned char)'T' ||
        bytes[field_start[1]] == (unsigned char)'w' ||
        bytes[field_start[1]] == (unsigned char)'W')) {
    return CUPIDBUILD_KSYMS_IGNORED;
  }
  if (field_size[2] >= 2u && bytes[field_start[2]] == (unsigned char)'.' &&
      bytes[field_start[2] + 1u] == (unsigned char)'L') {
    return CUPIDBUILD_KSYMS_IGNORED;
  }
  symbol_out->address = address;
  symbol_out->order = order;
  symbol_out->name_start = field_start[2];
  symbol_out->name_size = field_size[2];
  return CUPIDBUILD_KSYMS_TEXT;
}

static int cupidbuild_ksyms_symbol_less(
    const cupidbuild_ksyms_symbol_t *left,
    const cupidbuild_ksyms_symbol_t *right) {
  return left->address < right->address ||
         (left->address == right->address && left->order < right->order);
}

static void cupidbuild_ksyms_symbol_swap(
    cupidbuild_ksyms_symbol_t *left, cupidbuild_ksyms_symbol_t *right) {
  cupidbuild_ksyms_symbol_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void cupidbuild_ksyms_symbol_sift_down(
    cupidbuild_ksyms_symbol_t *symbols, size_t root, size_t count) {
  for (;;) {
    size_t child;
    size_t selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (cupidbuild_ksyms_symbol_less(&symbols[selected], &symbols[child])) {
      selected = child;
    }
    if (child + 1u < count &&
        cupidbuild_ksyms_symbol_less(&symbols[selected],
                                     &symbols[child + 1u])) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    cupidbuild_ksyms_symbol_swap(&symbols[root], &symbols[selected]);
    root = selected;
  }
}

static void cupidbuild_ksyms_symbol_sort(
    cupidbuild_ksyms_symbol_t *symbols, size_t count) {
  size_t start = count / 2u;
  size_t end = count;
  while (start != 0u) {
    start--;
    cupidbuild_ksyms_symbol_sift_down(symbols, start, count);
  }
  while (end > 1u) {
    cupidbuild_ksyms_symbol_swap(&symbols[0], &symbols[end - 1u]);
    end--;
    cupidbuild_ksyms_symbol_sift_down(symbols, 0u, end);
  }
}

static void cupidbuild_ksyms_write_le32(unsigned char *bytes, size_t offset,
                                        unsigned int value) {
  bytes[offset] = (unsigned char)(value & 0xffu);
  bytes[offset + 1u] = (unsigned char)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (unsigned char)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (unsigned char)((value >> 24u) & 0xffu);
}

static int cupidbuild_ksyms_append(cupidbuild_ksyms_buffer_t *buffer,
                                   const void *bytes, size_t size) {
  size_t required;
  size_t capacity;
  unsigned char *grown;
  if (size > CUPIDBUILD_TOOL_BYTES ||
      buffer->size > CUPIDBUILD_TOOL_BYTES - size) {
    return 0;
  }
  required = buffer->size + size;
  if (required > buffer->capacity) {
    capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
    while (capacity < required) {
      if (capacity > CUPIDBUILD_TOOL_BYTES / 2u) {
        capacity = CUPIDBUILD_TOOL_BYTES;
        break;
      }
      capacity *= 2u;
    }
    grown = (unsigned char *)realloc(buffer->bytes, capacity);
    if (grown == (unsigned char *)0) {
      return 0;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
  }
  if (size != 0u) {
    (void)memcpy(buffer->bytes + buffer->size, bytes, size);
  }
  buffer->size = required;
  return 1;
}

static int cupidbuild_ksyms_append_literal(
    cupidbuild_ksyms_buffer_t *buffer, const char *text) {
  return cupidbuild_ksyms_append(buffer, text, strlen(text));
}

static int cupidbuild_ksyms_append_word(cupidbuild_ksyms_buffer_t *buffer,
                                        unsigned int value) {
  static const char digits[] = "0123456789abcdef";
  char text[12];
  size_t index;
  text[0] = '0';
  text[1] = 'x';
  for (index = 0u; index < 8u; index++) {
    size_t shift = (7u - index) * 4u;
    text[2u + index] = digits[(value >> shift) & 0x0fu];
  }
  text[10] = 'u';
  text[11] = ',';
  return cupidbuild_ksyms_append(buffer, text, sizeof(text));
}

static int cupidbuild_ksyms_append_decimal(
    cupidbuild_ksyms_buffer_t *buffer, unsigned int value) {
  char reverse[10];
  char text[10];
  size_t count = 0u;
  size_t index;
  do {
    reverse[count++] = (char)('0' + (char)(value % 10u));
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    text[index] = reverse[count - index - 1u];
  }
  return cupidbuild_ksyms_append(buffer, text, count);
}

static const char *cupidbuild_ksyms_row_reason(
    cupidbuild_ksyms_row_kind_t kind) {
  if (kind == CUPIDBUILD_KSYMS_OMITTED_ADDRESS) {
    return "symbol reader omitted an address";
  }
  if (kind == CUPIDBUILD_KSYMS_INVALID_ADDRESS) {
    return "symbol reader emitted an invalid address";
  }
  if (kind == CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386) {
    return "symbol reader address is outside i386";
  }
  return "symbol reader emitted a malformed row";
}

static int cupidbuild_render_ksyms_source(
    const unsigned char *contents, size_t contents_size,
    unsigned char **source_out, size_t *source_size_out,
    const char **reason_out, size_t *line_out) {
  cupidbuild_ksyms_symbol_t *symbols =
      (cupidbuild_ksyms_symbol_t *)0;
  unsigned char *blob = (unsigned char *)0;
  cupidbuild_ksyms_buffer_t output;
  size_t symbol_count = 0u;
  size_t unique_count = 0u;
  size_t offset = 0u;
  size_t line = 1u;
  size_t index;
  size_t string_offset;
  size_t blob_size;
  size_t padded_size;
  size_t string_cursor;
  int success = 0;
  (void)memset(&output, 0, sizeof(output));
  *source_out = (unsigned char *)0;
  *source_size_out = 0u;
  *reason_out = "kernel symbol source validation failed";
  *line_out = 0u;
  while (offset < contents_size) {
    size_t start = offset;
    cupidbuild_ksyms_symbol_t symbol;
    cupidbuild_ksyms_row_kind_t kind;
    while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
      offset++;
    }
    kind = cupidbuild_ksyms_parse_row(contents, start, offset, symbol_count,
                                      &symbol);
    if (kind == CUPIDBUILD_KSYMS_TEXT) {
      symbol_count++;
    } else if (kind != CUPIDBUILD_KSYMS_EMPTY &&
               kind != CUPIDBUILD_KSYMS_IGNORED) {
      *reason_out = cupidbuild_ksyms_row_reason(kind);
      *line_out = line;
      goto done;
    }
    if (offset < contents_size) {
      offset++;
      line++;
    }
  }
  if (symbol_count == 0u) {
    *reason_out = "symbol reader reported no kernel text symbols";
    goto done;
  }
  if (symbol_count > (size_t)-1 / sizeof(*symbols)) {
    *reason_out = "kernel symbol inventory exceeds the host size limit";
    goto done;
  }
  symbols = (cupidbuild_ksyms_symbol_t *)malloc(
      symbol_count * sizeof(*symbols));
  if (symbols == (cupidbuild_ksyms_symbol_t *)0) {
    *reason_out = "kernel symbol inventory cannot be allocated";
    goto done;
  }
  offset = 0u;
  index = 0u;
  while (offset < contents_size) {
    size_t start = offset;
    cupidbuild_ksyms_symbol_t symbol;
    cupidbuild_ksyms_row_kind_t kind;
    while (offset < contents_size && contents[offset] != (unsigned char)'\n') {
      offset++;
    }
    kind = cupidbuild_ksyms_parse_row(contents, start, offset, index, &symbol);
    if (kind == CUPIDBUILD_KSYMS_TEXT) {
      symbols[index++] = symbol;
    }
    if (offset < contents_size) {
      offset++;
    }
  }
  cupidbuild_ksyms_symbol_sort(symbols, symbol_count);
  for (index = 0u; index < symbol_count; index++) {
    if (unique_count == 0u ||
        symbols[index].address != symbols[unique_count - 1u].address) {
      symbols[unique_count++] = symbols[index];
    }
  }
  if (unique_count > (0xffffffffu - 16u) / 8u) {
    *reason_out = "kernel symbol table size overflows i386";
    goto done;
  }
  string_offset = 16u + unique_count * 8u;
  blob_size = string_offset;
  for (index = 0u; index < unique_count; index++) {
    if (symbols[index].name_size >= 0xffffffffu ||
        blob_size > 0xffffffffu - symbols[index].name_size - 1u) {
      *reason_out = "kernel symbol strings overflow i386";
      goto done;
    }
    blob_size += symbols[index].name_size + 1u;
  }
  if (blob_size > 0xfffffffcu) {
    *reason_out = "word-packed kernel symbol source overflows i386";
    goto done;
  }
  padded_size = (blob_size + 3u) & ~(size_t)3u;
  if (padded_size > CUPIDBUILD_TOOL_BYTES) {
    *reason_out = "kernel symbol blob exceeds the validation limit";
    goto done;
  }
  blob = (unsigned char *)calloc(padded_size, 1u);
  if (blob == (unsigned char *)0) {
    *reason_out = "kernel symbol blob cannot be allocated";
    goto done;
  }
  cupidbuild_ksyms_write_le32(blob, 0u, 0x4d59534bu);
  cupidbuild_ksyms_write_le32(blob, 4u, (unsigned int)unique_count);
  cupidbuild_ksyms_write_le32(blob, 8u, (unsigned int)string_offset);
  cupidbuild_ksyms_write_le32(blob, 12u, (unsigned int)blob_size);
  string_cursor = string_offset;
  for (index = 0u; index < unique_count; index++) {
    cupidbuild_ksyms_write_le32(blob, 16u + index * 8u,
                                symbols[index].address);
    cupidbuild_ksyms_write_le32(
        blob, 20u + index * 8u,
        (unsigned int)(string_cursor - string_offset));
    (void)memcpy(blob + string_cursor,
                 contents + symbols[index].name_start,
                 symbols[index].name_size);
    string_cursor += symbols[index].name_size + 1u;
  }
  if (!cupidbuild_ksyms_append_literal(
          &output,
          "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
          "#include \"ksyms.h\"\n\n"
          "/* i386 words preserve the blob bytes with fewer initializers. */\n"
          "const unsigned int\n"
          "__attribute__((section(\".ksyms\"), used, aligned(4)))\n"
          "ksym_blob[] = {\n")) {
    *reason_out = "kernel symbol source exceeds the validation limit";
    goto done;
  }
  for (offset = 0u; offset < padded_size; offset += 4u) {
    unsigned int value = (unsigned int)blob[offset] |
                         ((unsigned int)blob[offset + 1u] << 8u) |
                         ((unsigned int)blob[offset + 2u] << 16u) |
                         ((unsigned int)blob[offset + 3u] << 24u);
    size_t word_index = offset / 4u;
    if (!cupidbuild_ksyms_append_literal(
            &output, word_index % 8u == 0u ? "  " : " ") ||
        !cupidbuild_ksyms_append_word(&output, value) ||
        ((word_index % 8u == 7u || offset + 4u == padded_size) &&
         !cupidbuild_ksyms_append_literal(&output, "\n"))) {
      *reason_out = "kernel symbol source exceeds the validation limit";
      goto done;
    }
  }
  if (!cupidbuild_ksyms_append_literal(
          &output, "};\n\nconst unsigned int ksym_blob_size = ") ||
      !cupidbuild_ksyms_append_decimal(&output, (unsigned int)blob_size) ||
      !cupidbuild_ksyms_append_literal(&output, "u;\n")) {
    *reason_out = "kernel symbol source exceeds the validation limit";
    goto done;
  }
  *source_out = output.bytes;
  *source_size_out = output.size;
  output.bytes = (unsigned char *)0;
  success = 1;

done:
  free(output.bytes);
  free(blob);
  free(symbols);
  return success;
}

int cupidbuild_generate_ksyms(const cupidbuild_ksyms_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  cupidbuild_host_snapshot_t symbols_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *symbols = (unsigned char *)0;
  unsigned char *candidate = (unsigned char *)0;
  unsigned char *expected = (unsigned char *)0;
  size_t expected_size = 0u;
  const char *validation_reason = (const char *)0;
  size_t validation_line = 0u;
  const char *disassembler_arguments[3];
  const char *object_arguments[5];
  int disassembler_status;
  int object_status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_ksyms_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr,
                  "cupidbuild: invalid kernel symbol generation request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->repository_root,
                              request->seed_manifest, 1, 1, &seed)) {
    goto done;
  }
  disassembler_arguments[0] = "-n";
  disassembler_arguments[1] = cupidbuild_host_frozen_source(transaction);
  disassembler_arguments[2] = (const char *)0;
  disassembler_status = cupidbuild_host_run_to_private_output(
      transaction, seed.frozen_tools[2], disassembler_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (disassembler_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_private_output(
          transaction, &symbols_snapshot, &symbols)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_render_ksyms_source(
          symbols, symbols_snapshot.size, &expected, &expected_size,
          &validation_reason, &validation_line)) {
    if (validation_line != 0u) {
      (void)fprintf(stderr,
                    "cupidbuild: independent kernel symbol validation "
                    "failed at line %u: %s\n",
                    (unsigned int)validation_line, validation_reason);
    } else {
      (void)fprintf(stderr,
                    "cupidbuild: independent kernel symbol validation "
                    "failed: %s\n",
                    validation_reason);
    }
    goto done;
  }
  free(symbols);
  symbols = (unsigned char *)0;
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  object_arguments[0] = "ksyms-source";
  object_arguments[1] = cupidbuild_host_private_output(transaction);
  object_arguments[2] = "-o";
  object_arguments[3] = cupidbuild_host_candidate(transaction);
  object_arguments[4] = (const char *)0;
  object_status = cupidbuild_host_run(transaction, seed.frozen_tools[4],
                                      object_arguments, 60000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (object_status != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (candidate_snapshot.size != expected_size ||
      memcmp(candidate, expected, expected_size) != 0) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidObj kernel symbol source differs "
                  "from the independent renderer\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  free(expected);
  expected = (unsigned char *)0;
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_private_output(transaction,
                                               &symbols_snapshot) ||
      !cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(expected);
  free(candidate);
  free(symbols);
  cupidbuild_seed_capture_close(&seed);
  cupidbuild_host_transaction_close(transaction);
  return result;
}

int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  cupidbuild_seed_capture_t seed;
  size_t tool_index;
  int status;
  int result = 1;
  (void)memset(&seed, 0, sizeof(seed));
  if (request == (const cupidbuild_run_request_t *)0 ||
      !cupidbuild_path_safe(request->working_directory, 0) ||
      !cupidbuild_path_safe(request->seed_manifest, 0) ||
      request->arguments == (const char *const *)0 ||
      request->tool == (const char *)0 ||
      (strcmp(request->tool, "cupidobj") != 0 &&
       strcmp(request->tool, "cupidld") != 0) ||
      request->timeout_seconds == 0u || request->timeout_seconds > 86400u) {
    (void)fprintf(stderr, "cupidbuild: invalid checked tool request\n");
    return 1;
  }
  tool_index = strcmp(request->tool, "cupidld") == 0 ? 3u : 4u;
  if (!cupidbuild_host_runner_open(request->working_directory,
                                    &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_seed_freeze(transaction, request->working_directory,
                              request->seed_manifest, 0, 1, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  status = cupidbuild_host_run_captured(
      transaction, seed.frozen_tools[tool_index], request->arguments,
      request->timeout_seconds * 1000u);
  if (!cupidbuild_seed_require_live(transaction, &seed)) {
    goto done;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (status == -2) {
    (void)fprintf(stderr, "cupidbuild: checked %s timed out\n",
                  cupidbuild_seed_names[tool_index]);
    goto done;
  }
  if (status < 0) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_forward_captured(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = status;

done:
  cupidbuild_seed_capture_close(&seed);
  if (!cupidbuild_host_transaction_close(transaction)) {
    (void)fprintf(stderr,
                  "cupidbuild: private checked-tool cleanup failed\n");
    if (result == 0) {
      result = 1;
    }
  }
  return result;
}
