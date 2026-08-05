#include "cupidobj.h"

#define CTOOL_OBJ_WRAP_FLAGS                                                \
  (CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC |                        \
   CTOOL_ELF32_SHF_EXECINSTR | CTOOL_ELF32_SHF_TLS |                      \
   CTOOL_ELF32_SHF_EXCLUDE)
#define CTOOL_OBJ_INSTALL_PATH_LIMIT 512u
#define CTOOL_OBJ_DISK_SECTOR_BYTES 512u
#define CTOOL_OBJ_DISK_BOOT_SECTORS 5u
#define CTOOL_OBJ_DISK_MBR_BOOT_BYTES 446u
#define CTOOL_OBJ_DISK_ROOT_ENTRIES 512u
#define CTOOL_OBJ_DISK_RESERVED_SECTORS 1u
#define CTOOL_OBJ_DISK_FAT_COPIES 2u
#define CTOOL_OBJ_DISK_FAT16_MIN_CLUSTERS 4085u
#define CTOOL_OBJ_DISK_FAT16_MAX_CLUSTERS 65525u
#define CTOOL_OBJ_ISO_ENTRY_LIMIT 512u
#define CTOOL_OBJ_ISO_BLOCK_BYTES 2048u
#define CTOOL_OBJ_ISO_MAX_DIRECTORY_DEPTH 8u
#define CTOOL_OBJ_ISO_IDENTIFIER_BYTES 14u
#define CTOOL_OBJ_ISO_ER_BYTES 237u

typedef struct {
  ctool_u32 address;
  ctool_u32 order;
  ctool_bytes_t contents;
} obj_flat_region_t;

typedef enum {
  OBJ_INSTALL_SYMBOL_BIN = 1,
  OBJ_INSTALL_SYMBOL_HEADER,
  OBJ_INSTALL_SYMBOL_BROWSER,
  OBJ_INSTALL_SYMBOL_CTXT,
  OBJ_INSTALL_SYMBOL_DOC_ASSET,
  OBJ_INSTALL_SYMBOL_HOME_ASSET
} obj_install_symbol_kind_t;

typedef struct {
  const char *prefix;
  ctool_string_t stem;
  const char *suffix;
  ctool_string_t path;
  obj_install_symbol_kind_t kind;
} obj_install_symbol_t;

typedef struct {
  ctool_u32 address;
  ctool_u32 order;
  ctool_string_t name;
} obj_ksyms_symbol_t;

typedef struct {
  ctool_u32 sectors_per_cluster;
  ctool_u32 root_dir_sectors;
  ctool_u32 sectors_per_fat;
} obj_disk_layout_t;

typedef struct {
  const ctool_obj_iso_fixture_entry_t *entry;
  ctool_string_t path;
  ctool_string_t name;
  ctool_u32 parent;
  ctool_u32 extent;
  ctool_u32 size;
  ctool_u32 directory_number;
  ctool_u32 child_start;
  ctool_u32 child_count;
  ctool_u32 identifier_size;
  ctool_u8 identifier[CTOOL_OBJ_ISO_IDENTIFIER_BYTES];
  ctool_bool directory;
} obj_iso_node_t;

typedef enum {
  OBJ_KSYMS_ROW_EMPTY = 0,
  OBJ_KSYMS_ROW_IGNORED,
  OBJ_KSYMS_ROW_TEXT,
  OBJ_KSYMS_ROW_OMITTED_ADDRESS,
  OBJ_KSYMS_ROW_MALFORMED,
  OBJ_KSYMS_ROW_INVALID_ADDRESS,
  OBJ_KSYMS_ROW_ADDRESS_OUTSIDE_I386
} obj_ksyms_row_kind_t;

static ctool_u32 obj_disk_decimal(char *destination, ctool_u32 value);

