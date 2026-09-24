#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABI_INPUT_COUNT 6u
#define ABI_MAX_FIELDS 128u
#define ABI_MAX_ASSIGNMENTS 128u
#define ABI_MAX_TOKENS 32768u
#define ABI_MAX_SOURCE_BYTES (1024u * 1024u)
#define ABI_DIGEST_BYTES 65536u

#define ABI_EXPECTED_VERSION 5u
#define ABI_EXPECTED_FIELD_COUNT 103u
#define ABI_EXPECTED_TABLE_SIZE 412u
#define ABI_EXPECTED_DIRENT_SIZE 136u
#define ABI_EXPECTED_STAT_SIZE 8u

#define ABI_FAIL(...)                                                        \
  do {                                                                       \
    (void)snprintf(abi_error, sizeof(abi_error), __VA_ARGS__);                \
    return 0;                                                                \
  } while (0)

static const char *abi_input_paths[ABI_INPUT_COUNT] = {
    "kernel/core/types.h",      "kernel/core/syscall.h",
    "kernel/core/syscall.cc",   "kernel/fs/vfs.h",
    "kernel/network/socket.h",  "user/cupid.h"};

static const char abi_expected_digest[] =
    "3e4d31320b2f56d19d37796ef679d1abbb228de9f36c9520d2dd5ec430c3c0bc";
static const char abi_expected_provider_digest[] =
    "0a51ba85c93b0249215b05e54867fabe0e7206d7e58a7695911a6ecb060916f4";

static char abi_error[1024];

typedef struct {
  unsigned char *bytes;
  size_t size;
} abi_file_t;

typedef struct {
  const char *start;
  size_t size;
  unsigned int kind;
} abi_token_t;

typedef struct {
  abi_token_t *items;
  size_t count;
  char *source;
} abi_tokens_t;

typedef struct {
  char name[64];
  char declaration[512];
} abi_field_t;

typedef struct {
  abi_field_t items[ABI_MAX_FIELDS];
  size_t count;
} abi_fields_t;

typedef struct {
  char name[64];
  char value[512];
} abi_assignment_t;

typedef struct {
  abi_assignment_t items[ABI_MAX_ASSIGNMENTS];
  size_t count;
} abi_assignments_t;

typedef struct {
  unsigned char bytes[ABI_DIGEST_BYTES];
  size_t size;
} abi_digest_input_t;

typedef struct {
  unsigned int size;
  unsigned int name_offset;
  unsigned int value_offset;
  unsigned int type_offset;
} abi_record_layout_t;

typedef struct {
  unsigned int version;
  unsigned int field_count;
  unsigned int table_size;
  unsigned int dirent_size;
  unsigned int dirent_name_offset;
  unsigned int dirent_value_offset;
  unsigned int dirent_type_offset;
  unsigned int stat_size;
  unsigned int stat_value_offset;
  unsigned int stat_type_offset;
  unsigned int provider_count;
  char first_function[64];
  char last_function[64];
  char abi_sha256[65];
  char provider_sha256[65];
} abi_report_t;

enum {
  ABI_TOKEN_OTHER = 0u,
  ABI_TOKEN_IDENTIFIER = 1u,
  ABI_TOKEN_NUMBER = 2u,
  ABI_TOKEN_STRING = 3u
};

static int abi_is_space(char character) {
  return character == ' ' || character == '\t' || character == '\r' ||
         character == '\n' || character == '\f' || character == '\v';
}

static int abi_is_identifier_start(char character) {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') || character == '_';
}

static int abi_is_identifier_continue(char character) {
  return abi_is_identifier_start(character) ||
         (character >= '0' && character <= '9');
}

static int abi_token_equal(const abi_token_t *token, const char *text) {
  size_t size = strlen(text);
  return token->size == size && memcmp(token->start, text, size) == 0;
}

static int abi_copy_token(const abi_token_t *token, char *destination,
                          size_t capacity) {
  if (token->size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, token->start, token->size);
  destination[token->size] = '\0';
  return 1;
}

static char *abi_path_join(const char *root, const char *relative) {
  size_t root_size = strlen(root);
  size_t relative_size = strlen(relative);
  int separator = root_size != 0u && root[root_size - 1u] != '/' &&
                  root[root_size - 1u] != '\\';
  char *path = (char *)malloc(root_size + (size_t)separator + relative_size +
                              1u);
  if (path == (char *)0) {
    return (char *)0;
  }
  (void)memcpy(path, root, root_size);
  if (separator) {
    path[root_size++] = '/';
  }
  (void)memcpy(path + root_size, relative, relative_size);
  path[root_size + relative_size] = '\0';
  return path;
}

static void abi_file_release(abi_file_t *file) {
  free(file->bytes);
  file->bytes = (unsigned char *)0;
  file->size = 0u;
}

static int abi_read_file(const char *root, const char *relative,
                         abi_file_t *file) {
  char *path = abi_path_join(root, relative);
  FILE *stream;
  long length;
  size_t read_size;
  file->bytes = (unsigned char *)0;
  file->size = 0u;
  if (path == (char *)0) {
    ABI_FAIL("cannot allocate the path for ABI input %s", relative);
  }
  stream = fopen(path, "rb");
  free(path);
  if (stream == (FILE *)0) {
    ABI_FAIL("ABI input is unavailable: %s", relative);
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    (void)fclose(stream);
    ABI_FAIL("cannot measure ABI input %s", relative);
  }
  length = ftell(stream);
  if (length < 0L || (unsigned long)length > ABI_MAX_SOURCE_BYTES ||
      fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    ABI_FAIL("ABI input has an unsupported size: %s", relative);
  }
  file->bytes = (unsigned char *)malloc((size_t)length + 1u);
  if (file->bytes == (unsigned char *)0) {
    (void)fclose(stream);
    ABI_FAIL("cannot allocate ABI input %s", relative);
  }
  read_size = fread(file->bytes, 1u, (size_t)length, stream);
  if (read_size != (size_t)length || fclose(stream) != 0) {
    abi_file_release(file);
    ABI_FAIL("cannot read ABI input %s", relative);
  }
  file->bytes[read_size] = 0u;
  file->size = read_size;
  return 1;
}

static char *abi_without_comments(const abi_file_t *file) {
  char *result = (char *)malloc(file->size + 1u);
  size_t index = 0u;
  char quote = '\0';
  int escaped = 0;
  if (result == (char *)0) {
    return (char *)0;
  }
  while (index < file->size) {
    char character = (char)file->bytes[index];
    if (quote != '\0') {
      result[index] = character;
      if (escaped) {
        escaped = 0;
      } else if (character == '\\') {
        escaped = 1;
      } else if (character == quote) {
        quote = '\0';
      }
      index++;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      result[index++] = character;
      continue;
    }
    if (character == '/' && index + 1u < file->size &&
        file->bytes[index + 1u] == (unsigned char)'/') {
      result[index++] = ' ';
      result[index++] = ' ';
      while (index < file->size && file->bytes[index] != (unsigned char)'\n' &&
             file->bytes[index] != (unsigned char)'\r') {
        result[index++] = ' ';
      }
      continue;
    }
    if (character == '/' && index + 1u < file->size &&
        file->bytes[index + 1u] == (unsigned char)'*') {
      result[index++] = ' ';
      result[index++] = ' ';
      while (index + 1u < file->size &&
             !(file->bytes[index] == (unsigned char)'*' &&
               file->bytes[index + 1u] == (unsigned char)'/')) {
        result[index] = file->bytes[index] == (unsigned char)'\n' ? '\n' : ' ';
        index++;
      }
      if (index + 1u >= file->size) {
        free(result);
        return (char *)0;
      }
      result[index++] = ' ';
      result[index++] = ' ';
      continue;
    }
    result[index++] = character;
  }
  result[file->size] = '\0';
  return result;
}

