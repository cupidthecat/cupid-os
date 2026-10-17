#include "ctool_host.h"
#include "ctool_kernel.h"
#include "dis.h"
#include "kernel.h"

#define CAPTURE_BYTES 2048u

static ctool_host_adapter_t adapter;
static char capture[CAPTURE_BYTES];
static ctool_u32 capture_size;
static ctool_bool capture_overflowed;

void print(const char *text);

typedef struct {
  const char *path;
  const ctool_u8 *bytes;
  ctool_u32 size;
  ctool_u32 reported_size;
  ctool_status_t read_status;
} memory_file_t;

static const ctool_u8 file_code16[] = {0xb8u, 0x34u, 0x12u, 0xc3u};
static const ctool_u8 file_code32[] = {
    0xb8u, 0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
static const ctool_u8 file_mixed[] = {
    0xb8u, 0x34u, 0x12u, 0x90u, 0xb8u,
    0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
static const ctool_u8 file_truncated[] = {0x0fu};
static const char file_map_v1[] =
    "cupid.raw-map.v1\nsize 10\nbase 0x8000\n"
    "range 0 code16\nrange 3 data\nrange 4 code32\n";
static const char file_map_v2[] =
    "cupid.raw-map.v2\nsize 10\nbase 0x8000\nedges 0\n"
    "range 0 code16\nrange 3 data\nrange 4 code32\n";
static const char file_map_wrong_size[] =
    "cupid.raw-map.v1\nsize 11\nbase 0x8000\nrange 0 code32\n";
static const char file_map_malformed[] =
    "cupid.raw-map.v1\nsize 10\nbase 0x8000\nrange 1 code32\n";

#define MEMORY_BYTES(path, bytes, reported, result) \
  {path, bytes, (ctool_u32)sizeof(bytes), reported, result}
#define MEMORY_TEXT(path, bytes) \
  {path, (const ctool_u8 *)bytes, (ctool_u32)(sizeof(bytes) - 1u), \
   (ctool_u32)(sizeof(bytes) - 1u), CTOOL_OK}

static const memory_file_t memory_files[] = {
    MEMORY_BYTES("/raw16.bin", file_code16,
                 (ctool_u32)sizeof(file_code16), CTOOL_OK),
    MEMORY_BYTES("/raw32.bin", file_code32,
                 (ctool_u32)sizeof(file_code32), CTOOL_OK),
    MEMORY_BYTES("/mixed.bin", file_mixed,
                 (ctool_u32)sizeof(file_mixed), CTOOL_OK),
    MEMORY_BYTES("/truncated.bin", file_truncated,
                 (ctool_u32)sizeof(file_truncated), CTOOL_OK),
    MEMORY_BYTES("/read-fails.bin", file_code32,
                 (ctool_u32)sizeof(file_code32), CTOOL_ERR_IO),
    MEMORY_BYTES("/oversize.bin", file_code32, 67108865u, CTOOL_OK),
    MEMORY_TEXT("/mixed-v1.cupidmap", file_map_v1),
    MEMORY_TEXT("/mixed-v2.cupidmap", file_map_v2),
    MEMORY_TEXT("/wrong-size.cupidmap", file_map_wrong_size),
    MEMORY_TEXT("/malformed.cupidmap", file_map_malformed),
    {"/oversize.cupidmap", (const ctool_u8 *)file_map_v1,
     (ctool_u32)(sizeof(file_map_v1) - 1u), 1048577u, CTOOL_OK}};

static ctool_u32 memory_overlimit_read_calls;
static ctool_u32 memory_write_calls;

static ctool_bool string_is(ctool_string_t value, const char *expected) {
  ctool_u32 index = 0u;
  if (value.data == (const char *)0 || expected == (const char *)0) {
    return CTOOL_FALSE;
  }
  while (expected[index] != '\0') {
    if (index >= value.size || value.data[index] != expected[index]) {
      return CTOOL_FALSE;
    }
    index++;
  }
  return index == value.size ? CTOOL_TRUE : CTOOL_FALSE;
}

static const memory_file_t *memory_find(ctool_string_t path) {
  ctool_u32 index;
  for (index = 0u;
       index < (ctool_u32)(sizeof(memory_files) / sizeof(memory_files[0]));
       index++) {
    if (string_is(path, memory_files[index].path) != CTOOL_FALSE) {
      return &memory_files[index];
    }
  }
  return (const memory_file_t *)0;
}

static ctool_status_t memory_size(void *context, ctool_string_t path,
                                  ctool_u32 *size_out) {
  const memory_file_t *file = memory_find(path);
  (void)context;
  if (size_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *size_out = 0u;
  if (file == (const memory_file_t *)0) {
    return CTOOL_ERR_NOT_FOUND;
  }
  *size_out = file->reported_size;
  return CTOOL_OK;
}

static ctool_status_t memory_read(void *context, ctool_string_t path,
                                  ctool_u8 *destination, ctool_u32 size) {
  const memory_file_t *file = memory_find(path);
  ctool_u32 index;
  (void)context;
  if (file == (const memory_file_t *)0) {
    return CTOOL_ERR_NOT_FOUND;
  }
  if (file->reported_size != file->size) {
    memory_overlimit_read_calls++;
  }
  if (file->read_status != CTOOL_OK) {
    return file->read_status;
  }
  if (size != file->size || (size != 0u && destination == (ctool_u8 *)0)) {
    return CTOOL_ERR_IO;
  }
  for (index = 0u; index < size; index++) {
    destination[index] = file->bytes[index];
  }
  return CTOOL_OK;
}

static ctool_status_t memory_write(void *context, ctool_string_t path,
                                   ctool_bytes_t bytes) {
  (void)context;
  (void)path;
  (void)bytes;
  memory_write_calls++;
  return CTOOL_ERR_IO;
}

ctool_job_config_t ctool_kernel_job_config(ctool_limits_t limits) {
  ctool_job_config_t config = ctool_host_job_config(&adapter, limits);
  config.files.context = (void *)0;
  config.files.file_size = memory_size;
  config.files.read_exact = memory_read;
  config.files.write_all = memory_write;
  return config;
}

void print(const char *text) {
  ctool_u32 index = 0u;
  if (text == (const char *)0) {
    return;
  }
  while (text[index] != '\0') {
    if (capture_size + 1u >= CAPTURE_BYTES) {
      capture_overflowed = CTOOL_TRUE;
      return;
    }
    capture[capture_size] = text[index];
    capture_size++;
    index++;
  }
  capture[capture_size] = '\0';
}

static void capture_reset(void) {
  capture[0] = '\0';
  capture_size = 0u;
  capture_overflowed = CTOOL_FALSE;
}

static ctool_bool text_contains(const char *text, const char *needle) {
  ctool_u32 start;
  if (text == (const char *)0 || needle == (const char *)0 ||
      needle[0] == '\0') {
    return CTOOL_FALSE;
  }
  for (start = 0u; text[start] != '\0'; start++) {
    ctool_u32 index = 0u;
    while (needle[index] != '\0' && text[start + index] == needle[index]) {
      index++;
    }
    if (needle[index] == '\0') {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static int check_legacy_32_bit(void) {
  static const uint8_t code[] = {0xb8u, 0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
  static const dis_sym_t symbols[] = {{0x00400000u, "entry"}};
  capture_reset();
  dis_disassemble(code, (uint32_t)sizeof(code), 0x00400000u, symbols, 1,
                  print);
  return capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "00400000 <entry>:") == CTOOL_TRUE &&
                 text_contains(capture, "mov eax, 0x12345678") == CTOOL_TRUE &&
                 text_contains(capture, "ret") == CTOOL_TRUE
             ? 0
             : 1;
}

static int check_fixed_16_bit(void) {
  static const uint8_t code[] = {0xb8u, 0x34u, 0x12u, 0xc3u};
  dis_raw_request_t request;
  int status;
  request.mode = CTOOL_X86_MODE_16;
  request.base_address = 0x00007c00u;
  request.ranges = (const ctool_dis_raw_range_t *)0;
  request.range_count = 0u;
  request.require_known = CTOOL_TRUE;
  capture_reset();
  status = dis_disassemble_raw(code, (uint32_t)sizeof(code), &request,
                               (const dis_sym_t *)0, 0, print);
  return status == 0 && capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "mov ax, 0x1234") == CTOOL_TRUE &&
                 text_contains(capture, "mov eax") == CTOOL_FALSE
             ? 0
             : 1;
}

static int check_typed_map(void) {
  static const uint8_t image[] = {0xb8u, 0x34u, 0x12u, 0x90u, 0xb8u,
                                  0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
  static const ctool_dis_raw_range_t ranges[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE16},
      {3u, CTOOL_DIS_RAW_RANGE_DATA},
      {4u, CTOOL_DIS_RAW_RANGE_CODE32}};
  static const dis_sym_t symbols[] = {{0x00008003u, "literal"}};
  dis_raw_request_t request;
  int status;
  request.mode = CTOOL_DIS_RAW_RANGE_MAP;
  request.base_address = 0x00008000u;
  request.ranges = ranges;
  request.range_count = (ctool_u32)(sizeof(ranges) / sizeof(ranges[0]));
  request.require_known = CTOOL_TRUE;
  capture_reset();
  status = dis_disassemble_raw(image, (uint32_t)sizeof(image), &request,
                               symbols, 1, print);
  return status == 0 && capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "mov ax, 0x1234") == CTOOL_TRUE &&
                 text_contains(capture, "00008003 <literal>:") == CTOOL_TRUE &&
                 text_contains(capture, "db 0x90") == CTOOL_TRUE &&
                 text_contains(capture, "mov eax, 0x12345678") == CTOOL_TRUE &&
                 text_contains(capture, "nop") == CTOOL_FALSE
             ? 0
             : 1;
}

static int check_strict_rejection(const uint8_t *code, uint32_t size,
                                  uint32_t base_address) {
  dis_raw_request_t request;
  int status;
  request.mode = CTOOL_X86_MODE_32;
  request.base_address = base_address;
  request.ranges = (const ctool_dis_raw_range_t *)0;
  request.range_count = 0u;
  request.require_known = CTOOL_TRUE;
  capture_reset();
  status = dis_disassemble_raw(code, size, &request, (const dis_sym_t *)0, 0,
                               print);
  return status != 0 && capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "code check failed") == CTOOL_TRUE &&
                 text_contains(capture, "[disassembly raw]") == CTOOL_FALSE
             ? 0
             : 1;
}

static int check_strict_failures_and_legacy_fallback(void) {
  static const uint8_t unknown[] = {0xd6u};
  static const uint8_t invalid[] = {0x66u, 0x66u, 0x90u};
  static const uint8_t truncated[] = {0x0fu};
  if (check_strict_rejection(unknown, (uint32_t)sizeof(unknown),
                             0x00009000u) != 0) {
    return 1;
  }
  if (check_strict_rejection(invalid, (uint32_t)sizeof(invalid),
                             0x00009100u) != 0) {
    return 2;
  }
  if (check_strict_rejection(truncated, (uint32_t)sizeof(truncated),
                             0x00009200u) != 0) {
    return 3;
  }
  capture_reset();
  dis_disassemble(truncated, (uint32_t)sizeof(truncated), 0x00009200u,
                  (const dis_sym_t *)0, 0, print);
  if (capture_overflowed != CTOOL_FALSE) {
    return 4;
  }
  if (text_contains(capture, "db 0x0F") != CTOOL_TRUE) {
    return 5;
  }
  return 0;
}

static int check_invalid_map_and_recovery(void) {
  static const uint8_t code[] = {0x90u, 0xc3u};
  static const ctool_dis_raw_range_t bad_ranges[] = {
      {1u, CTOOL_DIS_RAW_RANGE_CODE32}};
  dis_raw_request_t request;
  int status;
  request.mode = CTOOL_DIS_RAW_RANGE_MAP;
  request.base_address = 0x0000a000u;
  request.ranges = bad_ranges;
  request.range_count = 1u;
  request.require_known = CTOOL_TRUE;
  capture_reset();
  status = dis_disassemble_raw(code, (uint32_t)sizeof(code), &request,
                               (const dis_sym_t *)0, 0, print);
  if (status == 0 || capture_overflowed != CTOOL_FALSE ||
      text_contains(capture, "raw range map must start at offset zero") !=
          CTOOL_TRUE ||
      text_contains(capture, "[disassembly raw]") != CTOOL_FALSE) {
    return 1;
  }
  request.mode = CTOOL_X86_MODE_32;
  request.ranges = (const ctool_dis_raw_range_t *)0;
  request.range_count = 0u;
  capture_reset();
  status = dis_disassemble_raw(code, (uint32_t)sizeof(code), &request,
                               (const dis_sym_t *)0, 0, print);
  return status == 0 && capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "nop") == CTOOL_TRUE &&
                 text_contains(capture, "ret") == CTOOL_TRUE
             ? 0
             : 1;
}

static ctool_bool command_is_cleared(const dis_command_t *command) {
  return command->input.data == (const char *)0 && command->input.size == 0u &&
                 command->range_map.data == (const char *)0 &&
                 command->range_map.size == 0u && command->raw == CTOOL_FALSE &&
                 command->require_known == CTOOL_FALSE &&
                 command->mode == (ctool_x86_mode_t)0 &&
                 command->base_address == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static void command_poison(dis_command_t *command) {
  command->input = ctool_string("old-input");
  command->range_map = ctool_string("old-map");
  command->raw = CTOOL_TRUE;
  command->require_known = CTOOL_TRUE;
  command->mode = CTOOL_X86_MODE_16;
  command->base_address = 123u;
}

static int check_command_successes(void) {
  static const struct {
    const char *arguments;
    const char *input;
    const char *map;
    ctool_bool raw;
    ctool_bool strict;
    ctool_x86_mode_t mode;
    ctool_u32 base;
  } cases[] = {
      {"program.elf", "program.elf", (const char *)0, CTOOL_FALSE,
       CTOOL_FALSE, (ctool_x86_mode_t)0, 0u},
      {"--raw --mode 16 --base 0x7c00 raw.bin", "raw.bin",
       (const char *)0, CTOOL_TRUE, CTOOL_FALSE, CTOOL_X86_MODE_16, 0x7c00u},
      {"raw.bin --base 4096 --require-known --raw --mode 32", "raw.bin",
       (const char *)0, CTOOL_TRUE, CTOOL_TRUE, CTOOL_X86_MODE_32, 4096u},
      {"--raw --base 0 --mode 32 -- raw.bin", "raw.bin", (const char *)0,
       CTOOL_TRUE, CTOOL_FALSE, CTOOL_X86_MODE_32, 0u},
      {"--mode 32 --base 4294967295 --raw raw.bin", "raw.bin",
       (const char *)0, CTOOL_TRUE, CTOOL_FALSE, CTOOL_X86_MODE_32, 0xffffffffu},
      {"--raw --mode 16 --base 0xFFFFFFFF raw.bin", "raw.bin",
       (const char *)0, CTOOL_TRUE, CTOOL_FALSE, CTOOL_X86_MODE_16, 0xffffffffu},
      {"--raw --range-map map.cupidmap raw.bin", "raw.bin", "map.cupidmap",
       CTOOL_TRUE, CTOOL_FALSE, CTOOL_DIS_RAW_RANGE_MAP, 0u},
      {"--range-map map.cupidmap --require-known --raw -- raw.bin", "raw.bin",
       "map.cupidmap", CTOOL_TRUE, CTOOL_TRUE, CTOOL_DIS_RAW_RANGE_MAP, 0u},
      {"-- --raw", "--raw", (const char *)0, CTOOL_FALSE, CTOOL_FALSE,
       (ctool_x86_mode_t)0, 0u}};
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       index++) {
    dis_command_t command;
    command_poison(&command);
    if (dis_command_parse(ctool_string(cases[index].arguments), &command) !=
            CTOOL_OK ||
        string_is(command.input, cases[index].input) == CTOOL_FALSE ||
        command.raw != cases[index].raw ||
        command.require_known != cases[index].strict) {
      return (int)index + 1;
    }
    if (cases[index].map == (const char *)0) {
      if (command.range_map.data != (const char *)0 ||
          command.range_map.size != 0u) {
        return (int)index + 1;
      }
    } else if (string_is(command.range_map, cases[index].map) == CTOOL_FALSE) {
      return (int)index + 1;
    }
    if (command.raw != CTOOL_FALSE &&
        (command.mode != cases[index].mode ||
         command.base_address != cases[index].base)) {
      return (int)index + 1;
    }
  }
  {
    static const char bounded[] = {'f', 'i', 'l', 'e'};
    ctool_string_t arguments;
    dis_command_t command;
    arguments.data = bounded;
    arguments.size = (ctool_u32)sizeof(bounded);
    if (dis_command_parse(arguments, &command) != CTOOL_OK ||
        string_is(command.input, "file") == CTOOL_FALSE ||
        command.raw != CTOOL_FALSE) {
      return 10;
    }
  }
  return 0;
}

static int check_command_failures_and_recovery(void) {
  static const char *const cases[] = {
      "", "   ", "--", "one two", "--unknown file", "--raw",
      "--raw file", "--raw --mode 16 file", "--raw --base 0 file",
      "--mode 16 --base 0 file", "--require-known file",
      "--range-map map file", "--raw --mode 64 --base 0 file",
      "--raw --mode 0 --base 0 file", "--raw --mode --base 0 file",
      "--raw --mode 16 --base", "--raw --mode 16 --base -1 file",
      "--raw --mode 16 --base +1 file",
      "--raw --mode 16 --base 4294967296 file",
      "--raw --mode 16 --base 0x100000000 file",
      "--raw --mode 16 --base 0x file",
      "--raw --mode 16 --base 12junk file",
      "--raw --raw --mode 16 --base 0 file",
      "--raw --mode 16 --mode 32 --base 0 file",
      "--raw --mode 16 --base 0 --base 1 file",
      "--raw --mode 16 --base 0 --require-known --require-known file",
      "--raw --range-map first --range-map second file",
      "--raw --range-map map --mode 16 file",
      "--raw --range-map map --base 0 file",
      "--raw --mode 16 --base 0 --range-map map file",
      "--raw --range-map", "--raw --range-map map",
      "--raw --range-map map one two",
      "--raw --mode 16 --base 0 -- one two"};
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       index++) {
    dis_command_t command;
    command_poison(&command);
    if (dis_command_parse(ctool_string(cases[index]), &command) == CTOOL_OK ||
        command_is_cleared(&command) == CTOOL_FALSE) {
      return (int)index + 1;
    }
  }
  {
    static const char embedded_nul[] = {'f', '\0', 'x'};
    ctool_string_t arguments;
    dis_command_t command;
    arguments.data = embedded_nul;
    arguments.size = (ctool_u32)sizeof(embedded_nul);
    command_poison(&command);
    if (dis_command_parse(arguments, &command) == CTOOL_OK ||
        command_is_cleared(&command) == CTOOL_FALSE) {
      return 35;
    }
    arguments.data = (const char *)0;
    arguments.size = 1u;
    command_poison(&command);
    if (dis_command_parse(arguments, &command) == CTOOL_OK ||
        command_is_cleared(&command) == CTOOL_FALSE) {
      return 36;
    }
    if (dis_command_parse(ctool_string("file"), (dis_command_t *)0) ==
        CTOOL_OK) {
      return 37;
    }
    if (dis_command_parse(ctool_string("--raw --mode 32 --base 0 file"),
                          &command) != CTOOL_OK ||
        string_is(command.input, "file") == CTOOL_FALSE ||
        command.mode != CTOOL_X86_MODE_32 || command.base_address != 0u ||
        command.require_known != CTOOL_FALSE) {
      return 38;
    }
  }
  return 0;
}

static dis_raw_file_request_t fixed_file_request(ctool_x86_mode_t mode,
                                                ctool_u32 base) {
  dis_raw_file_request_t request;
  request.mode = mode;
  request.base_address = base;
  request.range_map_path = (const char *)0;
  request.require_known = CTOOL_TRUE;
  return request;
}

static dis_raw_file_request_t mapped_file_request(const char *map) {
  dis_raw_file_request_t request;
  request.mode = CTOOL_DIS_RAW_RANGE_MAP;
  request.base_address = 0u;
  request.range_map_path = map;
  request.require_known = CTOOL_TRUE;
  return request;
}

static int check_raw_file_fixed_modes(void) {
  dis_raw_file_request_t request =
      fixed_file_request(CTOOL_X86_MODE_16, 0x7c00u);
  capture_reset();
  if (dis_raw_file("/raw16.bin", &request, print) != 0 ||
      capture_overflowed != CTOOL_FALSE ||
      text_contains(capture, "00007C00") == CTOOL_FALSE ||
      text_contains(capture, "mov ax, 0x1234") == CTOOL_FALSE ||
      text_contains(capture, "mov eax") != CTOOL_FALSE) {
    return 1;
  }
  request = fixed_file_request(CTOOL_X86_MODE_32, 0x1000u);
  capture_reset();
  if (dis_raw_file("/raw32.bin", &request, print) != 0 ||
      capture_overflowed != CTOOL_FALSE ||
      text_contains(capture, "00001000") == CTOOL_FALSE ||
      text_contains(capture, "mov eax, 0x12345678") == CTOOL_FALSE ||
      text_contains(capture, "ret") == CTOOL_FALSE) {
    return 2;
  }
  return 0;
}

static int check_raw_file_maps(void) {
  static const char *const maps[] = {
      "/mixed-v1.cupidmap", "/mixed-v2.cupidmap"};
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)(sizeof(maps) / sizeof(maps[0]));
       index++) {
    dis_raw_file_request_t request = mapped_file_request(maps[index]);
    capture_reset();
    if (dis_raw_file("/mixed.bin", &request, print) != 0 ||
        capture_overflowed != CTOOL_FALSE ||
        text_contains(capture, "00008000") == CTOOL_FALSE ||
        text_contains(capture, "mov ax, 0x1234") == CTOOL_FALSE ||
        text_contains(capture, "db 0x90") == CTOOL_FALSE ||
        text_contains(capture, "mov eax, 0x12345678") == CTOOL_FALSE ||
        text_contains(capture, "nop") != CTOOL_FALSE) {
      return (int)index + 1;
    }
  }
  return 0;
}