static void obj_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool obj_string_valid(ctool_string_t string) {
  ctool_u32 index;
  if (string.data == (const char *)0 || string.size == 0u) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < string.size; index++) {
    if (string.data[index] == '\0') {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_string_equal(ctool_string_t left,
                                    ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_u8 obj_install_symbol_character(
    const obj_install_symbol_t *symbol, ctool_u64 index) {
  ctool_string_t prefix = ctool_string(symbol->prefix);
  ctool_string_t suffix = ctool_string(symbol->suffix);
  ctool_u8 character;
  if (index < (ctool_u64)prefix.size) {
    return (ctool_u8)prefix.data[(ctool_u32)index];
  }
  index -= (ctool_u64)prefix.size;
  if (index < (ctool_u64)symbol->stem.size) {
    character = (ctool_u8)symbol->stem.data[(ctool_u32)index];
    return character == (ctool_u8)'-' ? (ctool_u8)'_' : character;
  }
  index -= (ctool_u64)symbol->stem.size;
  return (ctool_u8)suffix.data[(ctool_u32)index];
}

static ctool_bool obj_install_symbols_equal(
    const obj_install_symbol_t *left,
    const obj_install_symbol_t *right) {
  ctool_string_t left_prefix = ctool_string(left->prefix);
  ctool_string_t left_suffix = ctool_string(left->suffix);
  ctool_string_t right_prefix = ctool_string(right->prefix);
  ctool_string_t right_suffix = ctool_string(right->suffix);
  ctool_u64 left_size = (ctool_u64)left_prefix.size +
                        (ctool_u64)left->stem.size +
                        (ctool_u64)left_suffix.size;
  ctool_u64 right_size = (ctool_u64)right_prefix.size +
                         (ctool_u64)right->stem.size +
                         (ctool_u64)right_suffix.size;
  ctool_u64 index;
  if (left_size != right_size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left_size; index++) {
    if (obj_install_symbol_character(left, index) !=
        obj_install_symbol_character(right, index)) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_string_has_prefix(ctool_string_t string,
                                        const char *prefix) {
  ctool_string_t prefix_string = ctool_string(prefix);
  ctool_u32 index;
  if (string.size < prefix_string.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < prefix_string.size; index++) {
    if (string.data[index] != prefix_string.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_string_has_suffix(ctool_string_t string,
                                        const char *suffix) {
  ctool_string_t suffix_string = ctool_string(suffix);
  ctool_u32 index;
  if (string.size < suffix_string.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < suffix_string.size; index++) {
    if (string.data[string.size - suffix_string.size + index] !=
        suffix_string.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t obj_append_literal(ctool_buffer_t *output,
                                         const char *text) {
  ctool_string_t string = ctool_string(text);
  return ctool_buffer_append(output, ctool_bytes(string.data, string.size));
}

static ctool_status_t obj_append_string(ctool_buffer_t *output,
                                        ctool_string_t string) {
  return ctool_buffer_append(output, ctool_bytes(string.data, string.size));
}

static ctool_bool obj_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_status_t obj_emit_failure_at(ctool_job_t *job,
                                           const ctool_source_t *source,
                                           ctool_status_t status,
                                           ctool_u32 code,
                                           ctool_u32 line,
                                           ctool_u32 column,
                                           const char *message) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t diagnostic_status;
  if (job == (ctool_job_t *)0) {
    return status;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = source != (const ctool_source_t *)0
                        ? source->path.text
                        : ctool_string("");
  diagnostic.line = line;
  diagnostic.column = column;
  diagnostic.message = ctool_string(message);
  diagnostic_status = ctool_job_emit(job, &diagnostic);
  return diagnostic_status == CTOOL_OK ? status : diagnostic_status;
}

static ctool_status_t obj_emit_failure(ctool_job_t *job,
                                        const ctool_source_t *source,
                                        ctool_status_t status,
                                        ctool_u32 code,
                                        ctool_u32 column,
                                        const char *message) {
  return obj_emit_failure_at(job, source, status, code, 0u, column, message);
}

static ctool_bool obj_region_less(const obj_flat_region_t *left,
                                  const obj_flat_region_t *right) {
  return left->address < right->address ||
                 (left->address == right->address && left->order < right->order)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static void obj_region_swap(obj_flat_region_t *left,
                            obj_flat_region_t *right) {
  obj_flat_region_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void obj_region_sift_down(obj_flat_region_t *regions,
                                 ctool_u32 root, ctool_u32 count) {
  for (;;) {
    ctool_u32 child;
    ctool_u32 selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (obj_region_less(&regions[selected], &regions[child]) == CTOOL_TRUE) {
      selected = child;
    }
    if (child + 1u < count &&
        obj_region_less(&regions[selected], &regions[child + 1u]) ==
            CTOOL_TRUE) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    obj_region_swap(&regions[root], &regions[selected]);
    root = selected;
  }
}

static void obj_region_sort(obj_flat_region_t *regions, ctool_u32 count) {
  ctool_u32 start = count / 2u;
  ctool_u32 end = count;
  while (start != 0u) {
    start--;
    obj_region_sift_down(regions, start, count);
  }
  while (end > 1u) {
    obj_region_swap(&regions[0], &regions[end - 1u]);
    end--;
    obj_region_sift_down(regions, 0u, end);
  }
}

static ctool_bool obj_ksyms_space(ctool_u8 character) {
  return character == (ctool_u8)' ' || character == (ctool_u8)'\t' ||
                 character == (ctool_u8)'\r' ||
                 character == (ctool_u8)'\v' ||
                 character == (ctool_u8)'\f'
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool obj_ksyms_token_equal(ctool_string_t token,
                                         const char *text) {
  return obj_string_equal(token, ctool_string(text));
}

static obj_ksyms_row_kind_t obj_ksyms_address(
    ctool_string_t spelling, ctool_u32 *address_out) {
  ctool_u32 value = 0u;
  ctool_u32 index = 0u;
  if (spelling.size >= 2u && spelling.data[0] == '0' &&
      (spelling.data[1] == 'x' || spelling.data[1] == 'X')) {
    index = 2u;
  }
  if (index == spelling.size) {
    return OBJ_KSYMS_ROW_INVALID_ADDRESS;
  }
  while (index < spelling.size) {
    unsigned char character = (unsigned char)spelling.data[index];
    ctool_u32 digit;
    if (character >= (unsigned char)'0' &&
        character <= (unsigned char)'9') {
      digit = (ctool_u32)(character - (unsigned char)'0');
    } else if (character >= (unsigned char)'a' &&
               character <= (unsigned char)'f') {
      digit = 10u + (ctool_u32)(character - (unsigned char)'a');
    } else if (character >= (unsigned char)'A' &&
               character <= (unsigned char)'F') {
      digit = 10u + (ctool_u32)(character - (unsigned char)'A');
    } else {
      return OBJ_KSYMS_ROW_INVALID_ADDRESS;
    }
    if (value > (0xffffffffu - digit) / 16u) {
      return OBJ_KSYMS_ROW_ADDRESS_OUTSIDE_I386;
    }
    value = value * 16u + digit;
    index++;
  }
  *address_out = value;
  return OBJ_KSYMS_ROW_TEXT;
}

static obj_ksyms_row_kind_t obj_ksyms_parse_row(
    ctool_bytes_t contents, ctool_u32 start, ctool_u32 end,
    ctool_u32 order, obj_ksyms_symbol_t *symbol_out) {
  ctool_string_t fields[3];
  ctool_u32 field_count = 0u;
  ctool_u32 index = start;
  ctool_u32 address = 0u;
  obj_ksyms_row_kind_t address_kind;
  while (index < end) {
    ctool_u32 field_start;
    while (index < end && obj_ksyms_space(contents.data[index]) == CTOOL_TRUE) {
      index++;
    }
    if (index == end) {
      break;
    }
    if (field_count == 3u) {
      return OBJ_KSYMS_ROW_MALFORMED;
    }
    field_start = index;
    while (index < end && obj_ksyms_space(contents.data[index]) == CTOOL_FALSE) {
      if (contents.data[index] == 0u) {
        return OBJ_KSYMS_ROW_MALFORMED;
      }
      index++;
    }
    fields[field_count].data =
        (const char *)(const void *)(contents.data + field_start);
    fields[field_count].size = index - field_start;
    field_count++;
  }
  if (field_count == 0u) {
    return OBJ_KSYMS_ROW_EMPTY;
  }
  if (field_count == 2u) {
    if (fields[0].size == 1u &&
        (obj_ksyms_token_equal(fields[0], "U") == CTOOL_TRUE ||
         obj_ksyms_token_equal(fields[0], "u") == CTOOL_TRUE ||
         obj_ksyms_token_equal(fields[0], "v") == CTOOL_TRUE ||
         obj_ksyms_token_equal(fields[0], "w") == CTOOL_TRUE)) {
      return OBJ_KSYMS_ROW_IGNORED;
    }
    return OBJ_KSYMS_ROW_OMITTED_ADDRESS;
  }
  if (field_count != 3u || fields[1].size != 1u) {
    return OBJ_KSYMS_ROW_MALFORMED;
  }
  address_kind = obj_ksyms_address(fields[0], &address);
  if (address_kind != OBJ_KSYMS_ROW_TEXT) {
    return address_kind;
  }
  if (!(fields[1].data[0] == 't' || fields[1].data[0] == 'T' ||
        fields[1].data[0] == 'w' || fields[1].data[0] == 'W')) {
    return OBJ_KSYMS_ROW_IGNORED;
  }
  if (obj_string_has_prefix(fields[2], ".L") == CTOOL_TRUE) {
    return OBJ_KSYMS_ROW_IGNORED;
  }
  symbol_out->address = address;
  symbol_out->order = order;
  symbol_out->name = fields[2];
  return OBJ_KSYMS_ROW_TEXT;
}

static ctool_bool obj_ksyms_symbol_less(
    const obj_ksyms_symbol_t *left, const obj_ksyms_symbol_t *right) {
  return left->address < right->address ||
                 (left->address == right->address &&
                  left->order < right->order)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static void obj_ksyms_symbol_swap(obj_ksyms_symbol_t *left,
                                   obj_ksyms_symbol_t *right) {
  obj_ksyms_symbol_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void obj_ksyms_symbol_sift_down(obj_ksyms_symbol_t *symbols,
                                        ctool_u32 root,
                                        ctool_u32 count) {
  for (;;) {
    ctool_u32 child;
    ctool_u32 selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (obj_ksyms_symbol_less(&symbols[selected], &symbols[child]) ==
        CTOOL_TRUE) {
      selected = child;
    }
    if (child + 1u < count &&
        obj_ksyms_symbol_less(&symbols[selected], &symbols[child + 1u]) ==
            CTOOL_TRUE) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    obj_ksyms_symbol_swap(&symbols[root], &symbols[selected]);
    root = selected;
  }
}

static void obj_ksyms_symbol_sort(obj_ksyms_symbol_t *symbols,
                                   ctool_u32 count) {
  ctool_u32 start = count / 2u;
  ctool_u32 end = count;
  while (start != 0u) {
    start--;
    obj_ksyms_symbol_sift_down(symbols, start, count);
  }
  while (end > 1u) {
    obj_ksyms_symbol_swap(&symbols[0], &symbols[end - 1u]);
    end--;
    obj_ksyms_symbol_sift_down(symbols, 0u, end);
  }
}

static void obj_ksyms_write_le32(ctool_u8 *bytes, ctool_u32 offset,
                                  ctool_u32 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (ctool_u8)((value >> 24u) & 0xffu);
}

static ctool_status_t obj_ksyms_append_word(ctool_buffer_t *output,
                                             ctool_u32 value) {
  static const char digits[] = "0123456789abcdef";
  char text[12];
  ctool_u32 index;
  text[0] = '0';
  text[1] = 'x';
  for (index = 0u; index < 8u; index++) {
    ctool_u32 shift = (7u - index) * 4u;
    text[2u + index] = digits[(value >> shift) & 0x0fu];
  }
  text[10] = 'u';
  text[11] = ',';
  return ctool_buffer_append(output, ctool_bytes(text, 12u));
}

static ctool_status_t obj_ksyms_append_decimal(ctool_buffer_t *output,
                                                ctool_u32 value) {
  char reverse[10];
  char text[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    text[index] = reverse[count - index - 1u];
  }
  return ctool_buffer_append(output, ctool_bytes(text, count));
}

static ctool_status_t obj_ksyms_emit_source(ctool_buffer_t *output,
                                             const ctool_u8 *blob,
                                             ctool_u32 blob_size,
                                             ctool_u32 padded_size) {
  ctool_u32 offset;
  ctool_status_t status = obj_append_literal(
      output, "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
              "#include \"ksyms.h\"\n\n"
              "/* i386 words preserve the blob bytes with fewer initializers. */\n"
              "const unsigned int\n"
              "__attribute__((section(\".ksyms\"), used, aligned(4)))\n"
              "ksym_blob[] = {\n");
  for (offset = 0u; status == CTOOL_OK && offset < padded_size;
       offset += 4u) {
    ctool_u32 value = (ctool_u32)blob[offset] |
                      ((ctool_u32)blob[offset + 1u] << 8u) |
                      ((ctool_u32)blob[offset + 2u] << 16u) |
                      ((ctool_u32)blob[offset + 3u] << 24u);
    ctool_u32 word_index = offset / 4u;
    if (word_index % 8u == 0u) {
      status = obj_append_literal(output, "  ");
    } else {
      status = obj_append_literal(output, " ");
    }
    if (status == CTOOL_OK) {
      status = obj_ksyms_append_word(output, value);
    }
    if (status == CTOOL_OK &&
        (word_index % 8u == 7u || offset + 4u == padded_size)) {
      status = obj_append_literal(output, "\n");
    }
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "};\n\nconst unsigned int ksym_blob_size = ");
  }
  if (status == CTOOL_OK) {
    status = obj_ksyms_append_decimal(output, blob_size);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "u;\n");
  }
  return status;
}

static ctool_status_t obj_ksyms_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_status_t status, ctool_u32 code,
    ctool_u32 line, const char *message) {
  ctool_status_t rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  return obj_emit_failure_at(job, source, status, code, line, 0u, message);
}

static ctool_status_t obj_ksyms_row_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, obj_ksyms_row_kind_t kind, ctool_u32 line) {
  if (kind == OBJ_KSYMS_ROW_OMITTED_ADDRESS) {
    return obj_ksyms_failure(
        job, source, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, line,
        "CupidObj symbol reader omitted an address");
  }
  if (kind == OBJ_KSYMS_ROW_INVALID_ADDRESS) {
    return obj_ksyms_failure(
        job, source, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, line,
        "CupidObj symbol reader emitted an invalid address");
  }
  if (kind == OBJ_KSYMS_ROW_ADDRESS_OUTSIDE_I386) {
    return obj_ksyms_failure(
        job, source, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW, line,
        "CupidObj symbol reader address is outside i386");
  }
  return obj_ksyms_failure(
      job, source, arena, mark, CTOOL_ERR_INPUT,
      CTOOL_OBJ_DIAG_INVALID_INPUT, line,
      "CupidObj symbol reader emitted a malformed row");
}

static ctool_status_t obj_ksyms_source(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_bytes_t contents = request->input->contents;
  obj_ksyms_symbol_t *symbols = (obj_ksyms_symbol_t *)0;
  ctool_u8 *blob = (ctool_u8 *)0;
  ctool_u32 symbol_count = 0u;
  ctool_u32 unique_count = 0u;
  ctool_u32 offset = 0u;
  ctool_u32 line = 1u;
  ctool_u32 index;
  ctool_u32 string_offset;
  ctool_u32 blob_size;
  ctool_u32 padded_size;
  ctool_u32 string_cursor;
  ctool_status_t status;
  ctool_status_t rewind_status;
  if (contents.data == (const ctool_u8 *)0 && contents.size != 0u) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
        "CupidObj input bytes are invalid");
  }
  while (offset < contents.size) {
    ctool_u32 start = offset;
    obj_ksyms_symbol_t symbol;
    obj_ksyms_row_kind_t kind;
    while (offset < contents.size && contents.data[offset] != (ctool_u8)'\n') {
      offset++;
    }
    kind = obj_ksyms_parse_row(contents, start, offset, symbol_count, &symbol);
    if (kind == OBJ_KSYMS_ROW_TEXT) {
      symbol_count++;
    } else if (kind != OBJ_KSYMS_ROW_EMPTY &&
               kind != OBJ_KSYMS_ROW_IGNORED) {
      return obj_ksyms_row_failure(job, request->input, arena, mark, kind,
                                   line);
    }
    if (offset < contents.size) {
      offset++;
      line++;
    }
  }
  if (symbol_count == 0u) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_NO_LOAD, 0u,
        "CupidObj symbol reader reported no kernel text symbols");
  }
  status = ctool_arena_alloc_zero(
      arena, symbol_count, (ctool_u32)sizeof(*symbols),
      (ctool_u32)sizeof(void *), (void **)&symbols);
  if (status != CTOOL_OK) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj kernel symbol inventory exceeds its arena limit");
  }
  offset = 0u;
  index = 0u;
  line = 1u;
  while (offset < contents.size) {
    ctool_u32 start = offset;
    obj_ksyms_symbol_t symbol;
    obj_ksyms_row_kind_t kind;
    while (offset < contents.size && contents.data[offset] != (ctool_u8)'\n') {
      offset++;
    }
    kind = obj_ksyms_parse_row(contents, start, offset, index, &symbol);
    if (kind == OBJ_KSYMS_ROW_TEXT) {
      symbols[index] = symbol;
      index++;
    } else if (kind != OBJ_KSYMS_ROW_EMPTY &&
               kind != OBJ_KSYMS_ROW_IGNORED) {
      return obj_ksyms_row_failure(job, request->input, arena, mark, kind,
                                   line);
    }
    if (offset < contents.size) {
      offset++;
      line++;
    }
  }
  obj_ksyms_symbol_sort(symbols, symbol_count);
  for (index = 0u; index < symbol_count; index++) {
    if (unique_count == 0u ||
        symbols[index].address != symbols[unique_count - 1u].address) {
      symbols[unique_count] = symbols[index];
      unique_count++;
    }
  }
  if (unique_count > (0xffffffffu - 16u) / 8u) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
        CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj kernel symbol table size overflows i386");
  }
  string_offset = 16u + unique_count * 8u;
  blob_size = string_offset;
  for (index = 0u; index < unique_count; index++) {
    if (symbols[index].name.size == 0xffffffffu ||
        blob_size > 0xffffffffu - symbols[index].name.size - 1u) {
      return obj_ksyms_failure(
          job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj kernel symbol strings overflow i386");
    }
    blob_size += symbols[index].name.size + 1u;
  }
  if (blob_size > 0xfffffffcu) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
        CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj word-packed kernel symbol source overflows i386");
  }
  padded_size = (blob_size + 3u) & ~3u;
  status = ctool_arena_alloc_zero(arena, padded_size, 1u, 4u,
                                  (void **)&blob);
  if (status != CTOOL_OK) {
    return obj_ksyms_failure(
        job, request->input, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj kernel symbol blob exceeds its arena limit");
  }
  obj_ksyms_write_le32(blob, 0u, 0x4d59534bu);
  obj_ksyms_write_le32(blob, 4u, unique_count);
  obj_ksyms_write_le32(blob, 8u, string_offset);
  obj_ksyms_write_le32(blob, 12u, blob_size);
  string_cursor = string_offset;
  for (index = 0u; index < unique_count; index++) {
    ctool_u32 name_index;
    obj_ksyms_write_le32(blob, 16u + index * 8u, symbols[index].address);
    obj_ksyms_write_le32(blob, 20u + index * 8u,
                         string_cursor - string_offset);
    for (name_index = 0u; name_index < symbols[index].name.size;
         name_index++) {
      blob[string_cursor + name_index] =
          (ctool_u8)symbols[index].name.data[name_index];
    }
    string_cursor += symbols[index].name.size + 1u;
  }
  status = obj_ksyms_emit_source(output, blob, blob_size, padded_size);
  rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  if (status != CTOOL_OK) {
    ctool_u32 code = status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                             status == CTOOL_ERR_NO_MEMORY
                         ? CTOOL_OBJ_DIAG_LIMIT
                         : CTOOL_OBJ_DIAG_OUTPUT;
    return obj_emit_failure(job, request->input, status, code, 0u,
                            "CupidObj could not emit kernel symbol source");
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_iso_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_status_t status, ctool_u32 code,
    ctool_u32 line, const char *message) {
  ctool_status_t emitted = obj_emit_failure_at(
      job, source, status, code, line, 0u, message);
  ctool_status_t rewound = ctool_arena_rewind(arena, mark);
  return rewound == CTOOL_OK ? emitted : rewound;
}

static ctool_u8 obj_iso_fold(ctool_u8 character) {
  if (character >= (ctool_u8)'A' && character <= (ctool_u8)'Z') {
    return (ctool_u8)(character + ((ctool_u8)'a' - (ctool_u8)'A'));
  }
  return character;
}

static ctool_i32 obj_iso_string_order(ctool_string_t left,
                                       ctool_string_t right) {
  ctool_u32 common = left.size < right.size ? left.size : right.size;
  ctool_u32 index;
  for (index = 0u; index < common; index++) {
    ctool_u8 left_byte = obj_iso_fold((ctool_u8)left.data[index]);
    ctool_u8 right_byte = obj_iso_fold((ctool_u8)right.data[index]);
    if (left_byte != right_byte) {
      return left_byte < right_byte ? -1 : 1;
    }
  }
  if (left.size != right.size) {
    return left.size < right.size ? -1 : 1;
  }
  for (index = 0u; index < left.size; index++) {
    ctool_u8 left_byte = (ctool_u8)left.data[index];
    ctool_u8 right_byte = (ctool_u8)right.data[index];
    if (left_byte != right_byte) {
      return left_byte < right_byte ? -1 : 1;
    }
  }
  return 0;
}

static ctool_bool obj_iso_string_equal_folded(ctool_string_t left,
                                               ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (obj_iso_fold((ctool_u8)left.data[index]) !=
        obj_iso_fold((ctool_u8)right.data[index])) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_iso_component_character(ctool_u8 character) {
  return ((character >= (ctool_u8)'a' && character <= (ctool_u8)'z') ||
          (character >= (ctool_u8)'A' && character <= (ctool_u8)'Z') ||
          (character >= (ctool_u8)'0' && character <= (ctool_u8)'9') ||
          character == (ctool_u8)'.' || character == (ctool_u8)'_' ||
          character == (ctool_u8)'-')
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool obj_iso_path_shape(ctool_string_t path,
                                     ctool_bool directory,
                                     ctool_string_t *name_out) {
  ctool_u32 component_start = 0u;
  ctool_u32 components = 0u;
  ctool_u32 index;
  if (path.data == (const char *)0 || path.size == 0u ||
      path.data[0] == '/' || path.data[path.size - 1u] == '/') {
    return CTOOL_FALSE;
  }
  for (index = 0u; index <= path.size; index++) {
    if (index == path.size || path.data[index] == '/') {
      ctool_u32 component_size = index - component_start;
      if (component_size == 0u || component_size > 127u ||
          (component_size == 1u && path.data[component_start] == '.') ||
          (component_size == 2u && path.data[component_start] == '.' &&
           path.data[component_start + 1u] == '.')) {
        return CTOOL_FALSE;
      }
      components++;
      if (index == path.size) {
        name_out->data = path.data + component_start;
        name_out->size = component_size;
      }
      component_start = index + 1u;
      continue;
    }
    if (obj_iso_component_character((ctool_u8)path.data[index]) ==
        CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  if ((directory == CTOOL_TRUE &&
       components >= CTOOL_OBJ_ISO_MAX_DIRECTORY_DEPTH) ||
      (directory == CTOOL_FALSE &&
       components > CTOOL_OBJ_ISO_MAX_DIRECTORY_DEPTH)) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_iso_path_parent(ctool_string_t path,
                                      ctool_string_t *parent_out) {
  ctool_u32 index = path.size;
  while (index != 0u) {
    index--;
    if (path.data[index] == '/') {
      parent_out->data = path.data;
      parent_out->size = index;
      return CTOOL_TRUE;
    }
  }
  parent_out->data = path.data;
  parent_out->size = 0u;
  return CTOOL_FALSE;
}

static ctool_status_t obj_iso_validate_manifest(
    ctool_job_t *job, const ctool_source_t *manifest,
    obj_iso_node_t *nodes, ctool_u32 node_count, ctool_u8 *seen,
    ctool_arena_t *arena, ctool_arena_mark_t mark) {
  ctool_bytes_t contents = manifest->contents;
  ctool_u32 offset = 0u;
  ctool_u32 line = 1u;
  ctool_u32 matched = 0u;
  if (contents.data == (const ctool_u8 *)0 || contents.size == 0u) {
    return obj_iso_failure(
        job, manifest, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
        "CupidObj ISO fixture manifest is empty or invalid");
  }
  while (offset < contents.size) {
    ctool_u32 start = offset;
    ctool_u32 end;
    ctool_string_t path;
    ctool_string_t name;
    ctool_u32 index;
    ctool_u32 exact = 0xffffffffu;
    while (offset < contents.size && contents.data[offset] != (ctool_u8)'\n') {
      offset++;
    }
    end = offset;
    if (end > start && contents.data[end - 1u] == (ctool_u8)'\r') {
      end--;
    }
    path.data = (const char *)(contents.data + start);
    path.size = end - start;
    if (obj_iso_path_shape(path, CTOOL_FALSE, &name) == CTOOL_FALSE) {
      return obj_iso_failure(
          job, manifest, arena, mark, CTOOL_ERR_INPUT,
          CTOOL_OBJ_DIAG_INVALID_INPUT, line,
          "CupidObj ISO fixture manifest path is invalid");
    }
    for (index = 1u; index < node_count; index++) {
      if (obj_string_equal(path, nodes[index].path) == CTOOL_TRUE) {
        exact = index;
        break;
      }
      if (obj_iso_string_equal_folded(path, nodes[index].path) ==
          CTOOL_TRUE) {
        return obj_iso_failure(
            job, manifest, arena, mark, CTOOL_ERR_INPUT,
            CTOOL_OBJ_DIAG_SYMBOL_COLLISION, line,
            "CupidObj ISO fixture manifest has a case collision");
      }
    }
    if (exact == 0xffffffffu) {
      return obj_iso_failure(
          job, manifest, arena, mark, CTOOL_ERR_INPUT,
          CTOOL_OBJ_DIAG_INVALID_INPUT, line,
          "CupidObj ISO fixture manifest entry has no typed input");
    }
    if (seen[exact - 1u] != 0u) {
      return obj_iso_failure(
          job, manifest, arena, mark, CTOOL_ERR_INPUT,
          CTOOL_OBJ_DIAG_SYMBOL_COLLISION, line,
          "CupidObj ISO fixture manifest contains a duplicate path");
    }
    seen[exact - 1u] = 1u;
    matched++;
    if (offset < contents.size) {
      offset++;
      line++;
    }
  }
  if (matched != node_count - 1u) {
    return obj_iso_failure(
        job, manifest, arena, mark, CTOOL_ERR_INPUT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
        "CupidObj ISO fixture typed input is absent from the manifest");
  }
  return CTOOL_OK;
}

static ctool_u8 obj_iso_identifier_character(ctool_u8 character) {
  ctool_u8 upper = character;
  if (upper >= (ctool_u8)'a' && upper <= (ctool_u8)'z') {
    upper = (ctool_u8)(upper - ((ctool_u8)'a' - (ctool_u8)'A'));
  }
  if ((upper >= (ctool_u8)'A' && upper <= (ctool_u8)'Z') ||
      (upper >= (ctool_u8)'0' && upper <= (ctool_u8)'9') ||
      upper == (ctool_u8)'_') {
    return upper;
  }
  return (ctool_u8)'_';
}

static ctool_bool obj_iso_identifier_equal(const obj_iso_node_t *left,
                                            const ctool_u8 *right,
                                            ctool_u32 right_size) {
  ctool_u32 index;
  if (left->identifier_size != right_size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < right_size; index++) {
    if (left->identifier[index] != right[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_i32 obj_iso_identifier_order(const obj_iso_node_t *left,
                                           const obj_iso_node_t *right) {
  ctool_u32 common = left->identifier_size < right->identifier_size
                         ? left->identifier_size
                         : right->identifier_size;
  ctool_u32 index;
  for (index = 0u; index < common; index++) {
    if (left->identifier[index] != right->identifier[index]) {
      return left->identifier[index] < right->identifier[index] ? -1 : 1;
    }
  }
  if (left->identifier_size == right->identifier_size) {
    return 0;
  }
  return left->identifier_size < right->identifier_size ? -1 : 1;
}

static ctool_status_t obj_iso_allocate_identifier(
    ctool_job_t *job, const ctool_source_t *manifest, obj_iso_node_t *nodes,
    ctool_u32 node_count, ctool_u32 node_index, ctool_arena_t *arena,
    ctool_arena_mark_t mark) {
  obj_iso_node_t *node = &nodes[node_index];
  ctool_u32 dot = 0xffffffffu;
  ctool_u32 stem_size;
  ctool_u32 extension_size = 0u;
  ctool_u32 index;
  ctool_u32 sequence = 0u;
  if (node->directory == CTOOL_FALSE && node->name.size > 2u &&
      node->name.data[0] != '.' &&
      node->name.data[node->name.size - 1u] != '.') {
    for (index = node->name.size; index != 0u; index--) {
      if (node->name.data[index - 1u] == '.') {
        dot = index - 1u;
        break;
      }
    }
  }
  stem_size = dot == 0xffffffffu ? node->name.size : dot;
  if (dot != 0xffffffffu) {
    extension_size = node->name.size - dot - 1u;
  }
  for (;;) {
    ctool_u8 candidate[CTOOL_OBJ_ISO_IDENTIFIER_BYTES];
    ctool_u8 stem[8];
    ctool_u8 extension[3];
    char suffix[10];
    ctool_u32 clean_stem = stem_size < 8u ? stem_size : 8u;
    ctool_u32 clean_extension =
        extension_size < 3u ? extension_size : 3u;
    ctool_u32 suffix_size = 0u;
    ctool_u32 candidate_size = 0u;
    ctool_bool collision = CTOOL_FALSE;
    if (clean_stem == 0u) {
      stem[0] = (ctool_u8)'_';
      clean_stem = 1u;
    } else {
      for (index = 0u; index < clean_stem; index++) {
        stem[index] = obj_iso_identifier_character(
            (ctool_u8)node->name.data[index]);
      }
    }
    for (index = 0u; index < clean_extension; index++) {
      extension[index] = obj_iso_identifier_character(
          (ctool_u8)node->name.data[dot + 1u + index]);
    }
    if (sequence != 0u) {
      suffix[0] = '_';
      suffix_size = 1u + obj_disk_decimal(suffix + 1u, sequence);
      if (suffix_size >= 8u) {
        return obj_iso_failure(
            job, manifest, arena, mark, CTOOL_ERR_LIMIT,
            CTOOL_OBJ_DIAG_LIMIT, 0u,
            "CupidObj ISO identifier collision space is exhausted");
      }
      if (clean_stem > 8u - suffix_size) {
        clean_stem = 8u - suffix_size;
      }
    }
    for (index = 0u; index < clean_stem; index++) {
      candidate[candidate_size++] = stem[index];
    }
    for (index = 0u; index < suffix_size; index++) {
      candidate[candidate_size++] = (ctool_u8)suffix[index];
    }
    if (node->directory == CTOOL_FALSE) {
      candidate[candidate_size++] = (ctool_u8)'.';
      for (index = 0u; index < clean_extension; index++) {
        candidate[candidate_size++] = extension[index];
      }
      candidate[candidate_size++] = (ctool_u8)';';
      candidate[candidate_size++] = (ctool_u8)'1';
    }
    for (index = 1u; index < node_count; index++) {
      if (index != node_index && nodes[index].parent == node->parent &&
          nodes[index].identifier_size != 0u &&
          obj_iso_identifier_equal(&nodes[index], candidate,
                                   candidate_size) == CTOOL_TRUE) {
        collision = CTOOL_TRUE;
        break;
      }
    }
    if (collision == CTOOL_FALSE) {
      node->identifier_size = candidate_size;
      for (index = 0u; index < candidate_size; index++) {
        node->identifier[index] = candidate[index];
      }
      return CTOOL_OK;
    }
    sequence++;
  }
}

static ctool_status_t obj_iso_build_nodes(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_arena_t *arena, ctool_arena_mark_t mark,
    obj_iso_node_t **nodes_out, ctool_u32 *node_count_out,
    ctool_u32 **directories_out, ctool_u32 *directory_count_out,
    ctool_u32 **files_out, ctool_u32 *file_count_out,
    ctool_u32 **children_out) {
  const ctool_obj_iso_fixture_request_t *iso = &request->as.iso_fixture;
  obj_iso_node_t *nodes = (obj_iso_node_t *)0;
  ctool_u8 *seen = (ctool_u8 *)0;
  ctool_u32 *directories = (ctool_u32 *)0;
  ctool_u32 *files = (ctool_u32 *)0;
  ctool_u32 *children = (ctool_u32 *)0;
  ctool_u32 node_count;
  ctool_u32 directory_count = 0u;
  ctool_u32 file_count = 0u;
  ctool_u32 index;
  ctool_status_t status;
  if (iso->entries == (const ctool_obj_iso_fixture_entry_t *)0 ||
      iso->entry_count == 0u) {
    return obj_iso_failure(
        job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
        "CupidObj ISO fixture entries are required");
  }
  if (iso->entry_count > CTOOL_OBJ_ISO_ENTRY_LIMIT) {
    return obj_iso_failure(
        job, request->input, arena, mark, CTOOL_ERR_LIMIT,
        CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj ISO fixture entry limit exceeded");
  }
  node_count = iso->entry_count + 1u;
  status = ctool_arena_alloc_zero(
      arena, node_count, (ctool_u32)sizeof(*nodes),
      (ctool_u32)sizeof(void *), (void **)&nodes);
  if (status == CTOOL_OK) {
    status = ctool_arena_alloc_zero(arena, iso->entry_count, 1u, 1u,
                                    (void **)&seen);
  }
  if (status == CTOOL_OK) {
    status = ctool_arena_alloc_zero(
        arena, node_count, (ctool_u32)sizeof(*directories),
        (ctool_u32)sizeof(ctool_u32), (void **)&directories);
  }
  if (status == CTOOL_OK) {
    status = ctool_arena_alloc_zero(
        arena, node_count, (ctool_u32)sizeof(*files),
        (ctool_u32)sizeof(ctool_u32), (void **)&files);
  }
  if (status == CTOOL_OK) {
    status = ctool_arena_alloc_zero(
        arena, node_count, (ctool_u32)sizeof(*children),
        (ctool_u32)sizeof(ctool_u32), (void **)&children);
  }
  if (status != CTOOL_OK) {
    return obj_iso_failure(
        job, request->input, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj ISO fixture inventory exceeds its arena limit");
  }
  nodes[0].path = ctool_string("");
  nodes[0].name = ctool_string("");
  nodes[0].parent = 0xffffffffu;
  nodes[0].directory = CTOOL_TRUE;
  nodes[0].identifier[0] = 0u;
  nodes[0].identifier_size = 1u;
  for (index = 0u; index < iso->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = &iso->entries[index];
    obj_iso_node_t *node = &nodes[index + 1u];
    const ctool_limits_t *limits = ctool_job_limits(job);
    ctool_bool directory =
        entry->kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY ? CTOOL_TRUE
                                                       : CTOOL_FALSE;
    ctool_u32 prior;
    if (entry->kind != CTOOL_OBJ_ISO_FIXTURE_DIRECTORY &&
        entry->kind != CTOOL_OBJ_ISO_FIXTURE_FILE) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
          "CupidObj ISO fixture entry kind is invalid");
    }
    if ((directory == CTOOL_TRUE &&
         entry->source != (const ctool_source_t *)0) ||
        (directory == CTOOL_FALSE &&
         entry->source == (const ctool_source_t *)0)) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
          "CupidObj ISO fixture entry source does not match its kind");
    }
    if (directory == CTOOL_FALSE &&
        entry->source->contents.data == (const ctool_u8 *)0 &&
        entry->source->contents.size != 0u) {
      return obj_iso_failure(
          job, entry->source, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
          "CupidObj ISO fixture file bytes are invalid");
    }
    if (entry->path.size > limits->path_bytes) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_LIMIT,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj ISO fixture logical path exceeds the job limit");
    }
    if (obj_iso_path_shape(entry->path, directory, &node->name) ==
        CTOOL_FALSE) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_INPUT,
          CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
          "CupidObj ISO fixture logical path is invalid");
    }
    for (prior = 1u; prior <= index; prior++) {
      if (obj_iso_string_equal_folded(entry->path, nodes[prior].path) ==
          CTOOL_TRUE) {
        return obj_iso_failure(
            job, request->input, arena, mark, CTOOL_ERR_INPUT,
            CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
            "CupidObj ISO fixture paths have a case collision");
      }
    }
    node->entry = entry;
    node->path = entry->path;
    node->directory = directory;
  }
  status = obj_iso_validate_manifest(job, request->input, nodes, node_count,
                                     seen, arena, mark);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 1u; index < node_count; index++) {
    ctool_string_t parent_path;
    ctool_bool has_parent =
        obj_iso_path_parent(nodes[index].path, &parent_path);
    ctool_u32 candidate;
    ctool_u32 parent = 0xffffffffu;
    if (has_parent == CTOOL_FALSE) {
      nodes[index].parent = 0u;
      continue;
    }
    for (candidate = 1u; candidate < node_count; candidate++) {
      if (obj_string_equal(parent_path, nodes[candidate].path) == CTOOL_TRUE) {
        parent = candidate;
        break;
      }
    }
    if (parent == 0xffffffffu || nodes[parent].directory == CTOOL_FALSE) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_INPUT,
          CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
          "CupidObj ISO fixture entry has no directory parent");
    }
    nodes[index].parent = parent;
  }
  for (index = 0u; index < node_count; index++) {
    ctool_u32 child_count = 0u;
    ctool_u32 allocated;
    ctool_u32 child;
    if (nodes[index].directory == CTOOL_FALSE) {
      continue;
    }
    for (child = 1u; child < node_count; child++) {
      if (nodes[child].parent == index) {
        child_count++;
      }
    }
    for (allocated = 0u; allocated < child_count; allocated++) {
      ctool_u32 selected = 0xffffffffu;
      for (child = 1u; child < node_count; child++) {
        if (nodes[child].parent != index ||
            nodes[child].identifier_size != 0u) {
          continue;
        }
        if (selected == 0xffffffffu ||
            obj_iso_string_order(nodes[child].name,
                                 nodes[selected].name) < 0) {
          selected = child;
        }
      }
      if (selected == 0xffffffffu) {
        return obj_iso_failure(
            job, request->input, arena, mark, CTOOL_ERR_INTERNAL,
            CTOOL_OBJ_DIAG_OUTPUT, 0u,
            "CupidObj ISO fixture child ordering failed");
      }
      status = obj_iso_allocate_identifier(
          job, request->input, nodes, node_count, selected, arena, mark);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  {
    ctool_u32 child_total = 0u;
    for (index = 0u; index < node_count; index++) {
      ctool_u32 child;
      nodes[index].child_start = child_total;
      if (nodes[index].directory == CTOOL_FALSE) {
        continue;
      }
      for (child = 1u; child < node_count; child++) {
        ctool_u32 position;
        if (nodes[child].parent != index) {
          continue;
        }
        position = child_total;
        while (position > nodes[index].child_start &&
               obj_iso_identifier_order(
                   &nodes[child], &nodes[children[position - 1u]]) < 0) {
          children[position] = children[position - 1u];
          position--;
        }
        children[position] = child;
        child_total++;
      }
      nodes[index].child_count = child_total - nodes[index].child_start;
    }
  }
  nodes[0].directory_number = 1u;
  directories[directory_count++] = 0u;
  for (index = 0u; index < directory_count; index++) {
    ctool_u32 parent = directories[index];
    ctool_u32 ordinal;
    for (ordinal = 0u; ordinal < nodes[parent].child_count; ordinal++) {
      ctool_u32 selected =
          children[nodes[parent].child_start + ordinal];
      if (nodes[selected].directory == CTOOL_FALSE) {
        continue;
      }
      if (directory_count == 65535u) {
        return obj_iso_failure(
            job, request->input, arena, mark, CTOOL_ERR_LIMIT,
            CTOOL_OBJ_DIAG_LIMIT, 0u,
            "CupidObj ISO fixture directory count exceeds ECMA-119");
      }
      nodes[selected].directory_number = directory_count + 1u;
      directories[directory_count++] = selected;
    }
  }
  for (index = 1u; index < node_count; index++) {
    ctool_u32 position;
    if (nodes[index].directory == CTOOL_TRUE) {
      continue;
    }
    position = file_count;
    while (position != 0u &&
           obj_iso_string_order(nodes[index].path,
                                nodes[files[position - 1u]].path) < 0) {
      files[position] = files[position - 1u];
      position--;
    }
    files[position] = index;
    file_count++;
  }
  *nodes_out = nodes;
  *node_count_out = node_count;
  *directories_out = directories;
  *directory_count_out = directory_count;
  *files_out = files;
  *file_count_out = file_count;
  *children_out = children;
  return CTOOL_OK;
}

typedef enum {
  OBJ_ISO_RECORD_PVD_ROOT = 0,
  OBJ_ISO_RECORD_ROOT_DOT,
  OBJ_ISO_RECORD_DOT,
  OBJ_ISO_RECORD_DOT_DOT,
  OBJ_ISO_RECORD_CHILD
} obj_iso_record_kind_t;

static void obj_iso_copy(ctool_u8 *destination, const ctool_u8 *source,
                         ctool_u32 count) {
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    destination[index] = source[index];
  }
}

static void obj_iso_fill(ctool_u8 *destination, ctool_u8 value,
                         ctool_u32 count) {
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    destination[index] = value;
  }
}

static void obj_iso_write_le16(ctool_u8 *bytes, ctool_u16 value) {
  bytes[0] = (ctool_u8)(value & 0xffu);
  bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
}

static void obj_iso_write_be16(ctool_u8 *bytes, ctool_u16 value) {
  bytes[0] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[1] = (ctool_u8)(value & 0xffu);
}

static void obj_iso_write_le32(ctool_u8 *bytes, ctool_u32 value) {
  bytes[0] = (ctool_u8)(value & 0xffu);
  bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[2] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[3] = (ctool_u8)((value >> 24u) & 0xffu);
}

static void obj_iso_write_be32(ctool_u8 *bytes, ctool_u32 value) {
  bytes[0] = (ctool_u8)((value >> 24u) & 0xffu);
  bytes[1] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[2] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[3] = (ctool_u8)(value & 0xffu);
}

static void obj_iso_write_both16(ctool_u8 *bytes, ctool_u16 value) {
  obj_iso_write_le16(bytes, value);
  obj_iso_write_be16(bytes + 2u, value);
}

static void obj_iso_write_both32(ctool_u8 *bytes, ctool_u32 value) {
  obj_iso_write_le32(bytes, value);
  obj_iso_write_be32(bytes + 4u, value);
}

static ctool_u32 obj_iso_directory_child_count(
    const obj_iso_node_t *nodes, ctool_u32 node_count,
    ctool_u32 parent) {
  ctool_u32 count = 0u;
  ctool_u32 index;
  for (index = 1u; index < node_count; index++) {
    if (nodes[index].parent == parent &&
        nodes[index].directory == CTOOL_TRUE) {
      count++;
    }
  }
  return count;
}

static ctool_u32 obj_iso_record_susp_size(
    obj_iso_record_kind_t kind, const obj_iso_node_t *node) {
  if (kind == OBJ_ISO_RECORD_PVD_ROOT) {
    return 0u;
  }
  if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
    return 7u + 36u + 26u + 28u;
  }
  if (kind == OBJ_ISO_RECORD_DOT || kind == OBJ_ISO_RECORD_DOT_DOT) {
    return 36u + 26u;
  }
  return 36u + 26u + 5u + node->name.size;
}

static ctool_u32 obj_iso_record_size(obj_iso_record_kind_t kind,
                                     ctool_u32 identifier_size,
                                     const obj_iso_node_t *node) {
  ctool_u32 size = 33u + identifier_size +
                   (identifier_size % 2u == 0u ? 1u : 0u) +
                   obj_iso_record_susp_size(kind, node);
  if (size % 2u != 0u) {
    size++;
  }
  return size;
}

static ctool_status_t obj_iso_directory_size(
    ctool_job_t *job, const ctool_source_t *manifest,
    const obj_iso_node_t *nodes, const ctool_u32 *children,
    ctool_u32 directory,
    ctool_u32 *size_out, ctool_arena_t *arena, ctool_arena_mark_t mark) {
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  ctool_u32 sizes[2];
  sizes[0] = obj_iso_record_size(
      directory == 0u ? OBJ_ISO_RECORD_ROOT_DOT : OBJ_ISO_RECORD_DOT, 1u,
      &nodes[directory]);
  sizes[1] = obj_iso_record_size(OBJ_ISO_RECORD_DOT_DOT, 1u,
                                 &nodes[directory]);
  for (index = 0u; index < 2u; index++) {
    ctool_u32 remaining =
        CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
    if (sizes[index] > 255u) {
      return obj_iso_failure(
          job, manifest, arena, mark, CTOOL_ERR_LIMIT,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj ISO directory record exceeds one byte");
    }
    if (sizes[index] > remaining) {
      cursor += remaining;
    }
    cursor += sizes[index];
  }
  for (index = 0u; index < nodes[directory].child_count; index++) {
    ctool_u32 child = children[nodes[directory].child_start + index];
    ctool_u32 size;
    ctool_u32 remaining;
    size = obj_iso_record_size(OBJ_ISO_RECORD_CHILD,
                               nodes[child].identifier_size, &nodes[child]);
    if (size > 255u) {
      return obj_iso_failure(
          job, manifest, arena, mark, CTOOL_ERR_LIMIT,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj Rock Ridge directory record is too long");
    }
    remaining =
        CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
    if (size > remaining) {
      cursor += remaining;
    }
    cursor += size;
  }
  if (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES != 0u) {
    cursor += CTOOL_OBJ_ISO_BLOCK_BYTES -
              (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
  }
  *size_out = cursor;
  return CTOOL_OK;
}

static void obj_iso_write_px(ctool_u8 *bytes,
                             const obj_iso_node_t *nodes,
                             ctool_u32 node_count, ctool_u32 node_index) {
  const obj_iso_node_t *node = &nodes[node_index];
  ctool_u32 mode = node->directory == CTOOL_TRUE ? 040555u : 0100444u;
  ctool_u32 links = node->directory == CTOOL_TRUE
                        ? 2u + obj_iso_directory_child_count(
                                   nodes, node_count, node_index)
                        : 1u;
  bytes[0] = (ctool_u8)'P';
  bytes[1] = (ctool_u8)'X';
  bytes[2] = 36u;
  bytes[3] = 1u;
  obj_iso_write_both32(bytes + 4u, mode);
  obj_iso_write_both32(bytes + 12u, links);
  obj_iso_write_both32(bytes + 20u, 0u);
  obj_iso_write_both32(bytes + 28u, 0u);
}

static void obj_iso_write_tf(ctool_u8 *bytes) {
  static const ctool_u8 recording[7] = {100u, 1u, 1u, 0u, 0u, 0u, 0u};
  bytes[0] = (ctool_u8)'T';
  bytes[1] = (ctool_u8)'F';
  bytes[2] = 26u;
  bytes[3] = 1u;
  bytes[4] = 0x0eu;
  obj_iso_copy(bytes + 5u, recording, 7u);
  obj_iso_copy(bytes + 12u, recording, 7u);
  obj_iso_copy(bytes + 19u, recording, 7u);
}

static void obj_iso_write_ce(ctool_u8 *bytes, ctool_u32 extent) {
  bytes[0] = (ctool_u8)'C';
  bytes[1] = (ctool_u8)'E';
  bytes[2] = 28u;
  bytes[3] = 1u;
  obj_iso_write_both32(bytes + 4u, extent);
  obj_iso_write_both32(bytes + 12u, 0u);
  obj_iso_write_both32(bytes + 20u, CTOOL_OBJ_ISO_ER_BYTES);
}

static void obj_iso_write_nm(ctool_u8 *bytes, ctool_string_t name) {
  bytes[0] = (ctool_u8)'N';
  bytes[1] = (ctool_u8)'M';
  bytes[2] = (ctool_u8)(5u + name.size);
  bytes[3] = 1u;
  bytes[4] = 0u;
  obj_iso_copy(bytes + 5u, (const ctool_u8 *)name.data, name.size);
}

static ctool_u32 obj_iso_write_record(
    ctool_u8 *bytes, ctool_u32 extent, ctool_u32 data_size,
    const ctool_u8 *identifier, ctool_u32 identifier_size,
    ctool_bool directory, obj_iso_record_kind_t kind,
    const obj_iso_node_t *nodes, ctool_u32 node_count,
    ctool_u32 metadata_node, ctool_u32 continuation_extent) {
  static const ctool_u8 recording[7] = {100u, 1u, 1u, 0u, 0u, 0u, 0u};
  const obj_iso_node_t *node = &nodes[metadata_node];
  ctool_u32 size = obj_iso_record_size(kind, identifier_size, node);
  ctool_u32 padding = identifier_size % 2u == 0u ? 1u : 0u;
  ctool_u32 susp = 33u + identifier_size + padding;
  bytes[0] = (ctool_u8)size;
  bytes[1] = 0u;
  obj_iso_write_both32(bytes + 2u, extent);
  obj_iso_write_both32(bytes + 10u, data_size);
  obj_iso_copy(bytes + 18u, recording, 7u);
  bytes[25] = directory == CTOOL_TRUE ? 0x02u : 0u;
  bytes[26] = 0u;
  bytes[27] = 0u;
  obj_iso_write_both16(bytes + 28u, 1u);
  bytes[32] = (ctool_u8)identifier_size;
  obj_iso_copy(bytes + 33u, identifier, identifier_size);
  if (kind == OBJ_ISO_RECORD_PVD_ROOT) {
    return size;
  }
  if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
    static const ctool_u8 sp[7] = {'S', 'P', 7u, 1u, 0xbeu, 0xefu, 0u};
    obj_iso_copy(bytes + susp, sp, 7u);
    susp += 7u;
  }
  obj_iso_write_px(bytes + susp, nodes, node_count, metadata_node);
  susp += 36u;
  obj_iso_write_tf(bytes + susp);
  susp += 26u;
  if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
    obj_iso_write_ce(bytes + susp, continuation_extent);
  } else if (kind == OBJ_ISO_RECORD_CHILD) {
    obj_iso_write_nm(bytes + susp, node->name);
  }
  return size;
}

static void obj_iso_write_directory(
    ctool_u8 *bytes, const obj_iso_node_t *nodes, ctool_u32 node_count,
    const ctool_u32 *children, ctool_u32 directory,
    ctool_u32 continuation_extent) {
  const obj_iso_node_t *node = &nodes[directory];
  ctool_u32 parent = directory == 0u ? 0u : node->parent;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  ctool_u8 dot = 0u;
  ctool_u8 dot_dot = 1u;
  obj_iso_record_kind_t dot_kind =
      directory == 0u ? OBJ_ISO_RECORD_ROOT_DOT : OBJ_ISO_RECORD_DOT;
  ctool_u32 size = obj_iso_record_size(dot_kind, 1u, node);
  cursor += obj_iso_write_record(bytes + cursor, node->extent, node->size,
                                 &dot, 1u, CTOOL_TRUE, dot_kind, nodes,
                                 node_count, directory,
                                 continuation_extent);
  size = obj_iso_record_size(OBJ_ISO_RECORD_DOT_DOT, 1u, &nodes[parent]);
  if (size > CTOOL_OBJ_ISO_BLOCK_BYTES -
                 (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES)) {
    cursor += CTOOL_OBJ_ISO_BLOCK_BYTES -
              (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
  }
  cursor += obj_iso_write_record(
      bytes + cursor, nodes[parent].extent, nodes[parent].size, &dot_dot, 1u,
      CTOOL_TRUE, OBJ_ISO_RECORD_DOT_DOT, nodes, node_count, parent,
      continuation_extent);
  for (index = 0u; index < nodes[directory].child_count; index++) {
    ctool_u32 child = children[nodes[directory].child_start + index];
    const obj_iso_node_t *child_node = &nodes[child];
    ctool_u32 child_size = child_node->directory == CTOOL_TRUE
                               ? child_node->size
                               : child_node->entry->source->contents.size;
    size = obj_iso_record_size(OBJ_ISO_RECORD_CHILD,
                               child_node->identifier_size, child_node);
    if (size > CTOOL_OBJ_ISO_BLOCK_BYTES -
                   (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES)) {
      cursor += CTOOL_OBJ_ISO_BLOCK_BYTES -
                (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
    }
    cursor += obj_iso_write_record(
        bytes + cursor, child_node->extent, child_size,
        child_node->identifier, child_node->identifier_size,
        child_node->directory, OBJ_ISO_RECORD_CHILD, nodes, node_count,
        child, continuation_extent);
  }
}

static void obj_iso_write_identifier_field(ctool_u8 *bytes,
                                            ctool_u32 size,
                                            const char *value) {
  ctool_string_t text = ctool_string(value);
  obj_iso_fill(bytes, (ctool_u8)' ', size);
  obj_iso_copy(bytes, (const ctool_u8 *)text.data, text.size);
}

static void obj_iso_write_er(ctool_u8 *bytes) {
  static const char identifier[] = "RRIP_1991A";
  static const char description[] =
      "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX "
      "FILE SYSTEM SEMANTICS";
  static const char source[] =
      "PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE "
      "PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT "
      "INFORMATION.";
  bytes[0] = (ctool_u8)'E';
  bytes[1] = (ctool_u8)'R';
  bytes[2] = (ctool_u8)CTOOL_OBJ_ISO_ER_BYTES;
  bytes[3] = 1u;
  bytes[4] = (ctool_u8)((ctool_u32)sizeof(identifier) - 1u);
  bytes[5] = (ctool_u8)((ctool_u32)sizeof(description) - 1u);
  bytes[6] = (ctool_u8)((ctool_u32)sizeof(source) - 1u);
  bytes[7] = 1u;
  obj_iso_copy(bytes + 8u, (const ctool_u8 *)identifier,
               (ctool_u32)sizeof(identifier) - 1u);
  obj_iso_copy(bytes + 18u, (const ctool_u8 *)description,
               (ctool_u32)sizeof(description) - 1u);
  obj_iso_copy(bytes + 102u, (const ctool_u8 *)source,
               (ctool_u32)sizeof(source) - 1u);
}

static ctool_u32 obj_iso_path_table_size(
    const obj_iso_node_t *nodes, const ctool_u32 *directories,
    ctool_u32 directory_count) {
  ctool_u32 size = 0u;
  ctool_u32 index;
  for (index = 0u; index < directory_count; index++) {
    ctool_u32 identifier_size =
        index == 0u ? 1u : nodes[directories[index]].identifier_size;
    size += 8u + identifier_size +
            (identifier_size % 2u != 0u ? 1u : 0u);
  }
  return size;
}

static void obj_iso_write_path_table(
    ctool_u8 *bytes, const obj_iso_node_t *nodes,
    const ctool_u32 *directories, ctool_u32 directory_count,
    ctool_bool big_endian) {
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  ctool_u8 root_identifier = 0u;
  for (index = 0u; index < directory_count; index++) {
    const obj_iso_node_t *node = &nodes[directories[index]];
    const ctool_u8 *identifier =
        index == 0u ? &root_identifier : node->identifier;
    ctool_u32 identifier_size =
        index == 0u ? 1u : node->identifier_size;
    ctool_u32 parent_number =
        index == 0u ? 1u : nodes[node->parent].directory_number;
    bytes[cursor] = (ctool_u8)identifier_size;
    bytes[cursor + 1u] = 0u;
    if (big_endian == CTOOL_TRUE) {
      obj_iso_write_be32(bytes + cursor + 2u, node->extent);
      obj_iso_write_be16(bytes + cursor + 6u,
                         (ctool_u16)parent_number);
    } else {
      obj_iso_write_le32(bytes + cursor + 2u, node->extent);
      obj_iso_write_le16(bytes + cursor + 6u,
                         (ctool_u16)parent_number);
    }
    obj_iso_copy(bytes + cursor + 8u, identifier, identifier_size);
    cursor += 8u + identifier_size;
    if (identifier_size % 2u != 0u) {
      bytes[cursor++] = 0u;
    }
  }
}

static void obj_iso_write_primary_descriptor(
    ctool_u8 *bytes, ctool_u32 volume_blocks, ctool_u32 path_table_size,
    ctool_u32 little_path_extent, ctool_u32 big_path_extent,
    const obj_iso_node_t *nodes, ctool_u32 node_count) {
  static const ctool_u8 header[7] = {1u, 'C', 'D', '0', '0', '1', 1u};
  static const ctool_u8 volume_date[17] = {
      '2', '0', '0', '0', '0', '1', '0', '1', '0',
      '0', '0', '0', '0', '0', '0', '0', 0u};
  static const ctool_u8 unspecified_date[17] = {
      '0', '0', '0', '0', '0', '0', '0', '0', '0',
      '0', '0', '0', '0', '0', '0', '0', 0u};
  ctool_u8 root_identifier = 0u;
  obj_iso_copy(bytes, header, 7u);
  obj_iso_write_identifier_field(bytes + 8u, 32u, "CUPID OS");
  obj_iso_write_identifier_field(bytes + 40u, 32u, "CUPID_OS_TEST");
  obj_iso_write_both32(bytes + 80u, volume_blocks);
  obj_iso_write_both16(bytes + 120u, 1u);
  obj_iso_write_both16(bytes + 124u, 1u);
  obj_iso_write_both16(bytes + 128u,
                       (ctool_u16)CTOOL_OBJ_ISO_BLOCK_BYTES);
  obj_iso_write_both32(bytes + 132u, path_table_size);
  obj_iso_write_le32(bytes + 140u, little_path_extent);
  obj_iso_write_be32(bytes + 148u, big_path_extent);
  (void)obj_iso_write_record(
      bytes + 156u, nodes[0].extent, nodes[0].size, &root_identifier, 1u,
      CTOOL_TRUE, OBJ_ISO_RECORD_PVD_ROOT, nodes, node_count, 0u, 0u);
  obj_iso_write_identifier_field(bytes + 190u, 128u,
                                 "CUPID_OS_TEST_FIXTURE");
  obj_iso_write_identifier_field(bytes + 318u, 128u, "CUPID OS");
  obj_iso_write_identifier_field(bytes + 446u, 128u,
                                 "CUPID OS REPOSITORY HOSTBUILD");
  obj_iso_write_identifier_field(
      bytes + 574u, 128u, "CUPID OS DETERMINISTIC ISO9660 AUTHOR");
  obj_iso_write_identifier_field(bytes + 702u, 37u, "");
  obj_iso_write_identifier_field(bytes + 739u, 37u, "");
  obj_iso_write_identifier_field(bytes + 776u, 37u, "");
  obj_iso_copy(bytes + 813u, volume_date, 17u);
  obj_iso_copy(bytes + 830u, volume_date, 17u);
  obj_iso_copy(bytes + 847u, unspecified_date, 17u);
  obj_iso_copy(bytes + 864u, volume_date, 17u);
  bytes[881u] = 1u;
}

static ctool_status_t obj_iso_fixture(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  obj_iso_node_t *nodes = (obj_iso_node_t *)0;
  ctool_u32 *directories = (ctool_u32 *)0;
  ctool_u32 *files = (ctool_u32 *)0;
  ctool_u32 *children = (ctool_u32 *)0;
  ctool_u32 node_count = 0u;
  ctool_u32 directory_count = 0u;
  ctool_u32 file_count = 0u;
  ctool_u32 path_table_size;
  ctool_u32 path_table_blocks;
  ctool_u32 little_path_extent = 18u;
  ctool_u32 big_path_extent;
  ctool_u32 continuation_extent;
  ctool_u64 next_extent;
  ctool_u64 output_bytes_u64;
  ctool_u32 output_offset;
  ctool_mut_bytes_t reserved;
  ctool_u8 *bytes;
  ctool_u32 index;
  ctool_status_t status;
  ctool_status_t rewind_status;

  if (request->input->contents.data == (const ctool_u8 *)0 &&
      request->input->contents.size != 0u) {
    return obj_iso_failure(
        job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
        "CupidObj ISO fixture manifest bytes are invalid");
  }
  status = obj_iso_build_nodes(
      job, request, arena, mark, &nodes, &node_count, &directories,
      &directory_count, &files, &file_count, &children);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < directory_count; index++) {
    ctool_u32 node_index = directories[index];
    status = obj_iso_directory_size(
        job, request->input, nodes, children, node_index,
        &nodes[node_index].size, arena, mark);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  path_table_size =
      obj_iso_path_table_size(nodes, directories, directory_count);
  path_table_blocks =
      (path_table_size + CTOOL_OBJ_ISO_BLOCK_BYTES - 1u) /
      CTOOL_OBJ_ISO_BLOCK_BYTES;
  big_path_extent = little_path_extent + path_table_blocks;
  next_extent = (ctool_u64)big_path_extent + path_table_blocks;
  for (index = 0u; index < directory_count; index++) {
    obj_iso_node_t *node = &nodes[directories[index]];
    if (next_extent > (ctool_u64)0xffffffffu) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj ISO directory extents overflow ECMA-119");
    }
    node->extent = (ctool_u32)next_extent;
    next_extent += node->size / CTOOL_OBJ_ISO_BLOCK_BYTES;
  }
  if (next_extent > (ctool_u64)0xffffffffu) {
    return obj_iso_failure(
        job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
        CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj ISO continuation extent overflows ECMA-119");
  }
  continuation_extent = (ctool_u32)next_extent;
  next_extent++;
  for (index = 0u; index < file_count; index++) {
    obj_iso_node_t *node = &nodes[files[index]];
    ctool_u32 size = node->entry->source->contents.size;
    if (size == 0u) {
      node->extent = 0u;
      continue;
    }
    if (next_extent > (ctool_u64)0xffffffffu) {
      return obj_iso_failure(
          job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
          CTOOL_OBJ_DIAG_LIMIT, 0u,
          "CupidObj ISO file extents overflow ECMA-119");
    }
    node->extent = (ctool_u32)next_extent;
    next_extent += ((ctool_u64)size + CTOOL_OBJ_ISO_BLOCK_BYTES - 1u) /
                   CTOOL_OBJ_ISO_BLOCK_BYTES;
  }
  output_bytes_u64 = next_extent * CTOOL_OBJ_ISO_BLOCK_BYTES;
  if (next_extent > (ctool_u64)0xffffffffu ||
      output_bytes_u64 > (ctool_u64)0xffffffffu) {
    return obj_iso_failure(
        job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
        CTOOL_OBJ_DIAG_LIMIT, 0u,
        "CupidObj ISO output exceeds the i386 byte limit");
  }
  status = ctool_buffer_reserve_zero(output, (ctool_u32)output_bytes_u64,
                                     &output_offset, &reserved);
  if (status != CTOOL_OK) {
    return obj_iso_failure(
        job, request->input, arena, mark, status,
        status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW
            ? CTOOL_OBJ_DIAG_LIMIT
            : CTOOL_OBJ_DIAG_OUTPUT,
        0u, "CupidObj ISO output cannot be reserved");
  }
  bytes = reserved.data;
  obj_iso_write_primary_descriptor(
      bytes + 16u * CTOOL_OBJ_ISO_BLOCK_BYTES, (ctool_u32)next_extent,
      path_table_size, little_path_extent, big_path_extent, nodes,
      node_count);
  bytes[17u * CTOOL_OBJ_ISO_BLOCK_BYTES] = 0xffu;
  obj_iso_copy(bytes + 17u * CTOOL_OBJ_ISO_BLOCK_BYTES + 1u,
               (const ctool_u8 *)"CD001", 5u);
  bytes[17u * CTOOL_OBJ_ISO_BLOCK_BYTES + 6u] = 1u;
  obj_iso_write_path_table(
      bytes + little_path_extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
      directories, directory_count, CTOOL_FALSE);
  obj_iso_write_path_table(
      bytes + big_path_extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
      directories, directory_count, CTOOL_TRUE);
  obj_iso_write_er(bytes + continuation_extent * CTOOL_OBJ_ISO_BLOCK_BYTES);
  for (index = 0u; index < directory_count; index++) {
    ctool_u32 node_index = directories[index];
    obj_iso_write_directory(
        bytes + nodes[node_index].extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
        node_count, children, node_index, continuation_extent);
  }
  for (index = 0u; index < file_count; index++) {
    const obj_iso_node_t *node = &nodes[files[index]];
    ctool_bytes_t contents = node->entry->source->contents;
    if (contents.size != 0u) {
      obj_iso_copy(bytes + node->extent * CTOOL_OBJ_ISO_BLOCK_BYTES,
                   contents.data, contents.size);
    }
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  (void)output_offset;
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_extract_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_status_t status, ctool_u32 code,
    ctool_u32 column, const char *message) {
  ctool_status_t rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  return obj_emit_failure(job, source, status, code, column, message);
}

static ctool_bool obj_jpeg_frame_marker(ctool_u8 marker) {
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
    return CTOOL_TRUE;
  default:
    return CTOOL_FALSE;
  }
}

static ctool_status_t obj_jpeg_marker_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_status_t status,
    ctool_u32 code, ctool_u32 column, const char *prefix, ctool_u8 marker,
    const char *suffix) {
  static const char digits[] = "0123456789abcdef";
  char message[128];
  ctool_string_t prefix_text = ctool_string(prefix);
  ctool_string_t suffix_text = ctool_string(suffix);
  ctool_u32 index;
  ctool_u32 cursor = 0u;
  if (prefix_text.size + suffix_text.size + 5u >
      (ctool_u32)sizeof(message)) {
    return obj_emit_failure(job, source, CTOOL_ERR_LIMIT,
                            CTOOL_OBJ_DIAG_LIMIT, column,
                            "CupidObj JPEG diagnostic is too long");
  }
  for (index = 0u; index < prefix_text.size; index++) {
    message[cursor] = prefix_text.data[index];
    cursor++;
  }
  message[cursor] = '0';
  message[cursor + 1u] = 'x';
  message[cursor + 2u] = digits[(marker >> 4u) & 0x0fu];
  message[cursor + 3u] = digits[marker & 0x0fu];
  cursor += 4u;
  for (index = 0u; index < suffix_text.size; index++) {
    message[cursor] = suffix_text.data[index];
    cursor++;
  }
  message[cursor] = '\0';
  return obj_emit_failure(job, source, status, code, column, message);
}

static ctool_status_t obj_validate_jpeg(ctool_job_t *job,
                                         const ctool_source_t *source) {
  ctool_bytes_t contents = source->contents;
  ctool_u32 offset = 2u;
  ctool_u32 frame_offset = 0u;
  ctool_u8 frame_marker = 0u;
  ctool_bool saw_scan = CTOOL_FALSE;
  ctool_bool saw_eoi = CTOOL_FALSE;

  if (contents.size < 2u || contents.data[0] != 0xffu ||
      contents.data[1] != 0xd8u) {
    return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "JPEG input has no SOI marker");
  }
  while (offset < contents.size) {
    ctool_u32 marker_offset = offset;
    ctool_u32 segment_size;
    ctool_u8 marker;
    if (contents.data[offset] != 0xffu) {
      return obj_emit_failure(
          job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
          offset, "JPEG marker stream is malformed outside a scan");
    }
    while (offset < contents.size && contents.data[offset] == 0xffu) {
      offset++;
    }
    if (offset >= contents.size) {
      break;
    }
    marker = contents.data[offset];
    offset++;
    if (marker == 0x00u) {
      return obj_emit_failure(
          job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
          marker_offset,
          "JPEG marker stream contains stuffed data before a scan");
    }
    if (marker == 0xd9u) {
      saw_eoi = CTOOL_TRUE;
      if (offset != contents.size) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            offset, "JPEG input has trailing bytes after the EOI marker");
      }
      break;
    }
    if (marker == 0x01u || marker == 0xd8u ||
        (marker >= 0xd0u && marker <= 0xd7u)) {
      if (marker != 0x01u) {
        return obj_jpeg_marker_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            marker_offset, "unexpected standalone JPEG marker ", marker,
            "");
      }
      continue;
    }
    if (contents.size - offset < 2u) {
      return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                              CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                              "JPEG marker length is truncated");
    }
    segment_size = ((ctool_u32)contents.data[offset] << 8u) |
                   (ctool_u32)contents.data[offset + 1u];
    if (segment_size < 2u || segment_size > contents.size - offset) {
      return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                              CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                              "JPEG marker length is invalid");
    }
    if (obj_jpeg_frame_marker(marker) == CTOOL_TRUE) {
      ctool_u32 component_count;
      if (frame_marker != 0u) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            marker_offset, "JPEG input contains more than one frame header");
      }
      if (segment_size < 8u) {
        return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                                "JPEG frame header is truncated");
      }
      component_count = (ctool_u32)contents.data[offset + 7u];
      if (component_count == 0u ||
          segment_size != 8u + 3u * component_count) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            marker_offset,
            "JPEG frame header has an invalid component table");
      }
      if (contents.data[offset + 2u] == 0u) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            marker_offset,
            "JPEG frame header has an invalid sample precision");
      }
      if ((contents.data[offset + 3u] == 0u &&
           contents.data[offset + 4u] == 0u) ||
          (contents.data[offset + 5u] == 0u &&
           contents.data[offset + 6u] == 0u)) {
        return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                                "JPEG frame header has an invalid image size");
      }
      frame_marker = marker;
      frame_offset = marker_offset;
    }
    if (marker == 0xdau) {
      ctool_u32 scan_components;
      if (frame_marker == 0u) {
        return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                                "JPEG scan appears before its frame header");
      }
      if (segment_size < 6u) {
        return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                CTOOL_OBJ_DIAG_INVALID_INPUT, marker_offset,
                                "JPEG scan header is truncated");
      }
      scan_components = (ctool_u32)contents.data[offset + 2u];
      if (scan_components == 0u ||
          segment_size != 6u + 2u * scan_components) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
            marker_offset,
            "JPEG scan header has an invalid component table");
      }
      saw_scan = CTOOL_TRUE;
      offset += segment_size;
      while (offset < contents.size) {
        ctool_u32 scan_marker_offset;
        ctool_u8 scan_marker;
        if (contents.data[offset] != 0xffu) {
          offset++;
          continue;
        }
        scan_marker_offset = offset;
        while (offset < contents.size && contents.data[offset] == 0xffu) {
          offset++;
        }
        if (offset >= contents.size) {
          return obj_emit_failure(
              job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
              scan_marker_offset,
              "JPEG entropy data ends with a partial marker");
        }
        scan_marker = contents.data[offset];
        offset++;
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
    return obj_emit_failure(
        job, source, CTOOL_ERR_UNSUPPORTED, CTOOL_OBJ_DIAG_UNSUPPORTED,
        frame_offset,
        "unsupported progressive JPEG frame; check in a baseline SOF0/SOF1 asset");
  }
  if (frame_marker != 0xc0u && frame_marker != 0xc1u) {
    if (frame_marker == 0u) {
      return obj_emit_failure(
          job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
          "JPEG input has no supported SOF0/SOF1 frame");
    }
    return obj_jpeg_marker_failure(
        job, source, CTOOL_ERR_UNSUPPORTED, CTOOL_OBJ_DIAG_UNSUPPORTED,
        frame_offset, "unsupported JPEG frame marker ", frame_marker,
        "; check in a baseline SOF0/SOF1 asset");
  }
  if (saw_scan == CTOOL_FALSE) {
    return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "JPEG input has no scan");
  }
  if (saw_eoi == CTOOL_FALSE) {
    return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "JPEG input has no EOI marker");
  }
  return CTOOL_OK;
}