static int abi_tokenize(const abi_file_t *file, abi_tokens_t *tokens) {
  size_t index = 0u;
  tokens->count = 0u;
  tokens->items = (abi_token_t *)0;
  tokens->source = abi_without_comments(file);
  if (tokens->source == (char *)0) {
    ABI_FAIL("ABI source has an incomplete comment or needs more memory");
  }
  tokens->items =
      (abi_token_t *)calloc(ABI_MAX_TOKENS, sizeof(abi_token_t));
  if (tokens->items == (abi_token_t *)0) {
    free(tokens->source);
    tokens->source = (char *)0;
    ABI_FAIL("cannot allocate ABI source tokens");
  }
  while (index < file->size) {
    size_t start;
    unsigned int kind = ABI_TOKEN_OTHER;
    char character = tokens->source[index];
    if (abi_is_space(character)) {
      index++;
      continue;
    }
    if (tokens->count == ABI_MAX_TOKENS) {
      ABI_FAIL("ABI source has too many tokens");
    }
    start = index;
    if (abi_is_identifier_start(character)) {
      kind = ABI_TOKEN_IDENTIFIER;
      index++;
      while (index < file->size &&
             abi_is_identifier_continue(tokens->source[index])) {
        index++;
      }
    } else if (character >= '0' && character <= '9') {
      kind = ABI_TOKEN_NUMBER;
      index++;
      while (index < file->size &&
             abi_is_identifier_continue(tokens->source[index])) {
        index++;
      }
    } else if (character == '\'' || character == '"') {
      char quote = character;
      int escaped = 0;
      kind = ABI_TOKEN_STRING;
      index++;
      while (index < file->size) {
        character = tokens->source[index++];
        if (escaped) {
          escaped = 0;
        } else if (character == '\\') {
          escaped = 1;
        } else if (character == quote) {
          break;
        }
      }
      if (index > file->size || tokens->source[index - 1u] != quote) {
        ABI_FAIL("ABI source has an incomplete string or character literal");
      }
    } else if (index + 2u < file->size &&
               ((character == '.' && tokens->source[index + 1u] == '.' &&
                 tokens->source[index + 2u] == '.') ||
                ((character == '<' || character == '>') &&
                 tokens->source[index + 1u] == character &&
                 tokens->source[index + 2u] == '='))) {
      index += 3u;
    } else if (index + 1u < file->size &&
               ((character == '=' && tokens->source[index + 1u] == '=') ||
                (character == '!' && tokens->source[index + 1u] == '=') ||
                (character == '<' && tokens->source[index + 1u] == '=') ||
                (character == '>' && tokens->source[index + 1u] == '=') ||
                (character == '-' && tokens->source[index + 1u] == '>') ||
                (character == '&' && tokens->source[index + 1u] == '&') ||
                (character == '|' && tokens->source[index + 1u] == '|'))) {
      index += 2u;
    } else {
      index++;
    }
    tokens->items[tokens->count].start = tokens->source + start;
    tokens->items[tokens->count].size = index - start;
    tokens->items[tokens->count].kind = kind;
    tokens->count++;
  }
  return 1;
}

static void abi_tokens_release(abi_tokens_t *tokens) {
  free(tokens->items);
  free(tokens->source);
  tokens->items = (abi_token_t *)0;
  tokens->source = (char *)0;
  tokens->count = 0u;
}

static const char *abi_canonical_token(const abi_token_t *token) {
  if (abi_token_equal(token, "vfs_dirent_t")) {
    return "cupid_dirent_t";
  }
  if (abi_token_equal(token, "vfs_stat_t")) {
    return "cupid_stat_t";
  }
  return (const char *)0;
}

static int abi_canonical_range(const abi_tokens_t *tokens, size_t first,
                               size_t last, char *destination,
                               size_t capacity) {
  size_t output = 0u;
  size_t index;
  for (index = first; index < last; index++) {
    const abi_token_t *token = &tokens->items[index];
    const char *alias = abi_canonical_token(token);
    size_t size = alias != (const char *)0 ? strlen(alias) : token->size;
    if (output != 0u) {
      if (output + 1u >= capacity) {
        return 0;
      }
      destination[output++] = ' ';
    }
    if (output + size >= capacity) {
      return 0;
    }
    if (alias != (const char *)0) {
      (void)memcpy(destination + output, alias, size);
    } else {
      (void)memcpy(destination + output, token->start, size);
    }
    output += size;
  }
  if (output >= capacity) {
    return 0;
  }
  destination[output] = '\0';
  return 1;
}

static int abi_field_from_range(const abi_tokens_t *tokens, size_t first,
                                size_t last, abi_field_t *field) {
  size_t index;
  size_t name_index = last;
  int bracket_depth = 0;
  for (index = first; index + 3u < last; index++) {
    if (abi_token_equal(&tokens->items[index], "(") &&
        abi_token_equal(&tokens->items[index + 1u], "*") &&
        tokens->items[index + 2u].kind == ABI_TOKEN_IDENTIFIER &&
        abi_token_equal(&tokens->items[index + 3u], ")")) {
      name_index = index + 2u;
      break;
    }
  }
  if (name_index == last) {
    index = last;
    while (index > first) {
      index--;
      if (abi_token_equal(&tokens->items[index], "]")) {
        bracket_depth++;
      } else if (abi_token_equal(&tokens->items[index], "[")) {
        bracket_depth--;
      } else if (bracket_depth == 0 &&
                 tokens->items[index].kind == ABI_TOKEN_IDENTIFIER) {
        name_index = index;
        break;
      }
    }
  }
  if (name_index == last ||
      !abi_copy_token(&tokens->items[name_index], field->name,
                      sizeof(field->name))) {
    ABI_FAIL("cannot name syscall or record field");
  }
  if (!abi_canonical_range(tokens, first, last, field->declaration,
                           sizeof(field->declaration))) {
    ABI_FAIL("ABI field declaration is too long: %s", field->name);
  }
  return 1;
}

