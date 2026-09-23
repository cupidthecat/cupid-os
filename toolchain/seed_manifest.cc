#include "seed_manifest.h"
#include "seed_release.h"
#include "cupidbuild_host.h"
#include "contract_parse_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CUPIDBUILD_JSON_TOKENS 2048u
#define CUPIDBUILD_SEED_ARTIFACTS 6u
typedef struct {
  char file[32];
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

static int cupidbuild_json_decode(const unsigned char *bytes,
                                  const cupidbuild_json_token_t *token,
                                  text_t *text) {
  json_reader_t reader;
  error_context_t error = {0, 0u, 0};
  if (token->type != CUPIDBUILD_JSON_STRING || token->start == 0u || token->end < token->start) return 0;
  reader.bytes = bytes + token->start - 1u;
  reader.size = token->end - token->start + 2u;
  reader.position = 0u;
  return cupid_contract_json_parse_string(&error, &reader, text) &&
         cupid_contract_json_finish(&error, &reader);
}

static int cupidbuild_json_text(const unsigned char *bytes,
                                const cupidbuild_json_token_t *token,
                                const char *expected) {
  text_t text = {0};
  size_t size = token->end - token->start;
  int ok;
  if (token->type == CUPIDBUILD_JSON_PRIMITIVE) return strlen(expected) == size && memcmp(bytes + token->start, expected, size) == 0;
  if (token->type != CUPIDBUILD_JSON_STRING) return 0;
  if (!memchr(bytes + token->start, '\\', size)) {
    return strlen(expected) == size && memcmp(bytes + token->start, expected, size) == 0;
  }
  ok = cupidbuild_json_decode(bytes, token, &text) &&
       cupid_contract_text_equals_literal(&text, expected);
  cupid_contract_text_release(&text);
  return ok;
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
  text_t text = {0};
  int ok = cupidbuild_json_decode(bytes, token, &text);
  if (ok && (text.size >= capacity || memchr(text.bytes, '\0', text.size))) ok = 0;
  if (ok) {
    memcpy(destination, text.bytes, text.size);
    destination[text.size] = '\0';
  }
  cupid_contract_text_release(&text);
  return ok;
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

static int cupidbuild_json_string_field_pair(
    const unsigned char *bytes, const cupidbuild_json_token_t *tokens,
    size_t count, size_t object, const char *name, const char *first,
    const char *second) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  return value < count && tokens[value].type == CUPIDBUILD_JSON_STRING &&
         (cupidbuild_json_text(bytes, &tokens[value], first) ||
          cupidbuild_json_text(bytes, &tokens[value], second));
}

static int cupidbuild_json_lower_hex_field(
    const unsigned char *bytes, const cupidbuild_json_token_t *tokens,
    size_t count, size_t object, const char *name, size_t expected_size) {
  size_t value = cupidbuild_json_required(bytes, tokens, count, object, name);
  size_t index;
  char decoded[65];
  if (value >= count || expected_size >= sizeof(decoded) ||
      !cupidbuild_json_copy(bytes, &tokens[value], decoded, sizeof(decoded)) ||
      strlen(decoded) != expected_size) {
    return 0;
  }
  for (index = 0u; index < expected_size; index++) {
    unsigned char digit = (unsigned char)decoded[index];
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
                                      int windows, int promoted, int candidate) {
  static const char legacy_revision[] =
      "a17c9465911da41d59b7ada71733d36c39faa5ea";
  static const char legacy_snapshot[] =
      "46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67";
  static const char legacy_linux_manifest[] =
      "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b";
  static const char preceding_parent_revision[] =
      "83d00ce70e5607dc5c011bb97c6478121f24a21c";
  static const char preceding_parent_linux_manifest[] =
      "a11c8af08eb1170d040dc6b361c30df321c088fcb4ae5becd6c2864995380622";
  static const char preceding_parent_windows_manifest[] =
      "f5124cbddbeb55a61ce2f8ae93923daae512d6fec6732a532b1e8f0d15bed590";
  static const char active_parent_revision[] =
      "142a9737f618ab8500308576a1c222501d639e5f";
  static const char active_parent_linux_manifest[] =
      "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52";
  static const char active_parent_windows_manifest[] =
      "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd";
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
                                           "source_input_count", 66u) ||
             cupidbuild_json_number_field(bytes, tokens, count, object,
                                           "source_input_count", 61u) ||
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
                                      "make bootstrap-windows-from-seed")) {
      return 0;
    }
    if (!promoted) {
      return cupidbuild_json_string_field(
                 bytes, tokens, count, object,
                 "parent_seed_manifest_sha256", legacy_linux_manifest) &&
             cupidbuild_json_string_field(
                 bytes, tokens, count, object,
                 "parent_seed_source_revision", legacy_revision);
    }
    return (candidate
                ? (cupidbuild_json_string_field(
                       bytes, tokens, count, object,
                       "linux_candidate_build_plan_sha256",
                       "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe") &&
                   cupidbuild_json_string_field(
                       bytes, tokens, count, object, "native_build_plan_sha256",
                       "70158fd9780990ec0cd0ed1c4da1af9f22f8acbcb483324693fd46c2362177b9"))
                : (cupidbuild_json_string_field(
                       bytes, tokens, count, object,
                       "linux_candidate_build_plan_sha256",
                       "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd") &&
                   cupidbuild_json_string_field_pair(
                       bytes, tokens, count, object, "native_build_plan_sha256",
                       "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14",
                       "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669"))) &&
            cupidbuild_json_lower_hex_field(
                bytes, tokens, count, object, "plan_seed_manifest_sha256",
                64u) &&
            ((cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_manifest_sha256",
                   preceding_parent_windows_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_source_revision",
                   preceding_parent_revision) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_manifest_sha256",
                   preceding_parent_linux_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_source_revision",
                   preceding_parent_revision)) ||
              (cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_manifest_sha256",
                   active_parent_windows_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_execution_seed_source_revision",
                   active_parent_revision) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_manifest_sha256",
                   active_parent_linux_manifest) &&
               cupidbuild_json_string_field(
                   bytes, tokens, count, object,
                   "parent_plan_seed_source_revision",
                   active_parent_revision)));
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
          ((cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_manifest_sha256",
                preceding_parent_linux_manifest) &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_source_revision",
                preceding_parent_revision)) ||
           (cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_manifest_sha256",
                active_parent_linux_manifest) &&
            cupidbuild_json_string_field(
                bytes, tokens, count, object, "parent_seed_source_revision",
                active_parent_revision))));
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