static void obj_disk_copy(ctool_u8 *destination, ctool_u32 offset,
                          const ctool_u8 *source, ctool_u32 count) {
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    destination[offset + index] = source[index];
  }
}

static void obj_disk_write_le16(ctool_u8 *bytes, ctool_u32 offset,
                                ctool_u16 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
}

static void obj_disk_write_le32(ctool_u8 *bytes, ctool_u32 offset,
                                ctool_u32 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (ctool_u8)((value >> 24u) & 0xffu);
}

static void obj_disk_initialize_fat(ctool_u8 *bytes, ctool_u32 offset) {
  bytes[offset] = 0xf8u;
  bytes[offset + 1u] = 0xffu;
  bytes[offset + 2u] = 0xffu;
  bytes[offset + 3u] = 0xffu;
}

static ctool_u32 obj_disk_decimal(char *destination, ctool_u32 value) {
  char reverse[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    destination[index] = reverse[count - index - 1u];
  }
  return count;
}

static ctool_status_t obj_disk_overlap_failure(
    ctool_job_t *job, const ctool_source_t *kernel, ctool_u32 fat_start_lba) {
  static const char prefix[] =
      "CupidObj kernel overlaps FAT partition at LBA ";
  char message[64];
  ctool_u32 index;
  ctool_u32 size = (ctool_u32)sizeof(prefix) - 1u;
  for (index = 0u; index < size; index++) {
    message[index] = prefix[index];
  }
  size += obj_disk_decimal(message + size, fat_start_lba);
  message[size] = '\0';
  return obj_emit_failure(job, kernel, CTOOL_ERR_INPUT,
                          CTOOL_OBJ_DIAG_OVERLAP, 0u, message);
}