static int abi_fields_from_range(const abi_tokens_t *tokens, size_t first,
                                 size_t last, abi_fields_t *fields) {
  size_t declaration = first;
  size_t index;
  int parentheses = 0;
  int brackets = 0;
  fields->count = 0u;
  for (index = first; index < last; index++) {
    if (abi_token_equal(&tokens->items[index], "(")) {
      parentheses++;
    } else if (abi_token_equal(&tokens->items[index], ")")) {
      parentheses--;
    } else if (abi_token_equal(&tokens->items[index], "[")) {
      brackets++;
    } else if (abi_token_equal(&tokens->items[index], "]")) {
      brackets--;
    } else if (abi_token_equal(&tokens->items[index], ";") &&
               parentheses == 0 && brackets == 0) {
      size_t other;
      if (declaration != index) {
        if (fields->count == ABI_MAX_FIELDS) {
          ABI_FAIL("ABI record has too many fields");
        }
        if (!abi_field_from_range(tokens, declaration, index,
                                  &fields->items[fields->count])) {
          return 0;
        }
        for (other = 0u; other < fields->count; other++) {
          if (strcmp(fields->items[other].name,
                     fields->items[fields->count].name) == 0) {
            ABI_FAIL("ABI record repeats field %s",
                     fields->items[fields->count].name);
          }
        }
        fields->count++;
      }
      declaration = index + 1u;
    }
    if (parentheses < 0 || brackets < 0) {
      ABI_FAIL("ABI record declaration has unbalanced delimiters");
    }
  }
  if (parentheses != 0 || brackets != 0 || declaration != last) {
    ABI_FAIL("ABI record declaration is incomplete");
  }
  return 1;
}

static int abi_find_matching(const abi_tokens_t *tokens, size_t opening,
                             const char *left, const char *right,
                             size_t *closing) {
  size_t index;
  unsigned int depth = 0u;
  for (index = opening; index < tokens->count; index++) {
    if (tokens->items[index].kind == ABI_TOKEN_STRING) {
      continue;
    }
    if (abi_token_equal(&tokens->items[index], left)) {
      depth++;
    } else if (abi_token_equal(&tokens->items[index], right)) {
      if (depth == 0u) {
        return 0;
      }
      depth--;
      if (depth == 0u) {
        *closing = index;
        return 1;
      }
    }
  }
  return 0;
}

static int abi_parse_syscall_table(const abi_file_t *file, const char *label,
                                   abi_fields_t *fields) {
  abi_tokens_t tokens;
  size_t index;
  if (!abi_tokenize(file, &tokens)) {
    return 0;
  }
  for (index = 0u; index + 4u < tokens.count; index++) {
    size_t closing;
    if (!abi_token_equal(&tokens.items[index], "typedef") ||
        !abi_token_equal(&tokens.items[index + 1u], "struct") ||
        !abi_token_equal(&tokens.items[index + 2u],
                         "cupid_syscall_table") ||
        !abi_token_equal(&tokens.items[index + 3u], "{")) {
      continue;
    }
    if (!abi_find_matching(&tokens, index + 3u, "{", "}", &closing) ||
        closing + 2u >= tokens.count ||
        !abi_token_equal(&tokens.items[closing + 1u],
                         "cupid_syscall_table_t") ||
        !abi_token_equal(&tokens.items[closing + 2u], ";")) {
      abi_tokens_release(&tokens);
      ABI_FAIL("%s has an incomplete cupid_syscall_table_t", label);
    }
    if (!abi_fields_from_range(&tokens, index + 4u, closing, fields)) {
      abi_tokens_release(&tokens);
      return 0;
    }
    abi_tokens_release(&tokens);
    return 1;
  }
  abi_tokens_release(&tokens);
  ABI_FAIL("%s does not define cupid_syscall_table_t", label);
}

static int abi_compare_fields(const abi_fields_t *kernel,
                              const abi_fields_t *user) {
  size_t index;
  size_t common = kernel->count < user->count ? kernel->count : user->count;
  for (index = 0u; index < common; index++) {
    if (strcmp(kernel->items[index].name, user->items[index].name) != 0 ||
        strcmp(kernel->items[index].declaration,
               user->items[index].declaration) != 0) {
      ABI_FAIL("syscall field %u differs: kernel %s (%s), user %s (%s)",
               (unsigned int)index, kernel->items[index].name,
               kernel->items[index].declaration, user->items[index].name,
               user->items[index].declaration);
    }
  }
  if (kernel->count != user->count) {
    ABI_FAIL("syscall field count differs: kernel %u, user %u",
             (unsigned int)kernel->count, (unsigned int)user->count);
  }
  return 1;
}

static int abi_canonical_integer_type(const abi_tokens_t *tokens,
                                      size_t first, size_t last,
                                      const char *label, char output[32]) {
  unsigned int signed_count = 0u;
  unsigned int unsigned_count = 0u;
  unsigned int char_count = 0u;
  unsigned int short_count = 0u;
  unsigned int int_count = 0u;
  unsigned int long_count = 0u;
  size_t index;
  const char *sign;
  const char *kind;
  for (index = first; index < last; index++) {
    const abi_token_t *token = &tokens->items[index];
    if (abi_token_equal(token, "signed")) {
      signed_count++;
    } else if (abi_token_equal(token, "unsigned")) {
      unsigned_count++;
    } else if (abi_token_equal(token, "char")) {
      char_count++;
    } else if (abi_token_equal(token, "short")) {
      short_count++;
    } else if (abi_token_equal(token, "int")) {
      int_count++;
    } else if (abi_token_equal(token, "long")) {
      long_count++;
    } else {
      ABI_FAIL("%s has an unsupported integer type", label);
    }
  }
  if (signed_count + unsigned_count > 1u || char_count > 1u ||
      short_count > 1u || int_count > 1u || long_count > 2u) {
    ABI_FAIL("%s has an invalid integer type", label);
  }
  sign = unsigned_count != 0u ? "unsigned" : "signed";
  if (char_count == 1u && short_count == 0u && int_count == 0u &&
      long_count == 0u) {
    kind = "char";
  } else if (short_count == 1u && char_count == 0u && long_count == 0u) {
    kind = "short";
  } else if (long_count == 0u && char_count == 0u && short_count == 0u) {
    kind = "int";
  } else if (long_count == 1u && char_count == 0u && short_count == 0u) {
    kind = "long";
  } else if (long_count == 2u && char_count == 0u && short_count == 0u) {
    kind = "long long";
  } else {
    ABI_FAIL("%s has an unsupported integer type", label);
  }
  if (snprintf(output, 32u, "%s %s", sign, kind) < 0) {
    ABI_FAIL("cannot render %s", label);
  }
  return 1;
}

static int abi_find_scalar_typedef(const abi_file_t *file, const char *name,
                                   const char *label, char output[32]) {
  abi_tokens_t tokens;
  size_t index;
  unsigned int matches = 0u;
  if (!abi_tokenize(file, &tokens)) {
    return 0;
  }
  for (index = 0u; index < tokens.count; index++) {
    size_t semicolon;
    if (!abi_token_equal(&tokens.items[index], "typedef")) {
      continue;
    }
    semicolon = index + 1u;
    while (semicolon < tokens.count &&
           !abi_token_equal(&tokens.items[semicolon], ";") &&
           !abi_token_equal(&tokens.items[semicolon], "{") &&
           !abi_token_equal(&tokens.items[semicolon], "}")) {
      semicolon++;
    }
    if (semicolon >= tokens.count || semicolon == index + 1u ||
        !abi_token_equal(&tokens.items[semicolon], ";")) {
      continue;
    }
    if (abi_token_equal(&tokens.items[semicolon - 1u], name)) {
      char type_label[160];
      matches++;
      (void)snprintf(type_label, sizeof(type_label), "%s %s", label, name);
      if (!abi_canonical_integer_type(&tokens, index + 1u, semicolon - 1u,
                                      type_label, output)) {
        abi_tokens_release(&tokens);
        return 0;
      }
    }
    index = semicolon;
  }
  abi_tokens_release(&tokens);
  if (matches != 1u) {
    ABI_FAIL("%s must define %s exactly once", label, name);
  }
  return 1;
}

