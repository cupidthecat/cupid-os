#ifndef CUPID_USER_SYSCALL_ABI_H
#define CUPID_USER_SYSCALL_ABI_H

#include <stddef.h>

#define CUPID_USER_ABI_INPUT_COUNT 6u
#define CUPID_USER_ABI_SOURCE_BYTES (1024u * 1024u)
#define CUPID_USER_ABI_ERROR_BYTES 1024u
#define CUPID_USER_ABI_JSON_BYTES 2048u

typedef struct {
  const unsigned char *bytes;
  size_t size;
} cupid_user_abi_input_t;

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
} cupid_user_abi_report_t;

/* Inputs follow the six paths returned below. Borrowed payloads need no trailing
 * NUL and remain unmodified. Each source is bounded, valid UTF-8 without NULs.
 * Validation owns no filesystem state. Writable result and diagnostic storage
 * must be disjoint from each other and from the inputs. Failure clears result;
 * diagnostics are optional, caller-owned and bounded. Calls share no mutable
 * state. A successful result retains the reviewed version-5 i386 ABI. */
const char *cupid_user_abi_input_path(size_t index);
int cupid_user_abi_validate(
    const cupid_user_abi_input_t inputs[CUPID_USER_ABI_INPUT_COUNT],
    cupid_user_abi_report_t *result, char *error, size_t error_size);

/* Format a successfully validated report with the existing JSON schema.
 * Failure clears nonempty writable output storage. */
int cupid_user_abi_format_json(const cupid_user_abi_report_t *report,
                               char *output, size_t capacity);

#endif