static ctool_bool obj_disk_choose_layout(ctool_u32 partition_sectors,
                                          obj_disk_layout_t *layout_out) {
  ctool_u64 partition = (ctool_u64)partition_sectors;
  ctool_u64 root_bytes =
      (ctool_u64)CTOOL_OBJ_DISK_ROOT_ENTRIES * 32u;
  ctool_u64 root_dir_sectors =
      (root_bytes + CTOOL_OBJ_DISK_SECTOR_BYTES - 1u) /
      CTOOL_OBJ_DISK_SECTOR_BYTES;
  ctool_u32 sectors_per_cluster = 1u;
  while (sectors_per_cluster <= 64u) {
    ctool_u64 sectors_per_fat = 1u;
    ctool_u64 previous_sectors_per_fat = 0u;
    for (;;) {
      ctool_u64 metadata_sectors =
          (ctool_u64)CTOOL_OBJ_DISK_RESERVED_SECTORS + root_dir_sectors +
          (ctool_u64)CTOOL_OBJ_DISK_FAT_COPIES * sectors_per_fat;
      ctool_u64 data_sectors;
      ctool_u64 clusters;
      ctool_u64 fat_bytes;
      ctool_u64 needed_fat;
      if (partition <= metadata_sectors) {
        break;
      }
      data_sectors = partition - metadata_sectors;
      clusters = data_sectors / (ctool_u64)sectors_per_cluster;
      fat_bytes = (clusters + 2u) * 2u;
      needed_fat =
          (fat_bytes + CTOOL_OBJ_DISK_SECTOR_BYTES - 1u) /
          CTOOL_OBJ_DISK_SECTOR_BYTES;
      if (needed_fat == sectors_per_fat) {
        if (clusters >= CTOOL_OBJ_DISK_FAT16_MIN_CLUSTERS &&
            clusters < CTOOL_OBJ_DISK_FAT16_MAX_CLUSTERS) {
          layout_out->sectors_per_cluster = sectors_per_cluster;
          layout_out->root_dir_sectors = (ctool_u32)root_dir_sectors;
          layout_out->sectors_per_fat = (ctool_u32)sectors_per_fat;
          return CTOOL_TRUE;
        }
        break;
      }
      if (needed_fat > (ctool_u64)0xffffffffu) {
        break;
      }
      if (needed_fat == previous_sectors_per_fat) {
        break;
      }
      previous_sectors_per_fat = sectors_per_fat;
      sectors_per_fat = needed_fat;
    }
    if (sectors_per_cluster == 64u) {
      break;
    }
    sectors_per_cluster *= 2u;
  }
  return CTOOL_FALSE;
}