static int abi_validate_scalar_types(const abi_file_t *kernel,
                                     const abi_file_t *user) {
  static const char *names[5] = {"uint8_t", "uint16_t", "uint32_t",
                                 "int32_t", "size_t"};
  static const char *expected[5] = {"unsigned char", "unsigned short",
                                    "unsigned int", "signed int",
                                    "unsigned long"};
  size_t index;
  for (index = 0u; index < 5u; index++) {
    char kernel_type[32];
    char user_type[32];
    if (!abi_find_scalar_typedef(kernel, names[index], "kernel types header",
                                 kernel_type) ||
        !abi_find_scalar_typedef(user, names[index], "user API header",
                                 user_type)) {
      return 0;
    }
    if (strcmp(kernel_type, user_type) != 0) {
      ABI_FAIL("%s differs: kernel %s, user %s", names[index], kernel_type,
               user_type);
    }
    if (strcmp(kernel_type, expected[index]) != 0) {
      ABI_FAIL("%s has an unexpected i386 layout: %s", names[index],
               kernel_type);
    }
  }
  return 1;
}

static int abi_parse_integer_text(const char *text, size_t size, int *value) {
  size_t index = 0u;
  unsigned int base = 10u;
  unsigned int magnitude = 0u;
  int negative = 0;
  int digits = 0;
  if (index < size && (text[index] == '+' || text[index] == '-')) {
    negative = text[index] == '-';
    index++;
  }
  if (index + 1u < size && text[index] == '0' &&
      (text[index + 1u] == 'x' || text[index + 1u] == 'X')) {
    base = 16u;
    index += 2u;
  }
  while (index < size) {
    unsigned int digit;
    char character = text[index];
    if (character >= '0' && character <= '9') {
      digit = (unsigned int)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = (unsigned int)(character - 'a') + 10u;
    } else if (character >= 'A' && character <= 'F') {
      digit = (unsigned int)(character - 'A') + 10u;
    } else {
      break;
    }
    if (digit >= base || magnitude > (0x7fffffffu - digit) / base) {
      return 0;
    }
    magnitude = magnitude * base + digit;
    digits = 1;
    index++;
  }
  if (!digits) {
    return 0;
  }
  while (index < size && (text[index] == 'u' || text[index] == 'U' ||
                          text[index] == 'l' || text[index] == 'L')) {
    index++;
  }
  if (index != size) {
    return 0;
  }
  *value = negative ? -(int)magnitude : (int)magnitude;
  return 1;
}

static int abi_integer_macro(const abi_file_t *file, const char *name,
                             const char *label, int *value) {
  char *source = abi_without_comments(file);
  const char *cursor;
  unsigned int matches = 0u;
  if (source == (char *)0) {
    ABI_FAIL("cannot scan macros in %s", label);
  }
  cursor = source;
  while (*cursor != '\0') {
    const char *line = cursor;
    const char *end;
    const char *word;
    size_t word_size;
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
      cursor++;
    }
    end = cursor;
    while (*cursor == '\n' || *cursor == '\r') {
      cursor++;
    }
    while (line < end && abi_is_space(*line)) {
      line++;
    }
    if (line == end || *line++ != '#') {
      continue;
    }
    while (line < end && abi_is_space(*line)) {
      line++;
    }
    word = line;
    while (line < end && abi_is_identifier_continue(*line)) {
      line++;
    }
    if ((size_t)(line - word) != 6u || memcmp(word, "define", 6u) != 0) {
      continue;
    }
    while (line < end && abi_is_space(*line)) {
      line++;
    }
    word = line;
    while (line < end && abi_is_identifier_continue(*line)) {
      line++;
    }
    word_size = (size_t)(line - word);
    if (word_size != strlen(name) || memcmp(word, name, word_size) != 0) {
      continue;
    }
    while (line < end && abi_is_space(*line)) {
      line++;
    }
    word = line;
    while (line < end && !abi_is_space(*line)) {
      line++;
    }
    word_size = (size_t)(line - word);
    while (line < end && abi_is_space(*line)) {
      line++;
    }
    if (line != end || !abi_parse_integer_text(word, word_size, value)) {
      free(source);
      ABI_FAIL("%s has a noninteger %s", label, name);
    }
    matches++;
  }
  free(source);
  if (matches != 1u) {
    ABI_FAIL("%s must define %s exactly once", label, name);
  }
  return 1;
}

typedef struct {
  const char *name;
  int expected;
} abi_named_value_t;

static int abi_validate_vfs_constants(const abi_file_t *kernel,
                                      const abi_file_t *user) {
  static const abi_named_value_t values[] = {
      {"O_RDONLY", 0x0000},   {"O_WRONLY", 0x0001},
      {"O_RDWR", 0x0002},     {"O_CREAT", 0x0100},
      {"O_TRUNC", 0x0200},    {"O_APPEND", 0x0400},
      {"SEEK_SET", 0},        {"SEEK_CUR", 1},
      {"SEEK_END", 2},        {"VFS_TYPE_FILE", 0},
      {"VFS_TYPE_DIR", 1},    {"VFS_TYPE_DEV", 2},
      {"VFS_MAX_NAME", 128},  {"VFS_MAX_PATH", 512}};
  size_t index;
  for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
    int kernel_value;
    int user_value;
    if (!abi_integer_macro(kernel, values[index].name, "kernel VFS header",
                           &kernel_value) ||
        !abi_integer_macro(user, values[index].name, "user API header",
                           &user_value)) {
      return 0;
    }
    if (kernel_value != user_value) {
      ABI_FAIL("%s differs: kernel %d, user %d", values[index].name,
               kernel_value, user_value);
    }
    if (kernel_value != values[index].expected) {
      ABI_FAIL("%s changed from the reviewed ABI value %d to %d",
               values[index].name, values[index].expected, kernel_value);
    }
  }
  return 1;
}

