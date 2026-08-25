#include "cupidbuild.h"
#include "ctool.h"
#include "ctool_host.h"
#include "cupidbuild_host.h"
#include "elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_PATH_BYTES 8192u
#define CUPIDBUILD_MANIFEST_BYTES 1048576u
#define CUPIDBUILD_JSON_TOKENS 2048u
typedef struct {
  char file[CUPIDBUILD_PATH_BYTES];
  char sha256[65];
  size_t size;
} cupidbuild_seed_artifact_t;

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

static unsigned char *cupidbuild_read_private(const char *path, size_t limit,
                                              size_t *size_out) {
  FILE *stream;
  long length;
  unsigned char *bytes;
#if defined(_WIN32)
  stream = (FILE *)0;
  if (fopen_s(&stream, path, "rb") != 0) {
    stream = (FILE *)0;
  }
#else
  stream = fopen(path, "rb");
#endif
  if (stream == (FILE *)0 || fseek(stream, 0L, SEEK_END) != 0) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return (unsigned char *)0;
  }
  length = ftell(stream);
  if (length < 0L || (unsigned long)length > (unsigned long)limit ||
      fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    return (unsigned char *)0;
  }
  bytes = (unsigned char *)malloc((size_t)length + 1u);
  if (bytes == (unsigned char *)0 ||
      ((size_t)length != 0u &&
       fread(bytes, 1u, (size_t)length, stream) != (size_t)length) ||
      fclose(stream) != 0) {
    free(bytes);
    return (unsigned char *)0;
  }
  bytes[(size_t)length] = 0u;
  *size_out = (size_t)length;
  return bytes;
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