static ctool_status_t obj_disk_template(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_disk_template_request_t *disk =
      &request->as.disk_template;
  const ctool_source_t *boot = request->input;
  const ctool_source_t *kernel = disk->kernel;
  obj_disk_layout_t layout;
  ctool_u64 fat_start_bytes;
  ctool_u64 kernel_end;
  ctool_u64 metadata_sectors;
  ctool_u64 template_sectors;
  ctool_u64 template_bytes;
  ctool_u64 fat_bytes_u64;
  ctool_u64 first_fat_offset_u64;
  ctool_u64 second_fat_offset_u64;
  ctool_u32 output_offset;
  ctool_u32 fat_offset;
  ctool_u32 first_fat_offset;
  ctool_u32 second_fat_offset;
  ctool_u32 partition_sectors;
  ctool_u16 total_sectors_16;
  ctool_u32 total_sectors_32;
  ctool_mut_bytes_t reserved;
  ctool_u8 *bytes;
  ctool_u8 *bpb;
  ctool_status_t status;

  obj_zero(&layout, (ctool_u32)sizeof(layout));
  if (kernel == (const ctool_source_t *)0) {
    return obj_emit_failure(job, boot, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj disk template kernel is required");
  }
  if (boot->contents.data == (const ctool_u8 *)0 &&
      boot->contents.size != 0u) {
    return obj_emit_failure(job, boot, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj bootloader bytes are invalid");
  }
  if (kernel->contents.data == (const ctool_u8 *)0 &&
      kernel->contents.size != 0u) {
    return obj_emit_failure(job, kernel, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj kernel bytes are invalid");
  }
  if (disk->fat_start_lba <= CTOOL_OBJ_DISK_BOOT_SECTORS) {
    return obj_emit_failure(
        job, boot, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
        "FAT partition must start after bootloader and kernel area");
  }
  if (disk->fat_start_lba >= disk->image_sectors) {
    return obj_emit_failure(job, boot, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "FAT partition start is beyond image size");
  }
  if (boot->contents.size <
      CTOOL_OBJ_DISK_BOOT_SECTORS * CTOOL_OBJ_DISK_SECTOR_BYTES) {
    return obj_emit_failure(job, boot, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "bootloader is too small; expected at least 5 "
                            "sectors");
  }

  fat_start_bytes = (ctool_u64)disk->fat_start_lba *
                    (ctool_u64)CTOOL_OBJ_DISK_SECTOR_BYTES;
  kernel_end =
      (ctool_u64)CTOOL_OBJ_DISK_BOOT_SECTORS *
          (ctool_u64)CTOOL_OBJ_DISK_SECTOR_BYTES +
      (ctool_u64)kernel->contents.size;
  if (kernel_end > fat_start_bytes) {
    return obj_disk_overlap_failure(job, kernel, disk->fat_start_lba);
  }

  partition_sectors = disk->image_sectors - disk->fat_start_lba;
  if (obj_disk_choose_layout(partition_sectors, &layout) == CTOOL_FALSE) {
    return obj_emit_failure(job, boot, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "cannot make FAT16 layout for this partition");
  }
  metadata_sectors =
      (ctool_u64)CTOOL_OBJ_DISK_RESERVED_SECTORS +
      (ctool_u64)CTOOL_OBJ_DISK_FAT_COPIES * layout.sectors_per_fat +
      layout.root_dir_sectors;
  template_sectors = (ctool_u64)disk->fat_start_lba + metadata_sectors;
  template_bytes =
      template_sectors * (ctool_u64)CTOOL_OBJ_DISK_SECTOR_BYTES;
  if (template_bytes > (ctool_u64)0xffffffffu) {
    return obj_emit_failure(job, boot, CTOOL_ERR_OVERFLOW,
                            CTOOL_OBJ_DIAG_LIMIT, 0u,
                            "CupidObj disk template size overflows i386");
  }

  status = ctool_buffer_reserve_zero(output, (ctool_u32)template_bytes,
                                     &output_offset, &reserved);
  if (status != CTOOL_OK) {
    ctool_u32 code = status == CTOOL_ERR_LIMIT ||
                             status == CTOOL_ERR_OVERFLOW ||
                             status == CTOOL_ERR_NO_MEMORY
                         ? CTOOL_OBJ_DIAG_LIMIT
                         : CTOOL_OBJ_DIAG_OUTPUT;
    return obj_emit_failure(job, boot, status, code, 0u,
                            "CupidObj could not emit disk template");
  }
  bytes = reserved.data;
  fat_bytes_u64 = (ctool_u64)layout.sectors_per_fat *
                  (ctool_u64)CTOOL_OBJ_DISK_SECTOR_BYTES;
  first_fat_offset_u64 =
      fat_start_bytes +
      (ctool_u64)CTOOL_OBJ_DISK_RESERVED_SECTORS *
          (ctool_u64)CTOOL_OBJ_DISK_SECTOR_BYTES;
  second_fat_offset_u64 = first_fat_offset_u64 + fat_bytes_u64;
  fat_offset = (ctool_u32)fat_start_bytes;

  obj_disk_copy(bytes, 0u, boot->contents.data,
                CTOOL_OBJ_DISK_MBR_BOOT_BYTES);
  bytes[446u] = 0x80u;
  bytes[447u] = 0xfeu;
  bytes[448u] = 0xffu;
  bytes[449u] = 0xffu;
  bytes[450u] = 0x06u;
  bytes[451u] = 0xfeu;
  bytes[452u] = 0xffu;
  bytes[453u] = 0xffu;
  obj_disk_write_le32(bytes, 454u, disk->fat_start_lba);
  obj_disk_write_le32(bytes, 458u, partition_sectors);
  bytes[510u] = 0x55u;
  bytes[511u] = 0xaau;
  obj_disk_copy(bytes, CTOOL_OBJ_DISK_SECTOR_BYTES,
                boot->contents.data + CTOOL_OBJ_DISK_SECTOR_BYTES,
                (CTOOL_OBJ_DISK_BOOT_SECTORS - 1u) *
                    CTOOL_OBJ_DISK_SECTOR_BYTES);
  obj_disk_copy(bytes,
                CTOOL_OBJ_DISK_BOOT_SECTORS *
                    CTOOL_OBJ_DISK_SECTOR_BYTES,
                kernel->contents.data, kernel->contents.size);

  bpb = bytes + fat_offset;
  bpb[0u] = 0xebu;
  bpb[1u] = 0x3cu;
  bpb[2u] = 0x90u;
  obj_disk_copy(bpb, 3u, (const ctool_u8 *)"CUPIDOS ", 8u);
  obj_disk_write_le16(bpb, 11u, (ctool_u16)CTOOL_OBJ_DISK_SECTOR_BYTES);
  bpb[13u] = (ctool_u8)layout.sectors_per_cluster;
  obj_disk_write_le16(bpb, 14u,
                      (ctool_u16)CTOOL_OBJ_DISK_RESERVED_SECTORS);
  bpb[16u] = (ctool_u8)CTOOL_OBJ_DISK_FAT_COPIES;
  obj_disk_write_le16(bpb, 17u,
                      (ctool_u16)CTOOL_OBJ_DISK_ROOT_ENTRIES);
  total_sectors_16 = partition_sectors < 65536u
                         ? (ctool_u16)partition_sectors
                         : (ctool_u16)0u;
  total_sectors_32 = total_sectors_16 != 0u ? 0u : partition_sectors;
  obj_disk_write_le16(bpb, 19u, total_sectors_16);
  bpb[21u] = 0xf8u;
  obj_disk_write_le16(bpb, 22u, (ctool_u16)layout.sectors_per_fat);
  obj_disk_write_le16(bpb, 24u, 63u);
  obj_disk_write_le16(bpb, 26u, 255u);
  obj_disk_write_le32(bpb, 28u, disk->fat_start_lba);
  obj_disk_write_le32(bpb, 32u, total_sectors_32);
  bpb[36u] = 0x80u;
  bpb[38u] = 0x29u;
  obj_disk_write_le32(bpb, 39u, 0x0c001d05u);
  obj_disk_copy(bpb, 43u, (const ctool_u8 *)"CUPIDOS    ", 11u);
  obj_disk_copy(bpb, 54u, (const ctool_u8 *)"FAT16   ", 8u);
  bpb[510u] = 0x55u;
  bpb[511u] = 0xaau;

  first_fat_offset = (ctool_u32)first_fat_offset_u64;
  second_fat_offset = (ctool_u32)second_fat_offset_u64;
  obj_disk_initialize_fat(bytes, first_fat_offset);
  obj_disk_initialize_fat(bytes, second_fat_offset);

  (void)output_offset;
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_wrap(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_wrap_binary_request_t *wrap = &request->as.wrap_binary;
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t arena_mark = ctool_arena_mark(arena);
  ctool_bytes_t contents = request->input->contents;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[3];
  ctool_elf32_object_spec_t object;
  ctool_status_t status;
  ctool_status_t rewind_status;
  ctool_u32 index;
  ctool_u32 removed = 0u;

  if (request->input->contents.data == (const ctool_u8 *)0 &&
      request->input->contents.size != 0u) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj input bytes are invalid");
  }
  if (obj_string_valid(wrap->section_name) == CTOOL_FALSE ||
      obj_power_of_two(wrap->section_alignment) == CTOOL_FALSE ||
      (wrap->section_flags & ~CTOOL_OBJ_WRAP_FLAGS) != 0u ||
      obj_string_equal(wrap->section_name, ctool_string(".symtab")) ==
          CTOOL_TRUE ||
      obj_string_equal(wrap->section_name, ctool_string(".strtab")) ==
          CTOOL_TRUE ||
      obj_string_equal(wrap->section_name, ctool_string(".shstrtab")) ==
          CTOOL_TRUE ||
      obj_string_has_prefix(wrap->section_name, ".rel.") == CTOOL_TRUE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_SECTION, 0u,
                            "CupidObj wrapped section description is invalid");
  }
  if (obj_string_valid(wrap->start_symbol) == CTOOL_FALSE ||
      obj_string_valid(wrap->end_symbol) == CTOOL_FALSE ||
      obj_string_valid(wrap->size_symbol) == CTOOL_FALSE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_SYMBOL, 0u,
                            "CupidObj wrapped symbol name is invalid");
  }
  if (obj_string_equal(wrap->start_symbol, wrap->end_symbol) == CTOOL_TRUE ||
      obj_string_equal(wrap->start_symbol, wrap->size_symbol) == CTOOL_TRUE ||
      obj_string_equal(wrap->end_symbol, wrap->size_symbol) == CTOOL_TRUE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                            "CupidObj wrapped symbol names collide");
  }

  if (request->operation == CTOOL_OBJ_WRAP_JPEG) {
    status = obj_validate_jpeg(job, request->input);
    if (status != CTOOL_OK) {
      return status;
    }
  }

  if (request->operation == CTOOL_OBJ_WRAP_TEXT) {
    ctool_u8 *normalized;
    ctool_u32 write_index = 0u;
    for (index = 0u; index + 1u < contents.size; index++) {
      if (contents.data[index] == (ctool_u8)'\r' &&
          contents.data[index + 1u] == (ctool_u8)'\n') {
        removed++;
        index++;
      }
    }
    if (removed != 0u) {
      status = ctool_arena_alloc(arena, contents.size - removed, 1u,
                                 (void **)&normalized);
      if (status != CTOOL_OK) {
        rewind_status = ctool_arena_rewind(arena, arena_mark);
        if (rewind_status != CTOOL_OK) {
          return rewind_status;
        }
        return obj_emit_failure(job, request->input, status,
                                CTOOL_OBJ_DIAG_LIMIT, 0u,
                                "CupidObj could not normalize text input");
      }
      for (index = 0u; index < contents.size; index++) {
        if (contents.data[index] == (ctool_u8)'\r' &&
            index + 1u < contents.size &&
            contents.data[index + 1u] == (ctool_u8)'\n') {
          continue;
        }
        normalized[write_index] = contents.data[index];
        write_index++;
      }
      contents = ctool_bytes(normalized, write_index);
    }
  }

  obj_zero(&section, (ctool_u32)sizeof(section));
  obj_zero(symbols, (ctool_u32)sizeof(symbols));
  obj_zero(&object, (ctool_u32)sizeof(object));
  section.name = wrap->section_name;
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = wrap->section_flags;
  section.alignment = wrap->section_alignment;
  section.size = contents.size;
  section.contents = contents;

  symbols[0].name = wrap->start_symbol;
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;

  symbols[1] = symbols[0];
  symbols[1].name = wrap->end_symbol;
  symbols[1].value = contents.size;

  symbols[2] = symbols[0];
  symbols[2].name = wrap->size_symbol;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[2].value = contents.size;

  object.sections = &section;
  object.section_count = 1u;
  object.symbols = symbols;
  object.symbol_count = 3u;
  status = ctool_elf32_write(job, &object, output);
  rewind_status = ctool_arena_rewind(arena, arena_mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  if (status != CTOOL_OK) {
    ctool_u32 code = status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                             status == CTOOL_ERR_NO_MEMORY
                         ? CTOOL_OBJ_DIAG_LIMIT
                         : CTOOL_OBJ_DIAG_OUTPUT;
    return obj_emit_failure(job, request->input, status, code, 0u,
                            "CupidObj could not emit the wrapped object");
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_extract_flat(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_elf32_object_t object;
  obj_flat_region_t *regions = (obj_flat_region_t *)0;
  ctool_u32 load_count = 0u;
  ctool_u32 file_region_count = 0u;
  ctool_u32 index;
  ctool_u32 position = 0u;
  ctool_u32 cursor;
  ctool_u32 end_address;
  ctool_status_t status = ctool_elf32_read(job, request->input, &object);

  if (status != CTOOL_OK) {
    /* The ELF reader has already rewound its temporary state and committed
     * its own precise diagnostic after that rewind. */
    return obj_emit_failure(job, request->input, status,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj input is not a valid static i386 ELF");
  }
  if (object.file_type != CTOOL_ELF32_ET_EXEC) {
    return obj_extract_failure(job, request->input, arena, mark,
                               CTOOL_ERR_UNSUPPORTED,
                               CTOOL_OBJ_DIAG_UNSUPPORTED, 0u,
                               "CupidObj flat extraction requires ET_EXEC");
  }
  for (index = 0u; index < object.program_header_count; index++) {
    if (object.program_headers[index].type == CTOOL_ELF32_PT_LOAD) {
      load_count++;
      if (object.program_headers[index].file_size != 0u) {
        file_region_count++;
      }
    }
  }
  if (load_count == 0u) {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u) {
        continue;
      }
      if (section->type == CTOOL_ELF32_SHT_NOBITS) {
        continue;
      }
      if (section->type != CTOOL_ELF32_SHT_PROGBITS) {
        return obj_extract_failure(
            job, request->input, arena, mark, CTOOL_ERR_UNSUPPORTED,
            CTOOL_OBJ_DIAG_UNSUPPORTED, section->file_offset,
            "CupidObj section fallback found unsupported allocated content");
      }
      if (section->size != 0u) {
        file_region_count++;
      }
    }
  }
  if (file_region_count == 0u) {
    return obj_extract_failure(job, request->input, arena, mark,
                               CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_NO_LOAD, 0u,
                               "CupidObj executable has no initialized load");
  }
  status = ctool_arena_alloc_zero(
      arena, file_region_count, (ctool_u32)sizeof(*regions),
      (ctool_u32)sizeof(void *), (void **)&regions);
  if (status != CTOOL_OK) {
    return obj_extract_failure(job, request->input, arena, mark, status,
                               CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj flat region limit exceeded");
  }
  if (load_count != 0u) {
    for (index = 0u; index < object.program_header_count; index++) {
      const ctool_elf32_program_header_t *header =
          &object.program_headers[index];
      if (header->type != CTOOL_ELF32_PT_LOAD || header->file_size == 0u) {
        continue;
      }
      regions[position].address = header->physical_address;
      regions[position].order = header->file_index;
      regions[position].contents = header->contents;
      position++;
    }
  } else {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
          section->type != CTOOL_ELF32_SHT_PROGBITS || section->size == 0u) {
        continue;
      }
      regions[position].address = section->address;
      regions[position].order = section->file_index;
      regions[position].contents = section->contents;
      position++;
    }
  }
  obj_region_sort(regions, file_region_count);
  cursor = regions[0].address;
  for (index = 0u; index < file_region_count; index++) {
    const obj_flat_region_t *region = &regions[index];
    if (region->address > 0xffffffffu - region->contents.size) {
      return obj_extract_failure(job, request->input, arena, mark,
                                 CTOOL_ERR_OVERFLOW,
                                 CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW,
                                 region->address,
                                 "CupidObj flat address range overflows");
    }
    end_address = region->address + region->contents.size;
    if (region->address < cursor) {
      return obj_extract_failure(job, request->input, arena, mark,
                                 CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_OVERLAP,
                                 region->address,
                                 "CupidObj initialized load ranges overlap");
    }
    status = ctool_buffer_fill(output, 0u, region->address - cursor);
    if (status == CTOOL_OK) {
      status = ctool_buffer_append(output, region->contents);
    }
    if (status != CTOOL_OK) {
      ctool_u32 code = status == CTOOL_ERR_LIMIT ||
                               status == CTOOL_ERR_OVERFLOW ||
                               status == CTOOL_ERR_NO_MEMORY
                           ? CTOOL_OBJ_DIAG_LIMIT
                           : CTOOL_OBJ_DIAG_OUTPUT;
      return obj_extract_failure(job, request->input, arena, mark, status,
                                 code, region->address,
                                 "CupidObj could not emit the flat image");
    }
    cursor = end_address;
  }
  result_out->base_address = regions[0].address;
  result_out->end_address = cursor;
  status = ctool_arena_rewind(arena, mark);
  if (status != CTOOL_OK) {
    return status;
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_bool obj_install_demo_stem(ctool_string_t path,
                                        ctool_string_t *stem_out) {
  ctool_string_t prefix = ctool_string("demos/");
  ctool_string_t suffix = ctool_string(".asm");
  ctool_u32 index;
  if (stem_out == (ctool_string_t *)0 ||
      obj_string_valid(path) == CTOOL_FALSE ||
      obj_string_has_prefix(path, "demos/") == CTOOL_FALSE ||
      obj_string_has_suffix(path, ".asm") == CTOOL_FALSE ||
      path.size <= prefix.size + suffix.size) {
    return CTOOL_FALSE;
  }
  stem_out->data = path.data + prefix.size;
  stem_out->size = path.size - prefix.size - suffix.size;
  for (index = 0u; index < stem_out->size; index++) {
    unsigned char character = (unsigned char)stem_out->data[index];
    if (!((character >= (unsigned char)'a' &&
           character <= (unsigned char)'z') ||
          (character >= (unsigned char)'A' &&
           character <= (unsigned char)'Z') ||
          (character >= (unsigned char)'0' &&
           character <= (unsigned char)'9') ||
          character == (unsigned char)'_')) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_install_plain_stem(ctool_string_t path,
                                         const char *prefix_text,
                                         const char *suffix_text,
                                         ctool_string_t *stem_out) {
  ctool_string_t prefix = ctool_string(prefix_text);
  ctool_string_t suffix = ctool_string(suffix_text);
  ctool_u32 index;
  if (stem_out == (ctool_string_t *)0 ||
      obj_string_valid(path) == CTOOL_FALSE ||
      obj_string_has_prefix(path, prefix_text) == CTOOL_FALSE ||
      obj_string_has_suffix(path, suffix_text) == CTOOL_FALSE ||
      path.size <= prefix.size + suffix.size) {
    return CTOOL_FALSE;
  }
  stem_out->data = path.data + prefix.size;
  stem_out->size = path.size - prefix.size - suffix.size;
  for (index = 0u; index < stem_out->size; index++) {
    unsigned char character = (unsigned char)stem_out->data[index];
    if (!((character >= (unsigned char)'a' &&
           character <= (unsigned char)'z') ||
          (character >= (unsigned char)'A' &&
           character <= (unsigned char)'Z') ||
          (character >= (unsigned char)'0' &&
           character <= (unsigned char)'9') ||
          character == (unsigned char)'_')) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t obj_install_emit_extern(
    ctool_buffer_t *output, const char *symbol_prefix, ctool_string_t stem,
    const char *symbol_suffix) {
  ctool_status_t status = obj_append_literal(output, "extern const char ");
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_suffix);
  }
  return status;
}

static ctool_status_t obj_install_emit_bin_entry(
    ctool_buffer_t *output, const char *symbol_prefix, ctool_string_t stem,
    const char *symbol_type, const char *install_directory,
    const char *extension, const char *log_directory) {
  ctool_status_t status = obj_append_literal(
      output, "    { uint32_t sz = (uint32_t)(");
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_type);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_end - ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_type);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "_start); ramfs_add_file(fs_private, \"");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, install_directory);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "\", ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_type);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "_start, sz); serial_printf(\"[kernel] Installed ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, log_directory);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, " (%u bytes)\\n\", sz); }\n");
  }
  return status;
}