static int cupidbuild_json_sources(const unsigned char *bytes,
                                   const cupidbuild_json_token_t *tokens,
                                   size_t count, size_t array, int promoted, int candidate) {
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
      {"cupidbuild_main", "/toolchain/cupidbuild_main.cc", 0},
      {"seed_manifest", "/toolchain/seed_manifest.cc", 0},
      {"seed_release", "/toolchain/seed_release.cc", 0},
      {"contract_parse_internal", "/toolchain/contract_parse_internal.cc", 0}};
  size_t cursor;
  size_t index;
  size_t expected_count = promoted ? (candidate ? 25u : 22u) : 19u;
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
                                 size_t count, size_t object, int promoted, int candidate) {
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
  static const char *const candidate_cupidbuild[] = {
      "start", "cupidbuild_main", "cupidbuild", "cupidbuild_host",
      "ctool_host", "ctool", "elf32", "seed_manifest", "seed_release",
      "contract_parse_internal", "runtime"};
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
                                      candidate ? candidate_cupidbuild : cupidbuild,
                                      candidate ? 11u : 8u);
}

static int cupidbuild_json_build_plan(const unsigned char *bytes,
                                      const cupidbuild_json_token_t *tokens,
                                      size_t count, size_t object,
                                      int promoted, int candidate) {
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
  if (!cupidbuild_json_sources(bytes, tokens, count, value, promoted, candidate)) {
    return 0;
  }
  value = cupidbuild_json_required(bytes, tokens, count, object, "links");
  return cupidbuild_json_links(bytes, tokens, count, value, promoted, candidate);
}

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
        actual_size == 0u || actual_size > 67108864u) {
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
                                    size_t manifest_size, int windows,
                                     cupidbuild_seed_artifact_t
                                         artifacts[CUPIDBUILD_SEED_ARTIFACTS],
                                     size_t *artifact_count_out,
                                     int *current_windows_plan_out,
                                     const char **reason_out) {
  cupidbuild_json_token_t *tokens;
  size_t count = 0u;
  size_t artifacts_token;
  size_t provenance;
  size_t schema;
  size_t target;
  int promoted;
  int candidate;
  int current_windows_plan = 0;
  const char *legacy_schema = windows ? "cupid.execution-seed.v1" : "cupid.bootstrap-seed.v1";
  const char *promoted_schema = windows ? "cupid.execution-seed.v2" : "cupid.bootstrap-seed.v2";
  static const char *const windows_top_names[] = {"artifacts", "provenance", "schema", "target"};
  static const char *const linux_top_names[] = {
      "artifacts", "build_plan", "build_plan_sha256", "provenance", "schema", "target"};
  const char *const *top_names = windows ? windows_top_names : linux_top_names;
  size_t top_count = windows ? 4u : 6u;
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
                             top_count)) {
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
  candidate = promoted && provenance < count &&
      cupidbuild_json_number_field(manifest, tokens, count, provenance,
                                   "source_input_count", 66u);
  if (!cupidbuild_json_provenance(manifest, tokens, count, provenance,
                                  windows, promoted, candidate)) {
    *reason_out = "fixed-point provenance differs";
    free(tokens);
    return 0;
  }
  if (windows && promoted) {
    current_windows_plan = candidate || cupidbuild_json_string_field(
        manifest, tokens, count, provenance, "native_build_plan_sha256",
        "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669");
  }
  if (!cupidbuild_json_target(manifest, tokens, count, target, windows)) {
    *reason_out = "target contract differs";
    free(tokens);
    return 0;
  }
  if (!windows) {
    size_t plan =
        cupidbuild_json_required(manifest, tokens, count, 0u, "build_plan");
    const char *expected_plan_sha256 =
        candidate
            ? "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe"
            : (promoted
                   ? "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
                   : "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc");
    if (!cupidbuild_json_string_field(manifest, tokens, count, 0u,
                                      "build_plan_sha256",
                                      expected_plan_sha256) ||
        !cupidbuild_json_build_plan(manifest, tokens, count, plan,
                                    promoted, candidate)) {
      *reason_out = "build plan differs";
      free(tokens);
      return 0;
    }
  }
  *artifact_count_out = promoted ? CUPIDBUILD_SEED_ARTIFACTS : 5u;
  *current_windows_plan_out = current_windows_plan;
  free(tokens);
  return 1;
}


