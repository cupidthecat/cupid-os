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

ctool_job_config_t ctool_kernel_job_config(ctool_limits_t limits) {
  return ctool_host_job_config(&adapter, limits);
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
  return 0;
}