static int abi_enum_value(const abi_file_t *file, const char *type_name,
                          const char *enumerator, const char *label,
                          int *result) {
  abi_tokens_t tokens;
  size_t index;
  if (!abi_tokenize(file, &tokens)) {
    return 0;
  }
  for (index = 0u; index + 2u < tokens.count; index++) {
    size_t opening;
    size_t closing;
    size_t cursor;
    int next_value = 0;
    if (!abi_token_equal(&tokens.items[index], "typedef") ||
        !abi_token_equal(&tokens.items[index + 1u], "enum")) {
      continue;
    }
    opening = index + 2u;
    if (opening < tokens.count &&
        tokens.items[opening].kind == ABI_TOKEN_IDENTIFIER) {
      opening++;
    }
    if (opening >= tokens.count ||
        !abi_token_equal(&tokens.items[opening], "{") ||
        !abi_find_matching(&tokens, opening, "{", "}", &closing) ||
        closing + 2u >= tokens.count ||
        !abi_token_equal(&tokens.items[closing + 1u], type_name) ||
        !abi_token_equal(&tokens.items[closing + 2u], ";")) {
      continue;
    }
    cursor = opening + 1u;
    while (cursor < closing) {
      char name[64];
      int value = next_value;
      if (abi_token_equal(&tokens.items[cursor], ",")) {
        cursor++;
        continue;
      }
      if (tokens.items[cursor].kind != ABI_TOKEN_IDENTIFIER ||
          !abi_copy_token(&tokens.items[cursor], name, sizeof(name))) {
        abi_tokens_release(&tokens);
        ABI_FAIL("%s has an invalid %s enumerator", label, type_name);
      }
      cursor++;
      if (cursor < closing && abi_token_equal(&tokens.items[cursor], "=")) {
        cursor++;
        if (cursor >= closing ||
            !abi_parse_integer_text(tokens.items[cursor].start,
                                    tokens.items[cursor].size, &value)) {
          abi_tokens_release(&tokens);
          ABI_FAIL("%s has a noninteger %s", label, name);
        }
        cursor++;
      }
      if (strcmp(name, enumerator) == 0) {
        *result = value;
        abi_tokens_release(&tokens);
        return 1;
      }
      next_value = value + 1;
      if (cursor < closing && !abi_token_equal(&tokens.items[cursor], ",")) {
        abi_tokens_release(&tokens);
        ABI_FAIL("%s has an invalid %s enumerator", label, type_name);
      }
    }
    abi_tokens_release(&tokens);
    ABI_FAIL("%s does not define %s", label, enumerator);
  }
  abi_tokens_release(&tokens);
  ABI_FAIL("%s does not define %s", label, type_name);
}

static int abi_validate_network_constants(const abi_file_t *kernel,
                                          const abi_file_t *user) {
  static const char *user_names[4] = {"SOCK_UDP", "SOCK_TCP", "SOL_TLS",
                                      "TLS_ENABLE"};
  static const char *kernel_names[4] = {"SOCK_TYPE_UDP", "SOCK_TYPE_TCP",
                                        "SOL_TLS", "TLS_ENABLE"};
  static const int expected[4] = {1, 2, 1, 1};
  static const abi_named_value_t states[] = {
      {"TCPS_CLOSED", 0},     {"TCPS_LISTEN", 1},
      {"TCPS_SYN_SENT", 2},   {"TCPS_SYN_RCVD", 3},
      {"TCPS_ESTABLISHED", 4}, {"TCPS_FIN_WAIT_1", 5},
      {"TCPS_FIN_WAIT_2", 6}, {"TCPS_TIME_WAIT", 7},
      {"TCPS_CLOSE_WAIT", 8}, {"TCPS_LAST_ACK", 9}};
  size_t index;
  for (index = 0u; index < 4u; index++) {
    int kernel_value;
    int user_value;
    if (!abi_integer_macro(kernel, kernel_names[index],
                           "kernel socket header", &kernel_value) ||
        !abi_integer_macro(user, user_names[index], "user API header",
                           &user_value)) {
      return 0;
    }
    if (kernel_value != user_value) {
      ABI_FAIL("%s differs: kernel %d, user %d", user_names[index],
               kernel_value, user_value);
    }
    if (kernel_value != expected[index]) {
      ABI_FAIL("%s changed from the reviewed ABI value %d to %d",
               kernel_names[index], expected[index], kernel_value);
    }
  }
  for (index = 0u; index < sizeof(states) / sizeof(states[0]); index++) {
    int kernel_value;
    int user_value;
    if (!abi_enum_value(kernel, "tcp_state_t", states[index].name,
                        "kernel socket header", &kernel_value) ||
        !abi_integer_macro(user, states[index].name, "user API header",
                           &user_value)) {
      return 0;
    }
    if (kernel_value != user_value) {
      ABI_FAIL("%s differs: kernel %d, user %d", states[index].name,
               kernel_value, user_value);
    }
    if (kernel_value != states[index].expected) {
      ABI_FAIL("%s changed from the reviewed ABI value %d to %d",
               states[index].name, states[index].expected, kernel_value);
    }
  }
  return 1;
}

static int abi_parse_record(const abi_file_t *file, const char *name,
                            const char *label, abi_fields_t *fields) {
  abi_tokens_t tokens;
  size_t index;
  if (!abi_tokenize(file, &tokens)) {
    return 0;
  }
  for (index = 0u; index + 2u < tokens.count; index++) {
    size_t opening;
    size_t closing;
    if (!abi_token_equal(&tokens.items[index], "typedef") ||
        !abi_token_equal(&tokens.items[index + 1u], "struct")) {
      continue;
    }
    opening = index + 2u;
    if (opening < tokens.count &&
        tokens.items[opening].kind == ABI_TOKEN_IDENTIFIER) {
      opening++;
    }
    if (opening >= tokens.count ||
        !abi_token_equal(&tokens.items[opening], "{") ||
        !abi_find_matching(&tokens, opening, "{", "}", &closing) ||
        closing + 2u >= tokens.count ||
        !abi_token_equal(&tokens.items[closing + 1u], name) ||
        !abi_token_equal(&tokens.items[closing + 2u], ";")) {
      continue;
    }
    if (!abi_fields_from_range(&tokens, opening + 1u, closing, fields)) {
      abi_tokens_release(&tokens);
      return 0;
    }
    abi_tokens_release(&tokens);
    return 1;
  }
  abi_tokens_release(&tokens);
  ABI_FAIL("%s does not define %s", label, name);
}

static int abi_record_fields_equal(const abi_fields_t *left,
                                   const abi_fields_t *right) {
  size_t index;
  if (left->count != right->count) {
    return 0;
  }
  for (index = 0u; index < left->count; index++) {
    if (strcmp(left->items[index].name, right->items[index].name) != 0 ||
        strcmp(left->items[index].declaration,
               right->items[index].declaration) != 0) {
      return 0;
    }
  }
  return 1;
}