int cupid_seed_manifest_validate(const unsigned char *bytes, size_t size,
    unsigned int format, cupid_seed_manifest_result_t *result,
    char *error, size_t error_capacity) {
  cupidbuild_seed_artifact_t artifacts[6];
  size_t artifact_count = 0u;
  size_t index;
  int current_windows_plan = 0;
  const char *reason = "manifest validation failed";
  error_context_t context = {error, error_capacity, 0};
  if (result) memset(result, 0, sizeof(*result));
  if (error_capacity && !error) return 0;
  if (error_capacity) error[0] = '\0';
  if (!bytes || !result || !size || size > 1048576u || (format != 1u && format != 2u)) {
    return cupid_contract_set_error(&context, "invalid manifest validator arguments");
  }
  memset(artifacts, 0, sizeof(artifacts));
  if (!cupidbuild_json_manifest(bytes, size, format == 2u, artifacts,
      &artifact_count, &current_windows_plan, &reason)) {
    return cupid_contract_set_error(&context, reason);
  }
  result->artifact_count = (uint32_t)artifact_count;
  result->current_windows_plan = (uint32_t)current_windows_plan;
  for (index = 0u; index < artifact_count; index++) {
    memcpy(result->artifacts[index].file, artifacts[index].file, sizeof(artifacts[index].file));
    memcpy(result->artifacts[index].sha256, artifacts[index].sha256, sizeof(artifacts[index].sha256));
    result->artifacts[index].size = (uint32_t)artifacts[index].size;
  }
  return 1;
}

int cupid_seed_pair_validate(
    const unsigned char *release_bytes, size_t release_size,
    const unsigned char *linux_bytes, size_t linux_size,
    const unsigned char *windows_bytes, size_t windows_size,
    char *error, size_t error_capacity) {
  cupid_seed_manifest_result_t result;
  cupidbuild_json_token_t *tokens;
  size_t count = 0u;
  size_t provenance;
  unsigned char digest[32];
  char hexadecimal[65];
  const char *digits = "0123456789abcdef";
  size_t index;
  int ok;
  error_context_t context = {error, error_capacity, 0};
  if (!cupid_seed_manifest_validate(linux_bytes, linux_size, 1u, &result,
                                     error, error_capacity) ||
      !cupid_seed_release_match_manifest(release_bytes, release_size,
                                         linux_bytes, linux_size, 1u,
                                         error, error_capacity) ||
      !cupid_seed_manifest_validate(windows_bytes, windows_size, 2u, &result,
                                     error, error_capacity) ||
      !cupid_seed_release_match_manifest(release_bytes, release_size,
                                         windows_bytes, windows_size, 2u,
                                         error, error_capacity)) return 0;
  tokens = (cupidbuild_json_token_t *)malloc(CUPIDBUILD_JSON_TOKENS * sizeof(*tokens));
  if (!tokens) return cupid_contract_set_error(&context, "JSON token storage is unavailable");
  if (!cupidbuild_json_parse(windows_bytes, windows_size, tokens, &count)) {
    free(tokens);
    return cupid_contract_set_error(&context, "Windows manifest syntax differs");
  }
  cupidbuild_host_sha256_bytes(linux_bytes, linux_size, digest);
  for (index = 0u; index < 32u; index++) {
    hexadecimal[index * 2u] = digits[digest[index] >> 4u];
    hexadecimal[index * 2u + 1u] = digits[digest[index] & 15u];
  }
  hexadecimal[64] = '\0';
  provenance = cupidbuild_json_required(windows_bytes, tokens, count, 0u, "provenance");
  ok = provenance < count && cupidbuild_json_string_field(windows_bytes, tokens,
      count, provenance, "plan_seed_manifest_sha256", hexadecimal);
  free(tokens);
  if (!ok) return cupid_contract_set_error(&context, "Windows seed plan reference differs from captured Linux manifest");
  return 1;
}