static int cupidbuild_json_u64(const unsigned char *bytes,
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

static int cupidbuild_json_number_field(const unsigned char *bytes,
                                        const cupidbuild_json_token_t *tokens,
                                        size_t count, size_t object,
                                        const char *name, size_t expected) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t actual;
  return value < count && cupidbuild_json_u64(bytes, &tokens[value], &actual) &&
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
                                      int windows) {
  static const char revision[] = "a17c9465911da41d59b7ada71733d36c39faa5ea";
  static const char snapshot[] =
      "46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67";
  static const char *const linux_names[] = {
      "fixed_point_command",   "fixed_point_result", "producer_lineage",
      "seed_generation",       "source_input_count", "source_revision",
      "source_snapshot_sha256"};
  static const char *const windows_names[] = {
      "artifact_generation",         "fixed_point_command",
      "fixed_point_result",          "parent_seed_manifest_sha256",
      "parent_seed_source_revision", "producer_lineage",
      "source_input_count",          "source_revision",
      "source_snapshot_sha256"};
  size_t lineage;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object,
                             windows ? windows_names : linux_names,
                             windows ? 9u : 7u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "fixed_point_result", "pass") ||
      !cupidbuild_json_number_field(bytes, tokens, count, object,
                                    "source_input_count", 50u) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "source_revision", revision) ||
      !cupidbuild_json_string_field(bytes, tokens, count, object,
                                    "source_snapshot_sha256", snapshot)) {
    return 0;
  }
  lineage = cupidbuild_json_required(bytes, tokens, count, object,
                                     "producer_lineage");
  if (!cupidbuild_json_lineage(bytes, tokens, count, lineage, windows)) {
    return 0;
  }
  if (windows) {
    return cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "artifact_generation",
                                        "paired-stage-four-native-windows") &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "fixed_point_command",
                                        "make bootstrap-windows-from-seed") &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "parent_seed_manifest_sha256",
                                        "b6e34a2e18dd18aba91c6358116eafde399535"
                                        "66efeadb224575ac8c13ab2c1b") &&
           cupidbuild_json_string_field(bytes, tokens, count, object,
                                        "parent_seed_source_revision",
                                        revision);
  }
  return cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "fixed_point_command",
                                      "make bootstrap-from-seed") &&
         cupidbuild_json_string_field(bytes, tokens, count, object,
                                      "seed_generation", "stage-four");
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
                                   size_t count, size_t array) {
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
      {"cupidc_main", "/toolchain/cupidc_main.cc", 0}};
  size_t cursor;
  size_t index;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != sizeof(expected) / sizeof(expected[0])) {
    return 0;
  }
  cursor = array + 1u;
  for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
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
                                 size_t count, size_t object) {
  static const char *const names[] = {"cupidasm", "cupiddis", "cupidld",
                                      "cupidobj", "cupidc"};
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
  size_t value;
  if (object >= count ||
      !cupidbuild_json_exact(bytes, tokens, count, object, names, 5u)) {
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
  return cupidbuild_json_string_array(bytes, tokens, count, value, cupidc, 12u);
}

static int cupidbuild_json_build_plan(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object) {
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
  if (!cupidbuild_json_sources(bytes, tokens, count, value)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "links");
  return cupidbuild_json_links(bytes, tokens, count, value);
}
#endif

static int cupidbuild_json_artifacts(const unsigned char *bytes,
                                     const cupidbuild_json_token_t *tokens,
                                     size_t count, size_t array, int windows,
                                     cupidbuild_seed_artifact_t artifacts[5]) {
  static const char *const fields[] = {"file", "name", "producer", "sha256",
                                       "size"};
  static const char *const names[] = {"cupidasm", "cupidc", "cupiddis",
                                      "cupidld", "cupidobj"};
  static const int producers[] = {1, 1, 0, 1, 0};
  int seen[5] = {0, 0, 0, 0, 0};
  size_t cursor;
  size_t item;
  if (array >= count || tokens[array].type != CUPIDBUILD_JSON_ARRAY ||
      tokens[array].count != 5u) {
    return 0;
  }
  cursor = array + 1u;
  for (item = 0u; item < 5u; item++) {
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
    for (index = 0u; index < 5u; index++) {
      if (name_token < count &&
          cupidbuild_json_text(bytes, &tokens[name_token], names[index])) {
        break;
      }
    }
    if (index == 5u || seen[index] != 0) {
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
        !cupidbuild_json_u64(bytes, &tokens[size_token], &actual_size) ||
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
  return 1;
}

static int cupidbuild_json_manifest(const unsigned char *manifest,
                                    size_t manifest_size,
                                    cupidbuild_seed_artifact_t artifacts[5],
                                    const char **reason_out) {
  cupidbuild_json_token_t *tokens;
  size_t count = 0u;
  size_t artifacts_token;
  size_t provenance;
  size_t schema;
  size_t target;
  int windows;
#if defined(_WIN32)
  static const char expected_schema[] = "cupid.execution-seed.v1";
  static const char *const top_names[] = {"artifacts", "provenance", "schema",
                                          "target"};
  windows = 1;
#else
  static const char expected_schema[] = "cupid.bootstrap-seed.v1";
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
  if (schema >= count || tokens[schema].type != CUPIDBUILD_JSON_STRING ||
      !cupidbuild_json_text(manifest, &tokens[schema], expected_schema)) {
    *reason_out = "schema differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_artifacts(manifest, tokens, count, artifacts_token,
                                 windows, artifacts)) {
    *reason_out = "artifact inventory differs";
    free(tokens);
    return 0;
  }
  if (!cupidbuild_json_provenance(manifest, tokens, count, provenance,
                                  windows)) {
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
    if (!cupidbuild_json_string_field(manifest, tokens, count, 0u,
                                      "build_plan_sha256",
                                      "59c1231e6fc7caafde8781dd6a566fa0ece2909b"
                                      "e606914f24a19a7bececadcc") ||
        !cupidbuild_json_build_plan(manifest, tokens, count, plan)) {
      *reason_out = "build plan differs";
      free(tokens);
      return 0;
    }
  }
#endif
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

static int cupidbuild_validate_object(const unsigned char *bytes, size_t size) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 index;
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
  return executable;
}

int cupidbuild_assemble_object(const cupidbuild_object_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  char manifest_path[CUPIDBUILD_PATH_BYTES];
  char manifest_directory[CUPIDBUILD_PATH_BYTES];
  char assembler_path[CUPIDBUILD_PATH_BYTES];
  char inspector_path[CUPIDBUILD_PATH_BYTES];
  const char *frozen_manifest = (const char *)0;
  const char *frozen_assembler = (const char *)0;
  const char *frozen_inspector = (const char *)0;
  cupidbuild_seed_artifact_t artifacts[5];
  cupidbuild_seed_artifact_t *assembler_artifact = &artifacts[0];
  cupidbuild_seed_artifact_t *compiler_artifact = &artifacts[1];
  cupidbuild_seed_artifact_t *inspector_artifact = &artifacts[2];
  cupidbuild_seed_artifact_t *linker_artifact = &artifacts[3];
  cupidbuild_seed_artifact_t *object_artifact = &artifacts[4];
  cupidbuild_host_snapshot_t assembler_snapshot;
  cupidbuild_host_snapshot_t compiler_snapshot;
  cupidbuild_host_snapshot_t inspector_snapshot;
  cupidbuild_host_snapshot_t linker_snapshot;
  cupidbuild_host_snapshot_t object_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *manifest = (unsigned char *)0;
  unsigned char *candidate = (unsigned char *)0;
  size_t manifest_size = 0u;
  const char *manifest_reason = "manifest path is invalid";
  const char *assembler_arguments[6];
  const char *inspector_arguments[5];
  int result = 1;
  if (request == (const cupidbuild_object_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid guarded object request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(request->repository_root,
                                        request->source, request->output,
                                        &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (request->seed_manifest[0] == '/' || request->seed_manifest[0] == '\\' ||
      request->seed_manifest[1] == ':') {
    if (strlen(request->seed_manifest) <= strlen(request->repository_root) ||
        !cupidbuild_repository_prefix(request->seed_manifest,
                                      request->repository_root)) {
      (void)fprintf(
          stderr,
          "cupidbuild: checked seed manifest is outside the repository\n");
      goto done;
    }
    if (strlen(request->seed_manifest) + 1u > sizeof(manifest_path)) {
      goto done;
    }
    (void)memcpy(manifest_path, request->seed_manifest,
                 strlen(request->seed_manifest) + 1u);
  } else if (!cupidbuild_join(manifest_path, sizeof(manifest_path),
                              request->repository_root,
                              request->seed_manifest)) {
    goto done;
  }
  if (!cupidbuild_host_freeze_input(transaction, manifest_path, "manifest.json",
                                    &frozen_manifest,
                                    (cupidbuild_host_snapshot_t *)0)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  manifest = cupidbuild_read_private(frozen_manifest, CUPIDBUILD_MANIFEST_BYTES,
                                     &manifest_size);
  if (manifest == (unsigned char *)0) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked seed manifest is invalid: unreadable bytes\n");
    goto done;
  }
  if (!cupidbuild_json_manifest(manifest, manifest_size, artifacts,
                                &manifest_reason)) {
    (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: %s\n",
                  manifest_reason);
    goto done;
  }
  if (!cupidbuild_manifest_directory(manifest_path, manifest_directory,
                                     sizeof(manifest_directory)) ||
      !cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, assembler_artifact->file) ||
      !cupidbuild_join(inspector_path, sizeof(inspector_path),
                       manifest_directory, inspector_artifact->file)) {
    (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: "
                          "artifact path is invalid\n");
    goto done;
  }
  if (!cupidbuild_host_freeze_input(transaction, assembler_path,
                                    assembler_artifact->file, &frozen_assembler,
                                    &assembler_snapshot) ||
      !cupidbuild_host_make_input_executable(transaction, frozen_assembler) ||
      !cupidbuild_artifact_matches(&assembler_snapshot, assembler_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, compiler_artifact->file) ||
      !cupidbuild_host_freeze_input(transaction, assembler_path,
                                    compiler_artifact->file, (const char **)0,
                                    &compiler_snapshot) ||
      !cupidbuild_artifact_matches(&compiler_snapshot, compiler_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidC digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_host_freeze_input(transaction, inspector_path,
                                    inspector_artifact->file, &frozen_inspector,
                                    &inspector_snapshot) ||
      !cupidbuild_host_make_input_executable(transaction, frozen_inspector) ||
      !cupidbuild_artifact_matches(&inspector_snapshot, inspector_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, linker_artifact->file) ||
      !cupidbuild_host_freeze_input(transaction, assembler_path,
                                    linker_artifact->file, (const char **)0,
                                    &linker_snapshot) ||
      !cupidbuild_artifact_matches(&linker_snapshot, linker_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidLD digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, object_artifact->file) ||
      !cupidbuild_host_freeze_input(transaction, assembler_path,
                                    object_artifact->file, (const char **)0,
                                    &object_snapshot) ||
      !cupidbuild_artifact_matches(&object_snapshot, object_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj digest mismatch\n");
    goto done;
  }
  assembler_arguments[0] = "-f";
  assembler_arguments[1] = "elf32";
  assembler_arguments[2] = "-o";
  assembler_arguments[3] = cupidbuild_host_candidate(transaction);
  assembler_arguments[4] = cupidbuild_host_frozen_source(transaction);
  assembler_arguments[5] = (const char *)0;
  if (cupidbuild_host_run(transaction, frozen_assembler, assembler_arguments,
                          60000u) != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate) ||
      !cupidbuild_validate_object(candidate, candidate_snapshot.size)) {
    (void)fprintf(
        stderr,
        "cupidbuild: checked CupidASM relocatable object validation failed\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  inspector_arguments[0] = "--require-known";
  inspector_arguments[1] = "--require-local-targets";
  inspector_arguments[2] = "--require-code-anchors";
  inspector_arguments[3] = cupidbuild_host_candidate(transaction);
  inspector_arguments[4] = (const char *)0;
  if (cupidbuild_host_run(transaction, frozen_inspector, inspector_arguments,
                          60000u) != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_inputs(transaction) ||
      !cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(candidate);
  free(manifest);
  cupidbuild_host_transaction_close(transaction);
  return result;
}