static ctool_status_t obj_install_validate_plain_list(
    ctool_job_t *job, const ctool_source_t *source,
    const ctool_string_t *paths, ctool_u32 count, const char *prefix,
    const char *suffix, ctool_u32 column_base, const char *message) {
  ctool_u32 index;
  if (count != 0u && paths == (const ctool_string_t *)0) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, column_base,
                            "CupidObj installation path list is missing");
  }
  for (index = 0u; index < count; index++) {
    ctool_string_t stem;
    ctool_u32 prior;
    if (obj_install_plain_stem(paths[index], prefix, suffix, &stem) ==
        CTOOL_FALSE) {
      return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                              CTOOL_OBJ_DIAG_INVALID_INPUT,
                              column_base + index, message);
    }
    for (prior = 0u; prior < index; prior++) {
      if (obj_string_equal(paths[prior], paths[index]) == CTOOL_TRUE) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
            column_base + index,
            "CupidObj installation path is duplicated");
      }
    }
  }
  return CTOOL_OK;
}

static void obj_install_bin_symbol(
    const ctool_obj_install_source_request_t *install, ctool_u32 index,
    obj_install_symbol_t *symbol_out) {
  if (index < install->bin_count) {
    (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc",
                                 &symbol_out->stem);
    symbol_out->prefix = "_binary_bin_";
    symbol_out->suffix = "_cc";
    symbol_out->path = install->bin_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_BIN;
    return;
  }
  index -= install->bin_count;
  if (index < install->header_count) {
    (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h",
                                 &symbol_out->stem);
    symbol_out->prefix = "_binary_bin_";
    symbol_out->suffix = "_h";
    symbol_out->path = install->header_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_HEADER;
    return;
  }
  index -= install->header_count;
  (void)obj_install_plain_stem(install->browser_paths[index], "bin/browser/",
                               ".cc", &symbol_out->stem);
  symbol_out->prefix = "_binary_bin_browser_";
  symbol_out->suffix = "_cc";
  symbol_out->path = install->browser_paths[index];
  symbol_out->kind = OBJ_INSTALL_SYMBOL_BROWSER;
}