static int file_rejected(const char *path,
                          const dis_raw_file_request_t *request) {
  capture_reset();
  return dis_raw_file(path, request, print) != 0 &&
                 capture_overflowed == CTOOL_FALSE &&
                 text_contains(capture, "[disassembly") == CTOOL_FALSE
             ? 0
             : 1;
}

static int check_raw_file_failures_and_recovery(void) {
  dis_raw_file_request_t request =
      fixed_file_request(CTOOL_X86_MODE_32, 0x1000u);
  ctool_u32 reads;
  if (file_rejected("/missing.bin", &request) != 0) return 1;
  if (file_rejected("/read-fails.bin", &request) != 0) return 2;
  reads = memory_overlimit_read_calls;
  if (file_rejected("/oversize.bin", &request) != 0 ||
      memory_overlimit_read_calls != reads) return 3;
  if (file_rejected((const char *)0, &request) != 0) return 4;
  if (file_rejected("/raw32.bin", (const dis_raw_file_request_t *)0) != 0)
    return 5;
  request.mode = (ctool_x86_mode_t)64;
  if (file_rejected("/raw32.bin", &request) != 0) return 6;
  request = fixed_file_request(CTOOL_X86_MODE_32, 0x1000u);
  request.range_map_path = "/mixed-v1.cupidmap";
  if (file_rejected("/raw32.bin", &request) != 0) return 7;
  request = mapped_file_request((const char *)0);
  if (file_rejected("/mixed.bin", &request) != 0) return 8;
  request = mapped_file_request("");
  if (file_rejected("/mixed.bin", &request) != 0) return 9;
  request = mapped_file_request("/mixed-v1.cupidmap");
  request.base_address = 1u;
  if (file_rejected("/mixed.bin", &request) != 0) return 10;
  request = mapped_file_request("/missing.cupidmap");
  if (file_rejected("/mixed.bin", &request) != 0) return 11;
  request = mapped_file_request("/mixed-v1.cupidmap");
  if (file_rejected("/missing.bin", &request) != 0) return 12;
  request = mapped_file_request("/wrong-size.cupidmap");
  if (file_rejected("/mixed.bin", &request) != 0) return 13;
  request = mapped_file_request("/malformed.cupidmap");
  if (file_rejected("/mixed.bin", &request) != 0) return 14;
  request = mapped_file_request("/oversize.cupidmap");
  reads = memory_overlimit_read_calls;
  if (file_rejected("/mixed.bin", &request) != 0 ||
      memory_overlimit_read_calls != reads) return 15;
  request = fixed_file_request(CTOOL_X86_MODE_32, 0x9000u);
  if (file_rejected("/truncated.bin", &request) != 0 ||
      text_contains(capture, "code check failed") == CTOOL_FALSE) return 16;
  request.require_known = CTOOL_FALSE;
  capture_reset();
  if (dis_raw_file("/truncated.bin", &request, print) != 0 ||
      text_contains(capture, "db 0x0F") == CTOOL_FALSE ||
      capture_overflowed != CTOOL_FALSE) return 17;
  /* A failed map owns a different job from the next successful input/map pair. */
  request = mapped_file_request("/malformed.cupidmap");
  if (file_rejected("/mixed.bin", &request) != 0) return 18;
  if (check_raw_file_maps() != 0 || check_raw_file_fixed_modes() != 0)
    return 19;
  return memory_write_calls == 0u ? 0 : 20;
}

int main(void) {
  if (ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 1;
  }
  if (check_legacy_32_bit() != 0) {
    return 2;
  }
  if (check_fixed_16_bit() != 0) {
    return 3;
  }
  if (check_typed_map() != 0) {
    return 4;
  }
  {
    int strict_status = check_strict_failures_and_legacy_fallback();
    if (strict_status != 0) {
      return 50 + strict_status;
    }
  }
  if (check_invalid_map_and_recovery() != 0) {
    return 6;
  }
  {
    int status = check_command_successes();
    if (status != 0) return 70 + status;
    status = check_command_failures_and_recovery();
    if (status != 0) return 90 + status;
    status = check_raw_file_fixed_modes();
    if (status != 0) return 130 + status;
    status = check_raw_file_maps();
    if (status != 0) return 140 + status;
    status = check_raw_file_failures_and_recovery();
    if (status != 0) return 150 + status;
  }
  return 0;
}
