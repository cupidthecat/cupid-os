#ifndef CUPID_CONTRACT_PARSE_INTERNAL_H
#define CUPID_CONTRACT_PARSE_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
#define SEED_ARTIFACT_COUNT 6u
#define JSON_MAX_DEPTH 64u
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
  char *bytes;
  size_t capacity;
  int has_error;
} error_context_t;

extern const char *const cupid_contract_seed_names[SEED_ARTIFACT_COUNT];
extern const char *const cupid_contract_seed_files[SEED_ARTIFACT_COUNT];
int cupid_contract_set_error(error_context_t *context, const char *message);
void cupid_contract_text_release(text_t *text);
int cupid_contract_text_equals_literal(const text_t *text, const char *literal);
int cupid_contract_slice_equals_text(const byte_slice_t *slice, const text_t *text);
int cupid_contract_text_compare(const text_t *left, const text_t *right);
void cupid_contract_json_skip_space(json_reader_t *reader);
int cupid_contract_json_take(error_context_t *context, json_reader_t *reader, unsigned char expected);
int cupid_contract_hex_value(unsigned char value);
int cupid_contract_json_hex_quad(error_context_t *context, json_reader_t *reader, uint32_t *value);
int cupid_contract_utf8_sequence_size(const unsigned char *bytes, size_t remaining,
                              size_t *sequence_size);
int cupid_contract_utf8_valid(const unsigned char *bytes, size_t size);
int cupid_contract_json_append_codepoint(unsigned char *output, size_t *written,
                                 uint32_t codepoint);
int cupid_contract_json_parse_string(error_context_t *context, json_reader_t *reader, text_t *result);
int cupid_contract_json_match_literal(json_reader_t *reader, const char *literal);
int cupid_contract_json_skip_number(error_context_t *context, json_reader_t *reader);
int cupid_contract_json_skip_array(error_context_t *context, json_reader_t *reader, unsigned int depth);
int cupid_contract_json_skip_object(error_context_t *context, json_reader_t *reader, unsigned int depth);
int cupid_contract_json_skip_value(error_context_t *context, json_reader_t *reader, unsigned int depth);
int cupid_contract_json_parse_positive_u64(error_context_t *context, json_reader_t *reader, uint64_t *result);
int cupid_contract_json_finish(error_context_t *context, json_reader_t *reader);
int cupid_contract_logical_path_valid(const unsigned char *bytes, size_t size);
int cupid_contract_seed_index_for_name(const text_t *name);
int cupid_contract_binary_read_u32(error_context_t *context, binary_reader_t *reader, uint32_t *value);
int cupid_contract_binary_read_u64(error_context_t *context, binary_reader_t *reader, uint64_t *value);
int cupid_contract_binary_read_slice(error_context_t *context, binary_reader_t *reader, byte_slice_t *slice);
#endif