static ctool_status_t obj_install_validate_bin_symbols(
    ctool_job_t *job, const ctool_source_t *source,
    const ctool_obj_install_source_request_t *install) {
  ctool_u32 count =
      install->bin_count + install->header_count + install->browser_count;
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    obj_install_symbol_t current;
    ctool_u32 prior;
    obj_install_bin_symbol(install, index, &current);
    for (prior = 0u; prior < index; prior++) {
      obj_install_symbol_t earlier;
      obj_install_bin_symbol(install, prior, &earlier);
      if (obj_install_symbols_equal(&earlier, &current) == CTOOL_TRUE) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
            index,
            "CupidObj installation paths map to the same binary symbol");
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t obj_install_output_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_status_t status) {
  ctool_u32 code = status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                           status == CTOOL_ERR_NO_MEMORY
                       ? CTOOL_OBJ_DIAG_LIMIT
                       : CTOOL_OBJ_DIAG_OUTPUT;
  return obj_emit_failure(job, source, status, code, 0u,
                          "CupidObj could not emit installation source");
}

static ctool_bool obj_install_count_fits(ctool_u32 count,
                                         ctool_u32 *total) {
  if (count > CTOOL_OBJ_INSTALL_PATH_LIMIT ||
      *total > CTOOL_OBJ_INSTALL_PATH_LIMIT - count) {
    return CTOOL_FALSE;
  }
  *total += count;
  return CTOOL_TRUE;
}

static ctool_status_t obj_install_validate_path_limit(
    ctool_job_t *job, const ctool_obj_request_t *request) {
  const ctool_obj_install_source_request_t *install =
      &request->as.install_source;
  ctool_u32 total = 0u;
  if (obj_install_count_fits(install->bin_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->header_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->browser_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->ctxt_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->doc_asset_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->home_asset_count, &total) == CTOOL_FALSE ||
      obj_install_count_fits(install->demo_count, &total) == CTOOL_FALSE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_LIMIT,
                            CTOOL_OBJ_DIAG_LIMIT, 0u,
                            "CupidObj installation inventory exceeds 512 paths");
  }
  return CTOOL_OK;
}

static ctool_status_t obj_install_demos(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_install_source_request_t *install =
      &request->as.install_source;
  ctool_u32 index;
  ctool_status_t status;
  if (install->demo_paths == (const ctool_string_t *)0 ||
      install->demo_count == 0u) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj demo installation list is empty");
  }
  for (index = 0u; index < install->demo_count; index++) {
    ctool_string_t stem;
    ctool_u32 prior;
    if (obj_install_demo_stem(install->demo_paths[index], &stem) ==
        CTOOL_FALSE) {
      return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                              CTOOL_OBJ_DIAG_INVALID_INPUT, index,
                              "CupidObj demo path must match demos/NAME.asm");
    }
    for (prior = 0u; prior < index; prior++) {
      if (obj_string_equal(install->demo_paths[prior],
                           install->demo_paths[index]) == CTOOL_TRUE) {
        return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                                CTOOL_OBJ_DIAG_SYMBOL_COLLISION, index,
                                "CupidObj demo installation path is duplicated");
      }
    }
  }
  status = obj_append_literal(
      output,
      "/* Auto-generated -- do not edit. */\n"
      "/* Lists all embedded CupidASM demos from demos/ directory */\n"
      "#include \"ramfs.h\"\n"
      "#include \"types.h\"\n"
      "#include \"../drivers/serial.h\"\n");
  for (index = 0u; status == CTOOL_OK && index < install->demo_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_demo_stem(install->demo_paths[index], &stem);
    status = obj_append_literal(output,
                                "extern const char _binary_demos_");
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(output, "_asm_start[];\n");
    }
  }
  for (index = 0u; status == CTOOL_OK && index < install->demo_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_demo_stem(install->demo_paths[index], &stem);
    status = obj_append_literal(output,
                                "extern const char _binary_demos_");
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(output, "_asm_end[];\n");
    }
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output,
        "void install_demo_programs(void *fs_private);\n"
        "void install_demo_programs(void *fs_private) {\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->demo_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_demo_stem(install->demo_paths[index], &stem);
    status = obj_append_literal(
        output,
        "    { uint32_t sz = (uint32_t)(_binary_demos_");
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(output, "_asm_end - _binary_demos_");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          "_asm_start); ramfs_add_file(fs_private, \"demos/");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          ".asm\", _binary_demos_");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          "_asm_start, sz); serial_printf(\"[kernel] Installed /demos/");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          ".asm (%u bytes)\\n\", sz); ramfs_add_file(fs_private, "
          "\"docs/demos/");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          ".asm\", _binary_demos_");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(
          output,
          "_asm_start, sz); serial_printf(\"[kernel] Installed /docs/demos/");
    }
    if (status == CTOOL_OK) {
      status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
      status = obj_append_literal(output, ".asm (%u bytes)\\n\", sz); }\n");
    }
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "}\n");
  }
  if (status != CTOOL_OK) {
    return obj_install_output_failure(job, request->input, status);
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_install_bin(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_install_source_request_t *install =
      &request->as.install_source;
  ctool_status_t status;
  ctool_u32 index;
  if (install->bin_count + install->header_count + install->browser_count ==
      0u) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj bin installation lists are empty");
  }
  status = obj_install_validate_plain_list(
      job, request->input, install->bin_paths, install->bin_count, "bin/",
      ".cc", 0u, "CupidObj program path must match bin/NAME.cc");
  if (status == CTOOL_OK) {
    status = obj_install_validate_plain_list(
        job, request->input, install->header_paths, install->header_count,
        "bin/", ".h", install->bin_count,
        "CupidObj header path must match bin/NAME.h");
  }
  if (status == CTOOL_OK) {
    status = obj_install_validate_plain_list(
        job, request->input, install->browser_paths, install->browser_count,
        "bin/browser/", ".cc", install->bin_count + install->header_count,
        "CupidObj browser path must match bin/browser/NAME.cc");
  }
  if (status == CTOOL_OK) {
    status = obj_install_validate_bin_symbols(job, request->input, install);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = obj_append_literal(
      output,
      "/* Auto-generated -- do not edit. */\n"
      "/* Lists all embedded CupidC programs from bin/ directory */\n"
      "#include \"ramfs.h\"\n"
      "#include \"types.h\"\n"
      "#include \"../drivers/serial.h\"\n");
  for (index = 0u; status == CTOOL_OK && index < install->bin_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc",
                                 &stem);
    status = obj_install_emit_extern(output, "_binary_bin_", stem,
                                     "_cc_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->header_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h",
                                 &stem);
    status = obj_install_emit_extern(output, "_binary_bin_", stem,
                                     "_h_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->bin_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc",
                                 &stem);
    status = obj_install_emit_extern(output, "_binary_bin_", stem,
                                     "_cc_end[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->header_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h",
                                 &stem);
    status = obj_install_emit_extern(output, "_binary_bin_", stem,
                                     "_h_end[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->browser_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->browser_paths[index],
                                 "bin/browser/", ".cc", &stem);
    status = obj_install_emit_extern(output, "_binary_bin_browser_", stem,
                                     "_cc_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->browser_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->browser_paths[index],
                                 "bin/browser/", ".cc", &stem);
    status = obj_install_emit_extern(output, "_binary_bin_browser_", stem,
                                     "_cc_end[];\n");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output,
        "void install_bin_programs(void *fs_private);\n"
        "void install_bin_programs(void *fs_private) {\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->bin_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc",
                                 &stem);
    status = obj_install_emit_bin_entry(output, "_binary_bin_", stem, "_cc",
                                        "bin/", ".cc", "/bin/");
  }
  for (index = 0u; status == CTOOL_OK && index < install->header_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h",
                                 &stem);
    status = obj_install_emit_bin_entry(output, "_binary_bin_", stem, "_h",
                                        "bin/", ".h", "/bin/");
  }
  for (index = 0u; status == CTOOL_OK && index < install->browser_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_plain_stem(install->browser_paths[index],
                                 "bin/browser/", ".cc", &stem);
    status = obj_install_emit_bin_entry(
        output, "_binary_bin_browser_", stem, "_cc", "bin/browser/", ".cc",
        "/bin/browser/");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "}\n");
  }
  if (status != CTOOL_OK) {
    return obj_install_output_failure(job, request->input, status);
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_bool obj_install_docs_stem(ctool_string_t path,
                                        const char *prefix_text,
                                        const char *suffix_text,
                                        ctool_string_t *stem_out) {
  ctool_string_t prefix = ctool_string(prefix_text);
  ctool_string_t suffix = ctool_string(suffix_text);
  ctool_u32 index;
  if (stem_out == (ctool_string_t *)0 ||
      obj_string_valid(path) == CTOOL_FALSE ||
      obj_string_has_prefix(path, prefix_text) == CTOOL_FALSE ||
      obj_string_has_suffix(path, suffix_text) == CTOOL_FALSE ||
      path.size <= prefix.size + suffix.size) {
    return CTOOL_FALSE;
  }
  stem_out->data = path.data + prefix.size;
  stem_out->size = path.size - prefix.size - suffix.size;
  for (index = 0u; index < stem_out->size; index++) {
    unsigned char character = (unsigned char)stem_out->data[index];
    if (!((character >= (unsigned char)'a' &&
           character <= (unsigned char)'z') ||
          (character >= (unsigned char)'A' &&
           character <= (unsigned char)'Z') ||
          (character >= (unsigned char)'0' &&
           character <= (unsigned char)'9') ||
          character == (unsigned char)'_' ||
          character == (unsigned char)'-')) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t obj_install_append_symbol_stem(
    ctool_buffer_t *output, ctool_string_t stem) {
  ctool_u32 index;
  for (index = 0u; index < stem.size; index++) {
    ctool_u8 character = (ctool_u8)stem.data[index];
    ctool_status_t status;
    if (character == (ctool_u8)'-') {
      character = (ctool_u8)'_';
    }
    status = ctool_buffer_put_u8(output, character);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t obj_install_emit_docs_extern(
    ctool_buffer_t *output, const char *symbol_prefix, ctool_string_t stem,
    const char *symbol_extension, const char *boundary) {
  ctool_status_t status = obj_append_literal(output, "extern const char ");
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, boundary);
  }
  return status;
}

static ctool_status_t obj_install_emit_docs_entry(
    ctool_buffer_t *output, const char *symbol_prefix, ctool_string_t stem,
    const char *symbol_extension, const char *install_directory,
    const char *file_extension, const char *log_directory) {
  ctool_status_t status = obj_append_literal(
      output, "    { uint32_t sz = (uint32_t)(");
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_end - ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "_start); ramfs_add_file(fs_private, \"");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, install_directory);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, file_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "\", ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_prefix);
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, symbol_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "_start, sz); serial_printf(\"[kernel] Installed ");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, log_directory);
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, file_extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, " (%u bytes)\\n\", sz); }\n");
  }
  return status;
}