static unsigned int abi_align_up(unsigned int value,
                                 unsigned int alignment) {
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static int abi_record_layout(const abi_fields_t *fields,
                             abi_record_layout_t *layout) {
  unsigned int offset = 0u;
  unsigned int record_alignment = 1u;
  size_t index;
  layout->name_offset = 0xffffffffu;
  layout->value_offset = 0xffffffffu;
  layout->type_offset = 0xffffffffu;
  for (index = 0u; index < fields->count; index++) {
    const abi_field_t *field = &fields->items[index];
    unsigned int size;
    unsigned int alignment;
    unsigned int count = 1u;
    const char *declaration = field->declaration;
    const char *bracket = strchr(declaration, '[');
    if (strncmp(declaration, "char ", 5u) == 0 ||
        strncmp(declaration, "uint8_t ", 8u) == 0) {
      size = 1u;
      alignment = 1u;
    } else if (strncmp(declaration, "uint16_t ", 9u) == 0) {
      size = 2u;
      alignment = 2u;
    } else if (strncmp(declaration, "uint32_t ", 9u) == 0 ||
               strncmp(declaration, "int32_t ", 8u) == 0) {
      size = 4u;
      alignment = 4u;
    } else {
      ABI_FAIL("unsupported VFS record field: %s", declaration);
    }
    if (bracket != (const char *)0) {
      const char *bound = bracket + 1;
      while (*bound == ' ') {
        bound++;
      }
      if (strncmp(bound, "VFS_MAX_NAME", 12u) == 0) {
        count = 128u;
      } else {
        int parsed;
        const char *end = bound;
        while (*end != '\0' && *end != ' ') {
          end++;
        }
        if (!abi_parse_integer_text(bound, (size_t)(end - bound), &parsed) ||
            parsed < 0) {
          ABI_FAIL("unknown VFS array bound in %s", declaration);
        }
        count = (unsigned int)parsed;
      }
      size *= count;
    }
    offset = abi_align_up(offset, alignment);
    if (strcmp(field->name, "name") == 0) {
      layout->name_offset = offset;
    } else if (strcmp(field->name, "size") == 0) {
      layout->value_offset = offset;
    } else if (strcmp(field->name, "type") == 0) {
      layout->type_offset = offset;
    }
    offset += size;
    if (record_alignment < alignment) {
      record_alignment = alignment;
    }
  }
  layout->size = abi_align_up(offset, record_alignment);
  return 1;
}

static int abi_validate_records(const abi_file_t *kernel,
                                const abi_file_t *user,
                                abi_record_layout_t *dirent,
                                abi_record_layout_t *stat) {
  abi_fields_t kernel_fields;
  abi_fields_t user_fields;
  if (!abi_parse_record(kernel, "vfs_dirent_t", "kernel VFS header",
                        &kernel_fields) ||
      !abi_parse_record(user, "cupid_dirent_t", "user API header",
                        &user_fields)) {
    return 0;
  }
  if (!abi_record_fields_equal(&kernel_fields, &user_fields)) {
    ABI_FAIL("cupid_dirent_t does not match vfs_dirent_t");
  }
  if (!abi_record_layout(&kernel_fields, dirent)) {
    return 0;
  }
  if (!abi_parse_record(kernel, "vfs_stat_t", "kernel VFS header",
                        &kernel_fields) ||
      !abi_parse_record(user, "cupid_stat_t", "user API header",
                        &user_fields)) {
    return 0;
  }
  if (!abi_record_fields_equal(&kernel_fields, &user_fields)) {
    ABI_FAIL("cupid_stat_t does not match vfs_stat_t");
  }
  return abi_record_layout(&kernel_fields, stat);
}

static int abi_parse_assignments(const abi_file_t *file,
                                 abi_assignments_t *assignments) {
  abi_tokens_t tokens;
  size_t index;
  size_t opening = 0u;
  size_t closing;
  int found = 0;
  assignments->count = 0u;
  if (!abi_tokenize(file, &tokens)) {
    return 0;
  }
  for (index = 0u; index + 6u < tokens.count; index++) {
    if (abi_token_equal(&tokens.items[index], "void") &&
        abi_token_equal(&tokens.items[index + 1u], "syscall_init") &&
        abi_token_equal(&tokens.items[index + 2u], "(") &&
        abi_token_equal(&tokens.items[index + 3u], "void") &&
        abi_token_equal(&tokens.items[index + 4u], ")") &&
        abi_token_equal(&tokens.items[index + 5u], "{")) {
      opening = index + 5u;
      found = 1;
      break;
    }
  }
  if (!found || !abi_find_matching(&tokens, opening, "{", "}", &closing)) {
    abi_tokens_release(&tokens);
    ABI_FAIL("kernel does not define a complete syscall_init function");
  }
  index = opening + 1u;
  while (index < closing) {
    if (index + 3u < closing &&
        abi_token_equal(&tokens.items[index], "syscall_table") &&
        abi_token_equal(&tokens.items[index + 1u], ".") &&
        tokens.items[index + 2u].kind == ABI_TOKEN_IDENTIFIER &&
        abi_token_equal(&tokens.items[index + 3u], "=")) {
      size_t semicolon = index + 4u;
      int parentheses = 0;
      int brackets = 0;
      while (semicolon < closing) {
        if (abi_token_equal(&tokens.items[semicolon], "(")) {
          parentheses++;
        } else if (abi_token_equal(&tokens.items[semicolon], ")")) {
          parentheses--;
        } else if (abi_token_equal(&tokens.items[semicolon], "[")) {
          brackets++;
        } else if (abi_token_equal(&tokens.items[semicolon], "]")) {
          brackets--;
        } else if (abi_token_equal(&tokens.items[semicolon], ";") &&
                   parentheses == 0 && brackets == 0) {
          break;
        }
        semicolon++;
      }
      if (semicolon == closing || assignments->count == ABI_MAX_ASSIGNMENTS ||
          !abi_copy_token(&tokens.items[index + 2u],
                          assignments->items[assignments->count].name,
                          sizeof(assignments->items[0].name)) ||
          !abi_canonical_range(
              &tokens, index + 4u, semicolon,
              assignments->items[assignments->count].value,
              sizeof(assignments->items[0].value))) {
        abi_tokens_release(&tokens);
        ABI_FAIL("kernel syscall initializer is incomplete or too large");
      }
      assignments->count++;
      index = semicolon + 1u;
      continue;
    }
    index++;
  }
  abi_tokens_release(&tokens);
  return 1;
}

static const abi_assignment_t *abi_find_assignment(
    const abi_assignments_t *assignments, const char *name,
    unsigned int *matches) {
  const abi_assignment_t *result = (const abi_assignment_t *)0;
  size_t index;
  *matches = 0u;
  for (index = 0u; index < assignments->count; index++) {
    if (strcmp(assignments->items[index].name, name) == 0) {
      result = &assignments->items[index];
      (*matches)++;
    }
  }
  return result;
}

static int abi_digest_append(abi_digest_input_t *input, const char *text) {
  size_t size = strlen(text);
  if (size > sizeof(input->bytes) - input->size) {
    ABI_FAIL("ABI fingerprint input is too large");
  }
  (void)memcpy(input->bytes + input->size, text, size);
  input->size += size;
  return 1;
}

static uint32_t abi_rotate_right(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32u - count));
}