static ctool_status_t obj_install_emit_home_entry(
    ctool_buffer_t *output, ctool_string_t stem, const char *extension) {
  ctool_status_t status = obj_append_literal(
      output, "    { uint32_t sz = (uint32_t)(_binary_");
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_end - _binary_");
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output, "_start); install_home_asset(\"/home/");
  }
  if (status == CTOOL_OK) {
    status = obj_append_string(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, ".");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "\", _binary_");
  }
  if (status == CTOOL_OK) {
    status = obj_install_append_symbol_stem(output, stem);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, extension);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "_start, sz); }\n");
  }
  return status;
}

static ctool_u32 obj_install_home_extension(ctool_string_t path,
                                             ctool_string_t *stem_out) {
  static const char *extensions[] = {".bmp", ".png", ".jpg", ".jpeg"};
  ctool_u32 index;
  for (index = 0u; index < 4u; index++) {
    if (obj_install_docs_stem(path, "", extensions[index], stem_out) ==
        CTOOL_TRUE) {
      return index + 1u;
    }
  }
  return 0u;
}

static void obj_install_docs_symbol(
    const ctool_obj_install_source_request_t *install, ctool_u32 index,
    obj_install_symbol_t *symbol_out) {
  static const char *home_symbol_suffixes[] = {"_bmp", "_png", "_jpg",
                                               "_jpeg"};
  ctool_u32 extension;
  if (index < install->ctxt_count) {
    (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/",
                                ".CTXT", &symbol_out->stem);
    symbol_out->prefix = "_binary_cupidos_txt_";
    symbol_out->suffix = "_CTXT";
    symbol_out->path = install->ctxt_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_CTXT;
    return;
  }
  index -= install->ctxt_count;
  if (index < install->doc_asset_count) {
    (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp",
                                &symbol_out->stem);
    symbol_out->prefix = "_binary_";
    symbol_out->suffix = "_bmp";
    symbol_out->path = install->doc_asset_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_DOC_ASSET;
    return;
  }
  index -= install->doc_asset_count;
  extension =
      obj_install_home_extension(install->home_asset_paths[index],
                                 &symbol_out->stem);
  symbol_out->prefix = "_binary_";
  symbol_out->suffix = home_symbol_suffixes[extension - 1u];
  symbol_out->path = install->home_asset_paths[index];
  symbol_out->kind = OBJ_INSTALL_SYMBOL_HOME_ASSET;
}

static ctool_bool obj_install_docs_alias_allowed(
    const obj_install_symbol_t *left,
    const obj_install_symbol_t *right) {
  if (obj_string_equal(left->path, right->path) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  return ((left->kind == OBJ_INSTALL_SYMBOL_DOC_ASSET &&
           right->kind == OBJ_INSTALL_SYMBOL_HOME_ASSET) ||
          (left->kind == OBJ_INSTALL_SYMBOL_HOME_ASSET &&
           right->kind == OBJ_INSTALL_SYMBOL_DOC_ASSET))
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t obj_install_validate_docs_symbols(
    ctool_job_t *job, const ctool_source_t *source,
    const ctool_obj_install_source_request_t *install) {
  ctool_u32 count = install->ctxt_count + install->doc_asset_count +
                    install->home_asset_count;
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    obj_install_symbol_t current;
    ctool_u32 prior;
    obj_install_docs_symbol(install, index, &current);
    for (prior = 0u; prior < index; prior++) {
      obj_install_symbol_t earlier;
      obj_install_docs_symbol(install, prior, &earlier);
      if (obj_install_symbols_equal(&earlier, &current) == CTOOL_TRUE &&
          obj_install_docs_alias_allowed(&earlier, &current) == CTOOL_FALSE) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
            index,
            "CupidObj installation paths map to the same binary symbol");
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t obj_install_validate_docs_list(
    ctool_job_t *job, const ctool_source_t *source,
    const ctool_string_t *paths, ctool_u32 count, const char *prefix,
    const char *suffix, ctool_u32 column_base, const char *message) {
  ctool_u32 index;
  if (count != 0u && paths == (const ctool_string_t *)0) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, column_base,
                            "CupidObj installation path list is missing");
  }
  for (index = 0u; index < count; index++) {
    ctool_string_t stem;
    ctool_u32 prior;
    if (obj_install_docs_stem(paths[index], prefix, suffix, &stem) ==
        CTOOL_FALSE) {
      return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                              CTOOL_OBJ_DIAG_INVALID_INPUT,
                              column_base + index, message);
    }
    for (prior = 0u; prior < index; prior++) {
      if (obj_string_equal(paths[prior], paths[index]) == CTOOL_TRUE) {
        return obj_emit_failure(
            job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
            column_base + index,
            "CupidObj installation path is duplicated");
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t obj_install_docs(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_install_source_request_t *install =
      &request->as.install_source;
  static const char *home_extensions[] = {"bmp", "png", "jpg", "jpeg"};
  static const char *home_symbol_suffixes[] = {"_bmp", "_png", "_jpg",
                                               "_jpeg"};
  ctool_u32 index;
  ctool_status_t status;
  if (install->ctxt_count + install->doc_asset_count +
          install->home_asset_count ==
      0u) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj docs installation lists are empty");
  }
  status = obj_install_validate_docs_list(
      job, request->input, install->ctxt_paths, install->ctxt_count,
      "cupidos-txt/", ".CTXT", 0u,
      "CupidObj manual path must match cupidos-txt/NAME.CTXT");
  if (status == CTOOL_OK) {
    status = obj_install_validate_docs_list(
        job, request->input, install->doc_asset_paths,
        install->doc_asset_count, "", ".bmp", install->ctxt_count,
        "CupidObj documentation asset must match NAME.bmp");
  }
  if (status == CTOOL_OK && install->home_asset_count != 0u &&
      install->home_asset_paths == (const ctool_string_t *)0) {
    status = obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_OBJ_DIAG_INVALID_REQUEST,
                              install->ctxt_count + install->doc_asset_count,
                              "CupidObj home asset list is missing");
  }
  for (index = 0u; status == CTOOL_OK && index < install->home_asset_count;
       index++) {
    ctool_string_t stem;
    ctool_u32 prior;
    if (obj_install_home_extension(install->home_asset_paths[index], &stem) ==
        0u) {
      status = obj_emit_failure(
          job, request->input, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
          install->ctxt_count + install->doc_asset_count + index,
          "CupidObj home asset must be NAME.bmp, NAME.png, NAME.jpg, or NAME.jpeg");
      break;
    }
    for (prior = 0u; prior < index; prior++) {
      if (obj_string_equal(install->home_asset_paths[prior],
                           install->home_asset_paths[index]) == CTOOL_TRUE) {
        status = obj_emit_failure(
            job, request->input, CTOOL_ERR_INPUT,
            CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
            install->ctxt_count + install->doc_asset_count + index,
            "CupidObj installation path is duplicated");
        break;
      }
    }
  }
  if (status == CTOOL_OK) {
    status = obj_install_validate_docs_symbols(job, request->input, install);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = obj_append_literal(
      output,
      "/* Auto-generated -- do not edit. */\n"
      "/* Lists all embedded CupidDoc files from cupidos-txt/ directory */\n"
      "#include \"homefs.h\"\n"
      "#include \"ramfs.h\"\n"
      "#include \"types.h\"\n"
      "#include \"vfs.h\"\n"
      "#include \"../drivers/serial.h\"\n");
  for (index = 0u; status == CTOOL_OK && index < install->ctxt_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/",
                                ".CTXT", &stem);
    status = obj_install_emit_docs_extern(
        output, "_binary_cupidos_txt_", stem, "_CTXT", "_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp",
                                &stem);
    status = obj_install_emit_docs_extern(output, "_binary_", stem, "_bmp",
                                          "_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->home_asset_count;
       index++) {
    ctool_string_t stem;
    ctool_u32 extension =
        obj_install_home_extension(install->home_asset_paths[index], &stem);
    status = obj_install_emit_docs_extern(
        output, "_binary_", stem, home_symbol_suffixes[extension - 1u],
        "_start[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->ctxt_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/",
                                ".CTXT", &stem);
    status = obj_install_emit_docs_extern(
        output, "_binary_cupidos_txt_", stem, "_CTXT", "_end[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp",
                                &stem);
    status = obj_install_emit_docs_extern(output, "_binary_", stem, "_bmp",
                                          "_end[];\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->home_asset_count;
       index++) {
    ctool_string_t stem;
    ctool_u32 extension =
        obj_install_home_extension(install->home_asset_paths[index], &stem);
    status = obj_install_emit_docs_extern(
        output, "_binary_", stem, home_symbol_suffixes[extension - 1u],
        "_end[];\n");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(
        output,
        "static void install_home_asset(const char *path, const char *data, uint32_t size) {\n"
        "    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);\n"
        "    if (fd < 0) { serial_printf(\"[kernel] Failed to open %s (%d)\\n\", path, fd); return; }\n"
        "    uint32_t off = 0;\n"
        "    while (off < size) {\n"
        "        int n = vfs_write(fd, data + off, size - off);\n"
        "        if (n <= 0) break;\n"
        "        off += (uint32_t)n;\n"
        "    }\n"
        "    vfs_close(fd);\n"
        "    serial_printf(\"[kernel] Installed %s (%u bytes)\\n\", path, off);\n"
        "}\n"
        "void install_docs_programs(void *fs_private);\n"
        "void install_docs_programs(void *fs_private) {\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->ctxt_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/",
                                ".CTXT", &stem);
    status = obj_install_emit_docs_entry(
        output, "_binary_cupidos_txt_", stem, "_CTXT", "docs/", ".ctxt",
        "/docs/");
  }
  for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count;
       index++) {
    ctool_string_t stem;
    (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp",
                                &stem);
    status = obj_install_emit_docs_entry(output, "_binary_", stem, "_bmp",
                                         "docs/", ".bmp", "/docs/");
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "    homefs_seed_begin();\n");
  }
  for (index = 0u; status == CTOOL_OK && index < install->home_asset_count;
       index++) {
    ctool_string_t stem;
    ctool_u32 extension =
        obj_install_home_extension(install->home_asset_paths[index], &stem);
    status = obj_install_emit_home_entry(output, stem,
                                         home_extensions[extension - 1u]);
  }
  if (status == CTOOL_OK) {
    status = obj_append_literal(output, "    homefs_seed_end();\n}\n");
  }
  if (status != CTOOL_OK) {
    return obj_install_output_failure(job, request->input, status);
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_install_source(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_install_source_request_t *install =
      &request->as.install_source;
  ctool_status_t status = obj_install_validate_path_limit(job, request);
  if (status != CTOOL_OK) {
    return status;
  }
  if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_DEMOS) {
    if (install->bin_paths != (const ctool_string_t *)0 ||
        install->bin_count != 0u ||
        install->header_paths != (const ctool_string_t *)0 ||
        install->header_count != 0u ||
        install->browser_paths != (const ctool_string_t *)0 ||
        install->browser_count != 0u ||
        install->ctxt_paths != (const ctool_string_t *)0 ||
        install->ctxt_count != 0u ||
        install->doc_asset_paths != (const ctool_string_t *)0 ||
        install->doc_asset_count != 0u ||
        install->home_asset_paths != (const ctool_string_t *)0 ||
        install->home_asset_count != 0u) {
      return obj_emit_failure(
          job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
          "CupidObj demo request contains another installation category");
    }
    return obj_install_demos(job, request, output, result_out);
  }
  if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_BIN) {
    if (install->ctxt_paths != (const ctool_string_t *)0 ||
        install->ctxt_count != 0u ||
        install->doc_asset_paths != (const ctool_string_t *)0 ||
        install->doc_asset_count != 0u ||
        install->home_asset_paths != (const ctool_string_t *)0 ||
        install->home_asset_count != 0u ||
        install->demo_paths != (const ctool_string_t *)0 ||
        install->demo_count != 0u) {
      return obj_emit_failure(
          job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
          "CupidObj bin request contains another installation category");
    }
    return obj_install_bin(job, request, output, result_out);
  }
  if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_DOCS) {
    if (install->bin_paths != (const ctool_string_t *)0 ||
        install->bin_count != 0u ||
        install->header_paths != (const ctool_string_t *)0 ||
        install->header_count != 0u ||
        install->browser_paths != (const ctool_string_t *)0 ||
        install->browser_count != 0u ||
        install->demo_paths != (const ctool_string_t *)0 ||
        install->demo_count != 0u) {
      return obj_emit_failure(
          job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
          "CupidObj docs request contains another installation category");
    }
    return obj_install_docs(job, request, output, result_out);
  }
  return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                          CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                          "CupidObj installation source kind is invalid");
}

ctool_status_t ctool_obj_transform(ctool_job_t *job,
                                    const ctool_obj_request_t *request,
                                    ctool_buffer_t *output,
                                    ctool_obj_result_t *result_out) {
  const ctool_source_t *source =
      request != (const ctool_obj_request_t *)0 ? request->input
                                                : (const ctool_source_t *)0;
  ctool_u32 output_mark;
  ctool_status_t status;
  if (result_out == (ctool_obj_result_t *)0) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj result is required");
  }
  obj_zero(result_out, (ctool_u32)sizeof(*result_out));
  if (job == (ctool_job_t *)0 ||
      request == (const ctool_obj_request_t *)0 ||
      request->input == (const ctool_source_t *)0 ||
      output == (ctool_buffer_t *)0 || ctool_buffer_view(output).size != 0u) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj request and empty output are required");
  }
  output_mark = ctool_buffer_mark(output);
  if (request->operation == CTOOL_OBJ_WRAP_BINARY ||
      request->operation == CTOOL_OBJ_WRAP_TEXT ||
      request->operation == CTOOL_OBJ_WRAP_JPEG) {
    status = obj_wrap(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_EXTRACT_FLAT) {
    status = obj_extract_flat(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE) {
    status = obj_install_source(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_GENERATE_KSYMS_SOURCE) {
    status = obj_ksyms_source(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_BUILD_DISK_TEMPLATE) {
    status = obj_disk_template(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_BUILD_ISO_FIXTURE) {
    status = obj_iso_fixture(job, request, output, result_out);
  } else {
    status = obj_emit_failure(job, request->input,
                              CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                              "CupidObj operation is invalid");
  }
  if (status != CTOOL_OK) {
    (void)ctool_buffer_rewind(output, output_mark);
    obj_zero(result_out, (ctool_u32)sizeof(*result_out));
  }
  return status;
}