static void abi_sha256_block(uint32_t state[8],
                             const unsigned char block[64]) {
  static const uint32_t constants[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t index;
  for (index = 0u; index < 16u; index++) {
    uint32_t offset = index * 4u;
    words[index] = ((uint32_t)block[offset] << 24u) |
                   ((uint32_t)block[offset + 1u] << 16u) |
                   ((uint32_t)block[offset + 2u] << 8u) |
                   (uint32_t)block[offset + 3u];
  }
  for (index = 16u; index < 64u; index++) {
    uint32_t left = words[index - 15u];
    uint32_t right = words[index - 2u];
    uint32_t small_zero = abi_rotate_right(left, 7u) ^
                          abi_rotate_right(left, 18u) ^ (left >> 3u);
    uint32_t small_one = abi_rotate_right(right, 17u) ^
                         abi_rotate_right(right, 19u) ^ (right >> 10u);
    words[index] = words[index - 16u] + small_zero + words[index - 7u] +
                   small_one;
  }
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];
  for (index = 0u; index < 64u; index++) {
    uint32_t large_one = abi_rotate_right(e, 6u) ^
                         abi_rotate_right(e, 11u) ^
                         abi_rotate_right(e, 25u);
    uint32_t choose = (e & f) ^ ((~e) & g);
    uint32_t temporary_one =
        h + large_one + choose + constants[index] + words[index];
    uint32_t large_zero = abi_rotate_right(a, 2u) ^
                          abi_rotate_right(a, 13u) ^
                          abi_rotate_right(a, 22u);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temporary_two = large_zero + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary_one;
    d = c;
    c = b;
    b = a;
    a = temporary_one + temporary_two;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

static void abi_sha256(const unsigned char *contents, size_t size,
                       unsigned char digest[32]) {
  uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                       0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                       0x1f83d9abu, 0x5be0cd19u};
  unsigned char tail[128];
  size_t offset = 0u;
  size_t remaining;
  size_t tail_size;
  size_t index;
  uint64_t bit_length = (uint64_t)size * 8u;
  while (size - offset >= 64u) {
    abi_sha256_block(state, contents + offset);
    offset += 64u;
  }
  remaining = size - offset;
  (void)memset(tail, 0, sizeof(tail));
  (void)memcpy(tail, contents + offset, remaining);
  tail[remaining] = 0x80u;
  tail_size = remaining < 56u ? 64u : 128u;
  for (index = 0u; index < 8u; index++) {
    tail[tail_size - 1u - index] =
        (unsigned char)((bit_length >> (index * 8u)) & 0xffu);
  }
  abi_sha256_block(state, tail);
  if (tail_size == 128u) {
    abi_sha256_block(state, tail + 64u);
  }
  for (index = 0u; index < 8u; index++) {
    digest[index * 4u] = (unsigned char)((state[index] >> 24u) & 0xffu);
    digest[index * 4u + 1u] =
        (unsigned char)((state[index] >> 16u) & 0xffu);
    digest[index * 4u + 2u] =
        (unsigned char)((state[index] >> 8u) & 0xffu);
    digest[index * 4u + 3u] = (unsigned char)(state[index] & 0xffu);
  }
}

static void abi_digest_hex(const abi_digest_input_t *input, char output[65]) {
  static const char hex[] = "0123456789abcdef";
  unsigned char digest[32];
  size_t index;
  abi_sha256(input->bytes, input->size, digest);
  for (index = 0u; index < sizeof(digest); index++) {
    output[index * 2u] = hex[digest[index] >> 4u];
    output[index * 2u + 1u] = hex[digest[index] & 15u];
  }
  output[64] = '\0';
}

static int abi_validate_initializer(const abi_file_t *implementation,
                                    const abi_fields_t *fields,
                                    abi_report_t *report) {
  abi_assignments_t assignments;
  abi_digest_input_t digest;
  size_t index;
  unsigned int matches;
  const abi_assignment_t *assignment;
  char line[768];
  digest.size = 0u;
  if (!abi_parse_assignments(implementation, &assignments)) {
    return 0;
  }
  for (index = 0u; index < fields->count; index++) {
    assignment =
        abi_find_assignment(&assignments, fields->items[index].name, &matches);
    if (matches == 0u) {
      ABI_FAIL("missing initializer assignments: %s",
               fields->items[index].name);
    }
    if (matches != 1u) {
      ABI_FAIL("duplicate initializer assignments: %s",
               fields->items[index].name);
    }
    (void)assignment;
  }
  for (index = 0u; index < assignments.count; index++) {
    size_t field_index;
    int known = 0;
    for (field_index = 0u; field_index < fields->count; field_index++) {
      if (strcmp(assignments.items[index].name,
                 fields->items[field_index].name) == 0) {
        known = 1;
        break;
      }
    }
    if (!known) {
      ABI_FAIL("unknown initializer assignments: %s",
               assignments.items[index].name);
    }
  }
  assignment = abi_find_assignment(&assignments, "version", &matches);
  if (assignment == (const abi_assignment_t *)0 ||
      strcmp(assignment->value, "CUPID_SYSCALL_VERSION") != 0) {
    ABI_FAIL("kernel syscall version is not initialized from "
             "CUPID_SYSCALL_VERSION");
  }
  assignment = abi_find_assignment(&assignments, "table_size", &matches);
  if (assignment == (const abi_assignment_t *)0 ||
      strcmp(assignment->value,
             "( uint32_t ) sizeof ( cupid_syscall_table_t )") != 0) {
    ABI_FAIL("kernel syscall table size is not initialized from its type");
  }
  for (index = 2u; index < fields->count; index++) {
    const char *field = fields->items[index].name;
    assignment = abi_find_assignment(&assignments, field, &matches);
    if (assignment == (const abi_assignment_t *)0 ||
        !abi_is_identifier_start(assignment->value[0])) {
      ABI_FAIL("syscall provider for %s is not one identifier", field);
    }
    {
      size_t value_index;
      for (value_index = 1u; assignment->value[value_index] != '\0';
           value_index++) {
        if (!abi_is_identifier_continue(assignment->value[value_index])) {
          ABI_FAIL("syscall provider for %s is not one identifier: %s", field,
                   assignment->value);
        }
      }
    }
    if ((strcmp(field, "ntohs") == 0 &&
         strcmp(assignment->value, "htons") != 0) ||
        (strcmp(field, "ntohl") == 0 &&
         strcmp(assignment->value, "htonl") != 0)) {
      ABI_FAIL("syscall provider contract changed: %s uses %s", field,
               assignment->value);
    }
    if (snprintf(line, sizeof(line), "%s=%s\n", field, assignment->value) <
            0 ||
        !abi_digest_append(&digest, line)) {
      return 0;
    }
  }
  report->provider_count = (unsigned int)(fields->count - 2u);
  abi_digest_hex(&digest, report->provider_sha256);
  if (strcmp(report->provider_sha256, abi_expected_provider_digest) != 0) {
    ABI_FAIL("syscall provider contract changed without updating the reviewed "
             "ABI contract");
  }
  return 1;
}

static int abi_field_digest(const abi_fields_t *fields, char output[65]) {
  abi_digest_input_t digest;
  size_t index;
  char line[768];
  digest.size = 0u;
  for (index = 0u; index < fields->count; index++) {
    if (snprintf(line, sizeof(line), "%u:%s:%s\n", (unsigned int)index,
                 fields->items[index].name,
                 fields->items[index].declaration) < 0 ||
        !abi_digest_append(&digest, line)) {
      return 0;
    }
  }
  abi_digest_hex(&digest, output);
  return 1;
}

static int abi_validate_snapshot(abi_file_t files[ABI_INPUT_COUNT],
                                 abi_report_t *report) {
  abi_fields_t kernel_fields;
  abi_fields_t user_fields;
  abi_record_layout_t dirent;
  abi_record_layout_t stat;
  int kernel_version;
  int user_version;
  if (!abi_parse_syscall_table(&files[1], "kernel syscall header",
                               &kernel_fields) ||
      !abi_parse_syscall_table(&files[5], "user API header", &user_fields) ||
      !abi_compare_fields(&kernel_fields, &user_fields) ||
      !abi_validate_scalar_types(&files[0], &files[5])) {
    return 0;
  }
  if (!abi_integer_macro(&files[1], "CUPID_SYSCALL_VERSION",
                         "kernel syscall header", &kernel_version) ||
      !abi_integer_macro(&files[5], "CUPID_SYSCALL_VERSION",
                         "user API header", &user_version)) {
    return 0;
  }
  if (kernel_version != user_version) {
    ABI_FAIL("syscall version differs: kernel %d, user %d", kernel_version,
             user_version);
  }
  if (kernel_version != (int)ABI_EXPECTED_VERSION) {
    ABI_FAIL("syscall version %d is not the reviewed version %u",
             kernel_version, ABI_EXPECTED_VERSION);
  }
  if (!abi_validate_vfs_constants(&files[3], &files[5]) ||
      !abi_validate_network_constants(&files[4], &files[5]) ||
      !abi_validate_records(&files[3], &files[5], &dirent, &stat)) {
    return 0;
  }
  if (dirent.size != ABI_EXPECTED_DIRENT_SIZE) {
    ABI_FAIL("directory entry size %u is not the reviewed size %u",
             dirent.size, ABI_EXPECTED_DIRENT_SIZE);
  }
  if (stat.size != ABI_EXPECTED_STAT_SIZE) {
    ABI_FAIL("file status size %u is not the reviewed size %u", stat.size,
             ABI_EXPECTED_STAT_SIZE);
  }
  if (dirent.name_offset != 0u || dirent.value_offset != 128u ||
      dirent.type_offset != 132u) {
    ABI_FAIL("directory entry offsets changed");
  }
  if (stat.value_offset != 0u || stat.type_offset != 4u) {
    ABI_FAIL("file status offsets changed");
  }
  report->version = (unsigned int)kernel_version;
  report->field_count = (unsigned int)kernel_fields.count;
  report->table_size = report->field_count * 4u;
  report->dirent_size = dirent.size;
  report->dirent_name_offset = dirent.name_offset;
  report->dirent_value_offset = dirent.value_offset;
  report->dirent_type_offset = dirent.type_offset;
  report->stat_size = stat.size;
  report->stat_value_offset = stat.value_offset;
  report->stat_type_offset = stat.type_offset;
  if (!abi_validate_initializer(&files[2], &kernel_fields, report)) {
    return 0;
  }
  if (report->field_count != ABI_EXPECTED_FIELD_COUNT) {
    ABI_FAIL("syscall field count %u is not the reviewed count %u",
             report->field_count, ABI_EXPECTED_FIELD_COUNT);
  }
  if (report->table_size != ABI_EXPECTED_TABLE_SIZE) {
    ABI_FAIL("syscall table size %u is not the reviewed size %u",
             report->table_size, ABI_EXPECTED_TABLE_SIZE);
  }
  if (!abi_field_digest(&kernel_fields, report->abi_sha256)) {
    return 0;
  }
  if (strcmp(report->abi_sha256, abi_expected_digest) != 0) {
    ABI_FAIL("syscall field signatures changed without updating the reviewed "
             "ABI contract");
  }
  (void)snprintf(report->first_function, sizeof(report->first_function), "%s",
                 kernel_fields.items[2].name);
  (void)snprintf(report->last_function, sizeof(report->last_function), "%s",
                 kernel_fields.items[kernel_fields.count - 1u].name);
  return 1;
}

static int abi_reread_inputs(const char *root,
                             abi_file_t snapshots[ABI_INPUT_COUNT]) {
  size_t index;
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    abi_file_t current;
    if (!abi_read_file(root, abi_input_paths[index], &current)) {
      char detail[sizeof(abi_error)];
      (void)snprintf(detail, sizeof(detail), "%s", abi_error);
      ABI_FAIL("ABI input changed while checking: %s: %s",
               abi_input_paths[index], detail);
    }
    if (current.size != snapshots[index].size ||
        memcmp(current.bytes, snapshots[index].bytes, current.size) != 0) {
      abi_file_release(&current);
      ABI_FAIL("ABI input changed while checking: %s",
               abi_input_paths[index]);
    }
    abi_file_release(&current);
  }
  return 1;
}

static void abi_print_report(const abi_report_t *report) {
  (void)printf("{\"abi_sha256\": \"%s\", ", report->abi_sha256);
  (void)printf("\"dirent_offsets\": {\"name\": %u, \"size\": %u, "
               "\"type\": %u}, ",
               report->dirent_name_offset, report->dirent_value_offset,
               report->dirent_type_offset);
  (void)printf("\"dirent_size\": %u, \"field_count\": %u, ",
               report->dirent_size, report->field_count);
  (void)printf("\"first_function\": \"%s\", \"last_function\": \"%s\", ",
               report->first_function, report->last_function);
  (void)printf("\"provider_count\": %u, \"provider_sha256\": \"%s\", ",
               report->provider_count, report->provider_sha256);
  (void)printf("\"scalar_types\": {"
               "\"int32_t\": {\"bytes\": 4, \"signed\": true}, "
               "\"size_t\": {\"bytes\": 4, \"signed\": false}, "
               "\"uint16_t\": {\"bytes\": 2, \"signed\": false}, "
               "\"uint32_t\": {\"bytes\": 4, \"signed\": false}, "
               "\"uint8_t\": {\"bytes\": 1, \"signed\": false}}, ");
  (void)printf("\"schema\": \"cupid.user-syscall-abi.v1\", ");
  (void)printf("\"stat_offsets\": {\"size\": %u, \"type\": %u}, "
               "\"stat_size\": %u, \"table_size\": %u, \"version\": %u}\n",
               report->stat_value_offset, report->stat_type_offset,
               report->stat_size, report->table_size, report->version);
}

static int abi_run_check(const char *snapshot_root, const char *reread_root) {
  abi_file_t files[ABI_INPUT_COUNT];
  abi_report_t report;
  size_t index;
  int ok = 1;
  (void)memset(files, 0, sizeof(files));
  (void)memset(&report, 0, sizeof(report));
  abi_error[0] = '\0';
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    if (!abi_read_file(snapshot_root, abi_input_paths[index], &files[index])) {
      ok = 0;
      break;
    }
  }
  if (ok) {
    ok = abi_validate_snapshot(files, &report);
  }
  if (ok) {
    ok = abi_reread_inputs(reread_root, files);
  }
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    abi_file_release(&files[index]);
  }
  if (!ok) {
    (void)fprintf(stderr, "Cupid user ABI contract failed: %s\n", abi_error);
    return 1;
  }
  abi_print_report(&report);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "check") == 0) {
    return abi_run_check(argv[2], argv[2]);
  }
  if (argc == 4 && strcmp(argv[1], "check-snapshot") == 0) {
    return abi_run_check(argv[2], argv[3]);
  }
  (void)fprintf(stderr,
                "usage: user-syscall-abi-contract check ROOT | "
                "check-snapshot SNAPSHOT_ROOT REREAD_ROOT\n");
  return 2;
}
