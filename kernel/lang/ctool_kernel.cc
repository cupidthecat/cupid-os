#include "ctool_kernel.h"
#include "elf32.h"
#include "x86.h"

#include "dis.h"
#include "kernel.h"
#include "memory.h"
#include "panic.h"
#include "serial.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"

#define CTOOL_KERNEL_INT_MAX 2147483647u

static void *ctool_kernel_allocate(void *context, ctool_u32 bytes) {
  (void)context;
  return kmalloc(bytes);
}

static void ctool_kernel_release(void *context, void *allocation,
                                 ctool_u32 bytes) {
  (void)context;
  (void)bytes;
  kfree(allocation);
}

static ctool_status_t ctool_kernel_vfs_status(int status) {
  if (status == VFS_ENOENT) {
    return CTOOL_ERR_NOT_FOUND;
  }
  if (status == VFS_ENOSPC) {
    return CTOOL_ERR_LIMIT;
  }
  if (status == VFS_EINVAL) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  return CTOOL_ERR_IO;
}

static ctool_status_t ctool_kernel_file_size(void *context,
                                             ctool_string_t logical_path,
                                             ctool_u32 *size_out) {
  vfs_stat_t stat;
  int status;
  (void)context;
  if (size_out == (ctool_u32 *)0 || logical_path.data == (const char *)0 ||
      logical_path.data[logical_path.size] != '\0') {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *size_out = 0u;
  status = vfs_stat(logical_path.data, &stat);
  if (status != VFS_OK) {
    return ctool_kernel_vfs_status(status);
  }
  if (stat.type != VFS_TYPE_FILE) {
    return CTOOL_ERR_INPUT;
  }
  *size_out = stat.size;
  return CTOOL_OK;
}

static ctool_status_t ctool_kernel_read_exact(void *context,
                                              ctool_string_t logical_path,
                                              ctool_u8 *destination,
                                              ctool_u32 size) {
  int result;
  (void)context;
  if ((destination == (ctool_u8 *)0 && size != 0u) ||
      logical_path.data == (const char *)0 ||
      logical_path.data[logical_path.size] != '\0') {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (size == 0u) {
    return CTOOL_OK;
  }
  if (size > CTOOL_KERNEL_INT_MAX) {
    return CTOOL_ERR_LIMIT;
  }
  result = vfs_read_all(logical_path.data, destination, size);
  if (result < 0) {
    return ctool_kernel_vfs_status(result);
  }
  return (ctool_u32)result == size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t ctool_kernel_write_all(void *context,
                                             ctool_string_t logical_path,
                                             ctool_bytes_t contents) {
  int result;
  (void)context;
  if ((contents.data == (const ctool_u8 *)0 && contents.size != 0u) ||
      logical_path.data == (const char *)0 ||
      logical_path.data[logical_path.size] != '\0') {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (contents.size > CTOOL_KERNEL_INT_MAX) {
    return CTOOL_ERR_LIMIT;
  }
  result = vfs_write_all(logical_path.data, contents.data, contents.size);
  if (result < 0) {
    return ctool_kernel_vfs_status(result);
  }
  return (ctool_u32)result == contents.size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t ctool_kernel_write_text(void *context,
                                              ctool_bytes_t text) {
  char chunk[129];
  ctool_u32 offset = 0u;
  (void)context;
  if (text.data == (const ctool_u8 *)0 && text.size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (offset < text.size) {
    ctool_u32 count = text.size - offset;
    ctool_u32 index;
    if (count > 128u) {
      count = 128u;
    }
    for (index = 0u; index < count; index++) {
      chunk[index] = (char)text.data[offset + index];
    }
    chunk[count] = '\0';
    print(chunk);
    offset += count;
  }
  return CTOOL_OK;
}

ctool_job_config_t ctool_kernel_job_config(ctool_limits_t limits) {
  ctool_job_config_t config;
  config.allocator.context = (void *)0;
  config.allocator.allocate = ctool_kernel_allocate;
  config.allocator.release = ctool_kernel_release;
  config.files.context = (void *)0;
  config.files.file_size = ctool_kernel_file_size;
  config.files.read_exact = ctool_kernel_read_exact;
  config.files.write_all = ctool_kernel_write_all;
  config.diagnostics.context = (void *)0;
  config.diagnostics.write = ctool_kernel_write_text;
  config.limits = limits;
  return config;
}

static ctool_status_t ctool_kernel_selftest_body(
    ctool_invocation_t *invocation, void *user_data) {
  static const char marker[] = "ctool-kernel-ok";
  ctool_path_t root;
  ctool_path_t resolved;
  void *aligned;
  ctool_u8 *zeroed;
  ctool_u32 index;
  ctool_status_t status;
  (void)user_data;
  if (invocation->input->contents.size == 0u ||
      invocation->input->contents.data[invocation->input->contents.size] !=
          0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(invocation->job), 19u, 1u,
                                  16u, &aligned);
  if (status != CTOOL_OK || ((uint32_t)aligned & 15u) != 0u) {
    return status == CTOOL_OK ? CTOOL_ERR_INTERNAL : status;
  }
  zeroed = (ctool_u8 *)aligned;
  for (index = 0u; index < 19u; index++) {
    if (zeroed[index] != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  status = ctool_path_root(ctool_job_arena(invocation->job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(invocation->job), &root,
                                ctool_string("/kernel/../bin/ls.cc"), 64u,
                                &resolved);
  }
  if (status != CTOOL_OK ||
      ctool_path_equal(&resolved, &invocation->input->path) == CTOOL_FALSE) {
    return status == CTOOL_OK ? CTOOL_ERR_INTERNAL : status;
  }
  return ctool_buffer_append(
      invocation->output,
      ctool_bytes(marker, (ctool_u32)(sizeof(marker) - 1u)));
}

static void ctool_kernel_elf32_selftest(void) {
  static const ctool_u8 text[] = {0xe8u, 0u, 0u, 0u, 0u, 0xc3u};
  ctool_job_config_t config = ctool_kernel_job_config(ctool_default_limits());
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t spec;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_status_t status = ctool_job_open(&config, &job);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    kernel_panic("Cupid ELF32 self-test setup failed (%u)",
                 (uint32_t)status);
  }
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 16u;
  section.entry_size = 0u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  symbols[0].name = ctool_string("entry");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 0u;
  symbols[0].size = (ctool_u32)sizeof(text);
  symbols[0].alignment = 0u;
  symbols[1].name = ctool_string("external");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[1].section = CTOOL_ELF32_NO_SECTION;
  symbols[1].value = 0u;
  symbols[1].size = 0u;
  symbols[1].alignment = 0u;
  relocation.target_section = 0u;
  relocation.offset = 1u;
  relocation.symbol = 1u;
  relocation.type = CTOOL_ELF32_R_386_PC32;
  relocation.addend = -4;
  spec.sections = &section;
  spec.section_count = 1u;
  spec.symbols = symbols;
  spec.symbol_count = 2u;
  spec.relocations = &relocation;
  spec.relocation_count = 1u;
  status = ctool_elf32_write(job, &spec, output);
  source.path.text = ctool_string("/ctool-selftest.o");
  source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &source, &object);
  }
  if (status != CTOOL_OK || object.section_count != 6u ||
      object.symbol_count != 3u || object.relocation_count != 1u ||
      object.relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object.relocations[0].addend_known == CTOOL_FALSE ||
      object.relocations[0].addend != -4) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    kernel_panic("Cupid ELF32 self-test failed (%u)", (uint32_t)status);
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  KINFO("Cupid ELF32 object self-test passed");
}

static void ctool_kernel_x86_selftest(void) {
  static const ctool_u8 pxor_bytes[] = {0x66u, 0x0fu, 0xefu, 0xcau};
  static const ctool_u8 address_bytes[] = {0x8bu, 0x40u, 0x7fu};
  ctool_job_config_t config = ctool_kernel_job_config(ctool_default_limits());
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_x86_instruction_t instruction;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_status_t status = ctool_job_open(&config, &job);
  ctool_u32 index;
  if (status == CTOOL_OK) {
    status = ctool_x86_validate_model(job);
  }
  if (status != CTOOL_OK) {
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    kernel_panic("Cupid x86 model self-test setup failed (%u)",
                 (uint32_t)status);
  }

  memset(&instruction, 0, (uint32_t)sizeof(instruction));
  instruction.mnemonic = CTOOL_X86_MN_MOV;
  instruction.operand_bits = 16u;
  instruction.address_bits = 16u;
  instruction.operand_count = 2u;
  instruction.operands[0].kind = CTOOL_X86_OPERAND_REGISTER;
  instruction.operands[0].as.reg.class_id = CTOOL_X86_REG_GPR16;
  instruction.operands[0].as.reg.index = 0u;
  instruction.operands[1].kind = CTOOL_X86_OPERAND_MEMORY;
  instruction.operands[1].width_bits = 16u;
  instruction.operands[1].as.memory.address_bits = 16u;
  instruction.operands[1].as.memory.segment.class_id = CTOOL_X86_REG_NONE;
  instruction.operands[1].as.memory.base.class_id = CTOOL_X86_REG_GPR16;
  instruction.operands[1].as.memory.base.index = 3u;
  instruction.operands[1].as.memory.index.class_id = CTOOL_X86_REG_GPR16;
  instruction.operands[1].as.memory.index.index = 6u;
  instruction.operands[1].as.memory.scale = 1u;
  instruction.operands[1].as.memory.displacement.kind =
      CTOOL_X86_VALUE_CONSTANT;
  instruction.operands[1].as.memory.displacement.bits = 0x7fu;
  instruction.operands[1].as.memory.displacement_bits = 8u;
  status = ctool_x86_encode(job, CTOOL_X86_MODE_16, &instruction,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (status != CTOOL_OK || encoding.size != (ctool_u8)sizeof(address_bytes)) {
    ctool_job_close(job);
    kernel_panic("Cupid x86 16-bit encode self-test failed (%u)",
                 (uint32_t)status);
  }
  for (index = 0u; index < (ctool_u32)sizeof(address_bytes); index++) {
    if (encoding.bytes[index] != address_bytes[index]) {
      ctool_job_close(job);
      kernel_panic("Cupid x86 16-bit encode bytes mismatch");
    }
  }

  memset(&instruction, 0, (uint32_t)sizeof(instruction));
  instruction.mnemonic = CTOOL_X86_MN_CALL;
  instruction.operand_bits = 32u;
  instruction.address_bits = 32u;
  instruction.operand_count = 1u;
  instruction.operands[0].kind = CTOOL_X86_OPERAND_RELATIVE;
  instruction.operands[0].width_bits = 32u;
  instruction.operands[0].encoding_bits = 32u;
  instruction.operands[0].as.value.kind = CTOOL_X86_VALUE_REFERENCE;
  instruction.operands[0].as.value.reference = 9u;
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &instruction,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (status != CTOOL_OK || encoding.size != 5u ||
      encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_RELATIVE ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_PC_RELATIVE ||
      encoding.fields[0].encoded_addend != -4 ||
      encoding.fields[0].reference != 9u) {
    ctool_job_close(job);
    kernel_panic("Cupid x86 relocation self-test failed (%u)",
                 (uint32_t)status);
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(pxor_bytes, (ctool_u32)sizeof(pxor_bytes)), 0u, &decoded);
  if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
      decoded.instruction.mnemonic != CTOOL_X86_MN_PXOR ||
      decoded.instruction.prefixes != 0u || decoded.consumed != 4u ||
      decoded.instruction.operands[0].as.reg.class_id != CTOOL_X86_REG_XMM ||
      decoded.instruction.operands[0].as.reg.index != 1u ||
      decoded.instruction.operands[1].as.reg.index != 2u) {
    ctool_job_close(job);
    kernel_panic("Cupid x86 SSE2 decode self-test failed (%u)",
                 (uint32_t)status);
  }
  ctool_job_close(job);
  KINFO("Cupid x86 instruction model self-test passed");
}

typedef struct {
  char bytes[256];
  uint32_t size;
  bool overflowed;
} ctool_kernel_dis_capture_t;

static ctool_kernel_dis_capture_t *ctool_kernel_dis_capture_active;

static void ctool_kernel_dis_capture_write(const char *text) {
  ctool_kernel_dis_capture_t *capture = ctool_kernel_dis_capture_active;
  uint32_t index = 0u;
  if (capture == (ctool_kernel_dis_capture_t *)0 || text == (const char *)0) {
    return;
  }
  while (text[index] != '\0') {
    if (capture->size + 1u >= (uint32_t)sizeof(capture->bytes)) {
      capture->overflowed = true;
      return;
    }
    capture->bytes[capture->size] = text[index];
    capture->size++;
    index++;
  }
  capture->bytes[capture->size] = '\0';
}

static void ctool_kernel_dis_put_le16(uint8_t *bytes, uint32_t offset,
                                      uint16_t value) {
  bytes[offset] = (uint8_t)(value & 0xffu);
  bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
}

static void ctool_kernel_dis_put_le32(uint8_t *bytes, uint32_t offset,
                                      uint32_t value) {
  bytes[offset] = (uint8_t)(value & 0xffu);
  bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (uint8_t)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (uint8_t)((value >> 24u) & 0xffu);
}

static void ctool_kernel_cupiddis_selftest(void) {
  static const char object_name[] = "/tmp/ctool-cupiddis-selftest.elf";
  static const ctool_u8 code[] = {0xb8u, 0x78u, 0x56u, 0x34u,
                                  0x12u, 0xc3u};
  static const dis_sym_t symbols[] = {{0x00400000u, "entry"}};
  uint8_t image[90];
  ctool_kernel_dis_capture_t capture;
  int status;

  memset(&capture, 0, (uint32_t)sizeof(capture));
  ctool_kernel_dis_capture_active = &capture;
  dis_disassemble(code, (uint32_t)sizeof(code), 0x00400000u, symbols, 1,
                  ctool_kernel_dis_capture_write);
  ctool_kernel_dis_capture_active = (ctool_kernel_dis_capture_t *)0;
  if (capture.overflowed ||
      strstr(capture.bytes, "[disassembly raw]\n") == (char *)0 ||
      strstr(capture.bytes, "00400000 <entry>:") == (char *)0 ||
      strstr(capture.bytes, "00400000") == (char *)0 ||
      strstr(capture.bytes, "mov eax, 0x12345678") == (char *)0 ||
      strstr(capture.bytes, "ret") == (char *)0) {
    kernel_panic("CupidDis kernel adapter self-test failed");
  }

  memset(image, 0, (uint32_t)sizeof(image));
  image[0] = 0x7fu;
  image[1] = (uint8_t)'E';
  image[2] = (uint8_t)'L';
  image[3] = (uint8_t)'F';
  image[4] = 1u;
  image[5] = 1u;
  image[6] = 1u;
  ctool_kernel_dis_put_le16(image, 16u, 2u);
  ctool_kernel_dis_put_le16(image, 18u, 3u);
  ctool_kernel_dis_put_le32(image, 20u, 1u);
  ctool_kernel_dis_put_le32(image, 24u, 0x00400000u);
  ctool_kernel_dis_put_le32(image, 28u, 52u);
  ctool_kernel_dis_put_le16(image, 40u, 52u);
  ctool_kernel_dis_put_le16(image, 42u, 32u);
  ctool_kernel_dis_put_le16(image, 44u, 1u);
  ctool_kernel_dis_put_le32(image, 52u, CTOOL_ELF32_PT_LOAD);
  ctool_kernel_dis_put_le32(image, 56u, 84u);
  ctool_kernel_dis_put_le32(image, 60u, 0x00400000u);
  ctool_kernel_dis_put_le32(image, 64u, 0x00400000u);
  ctool_kernel_dis_put_le32(image, 68u, (uint32_t)sizeof(code));
  ctool_kernel_dis_put_le32(image, 72u, (uint32_t)sizeof(code));
  ctool_kernel_dis_put_le32(image, 76u,
                            CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  ctool_kernel_dis_put_le32(image, 80u, 4u);
  memcpy(image + 84u, code, (uint32_t)sizeof(code));
  status = vfs_write_all(object_name, image, (uint32_t)sizeof(image));
  if (status != (int)sizeof(image)) {
    kernel_panic("CupidDis kernel command fixture write failed");
  }
  memset(&capture, 0, (uint32_t)sizeof(capture));
  ctool_kernel_dis_capture_active = &capture;
  status = dis_elf(object_name, ctool_kernel_dis_capture_write);
  ctool_kernel_dis_capture_active = (ctool_kernel_dis_capture_t *)0;
  if (vfs_unlink(object_name) != VFS_OK) {
    kernel_panic("CupidDis kernel command fixture cleanup failed");
  }
  if (status != VFS_OK || capture.overflowed ||
      strstr(capture.bytes, "[disassembly LOAD#0]\n") == (char *)0 ||
      strstr(capture.bytes, "00400000") == (char *)0 ||
      strstr(capture.bytes, "mov eax, 0x12345678") == (char *)0 ||
      strstr(capture.bytes, "ret") == (char *)0) {
    kernel_panic("CupidDis kernel command self-test failed");
  }
  KINFO("CupidDis kernel adapter self-test passed");
}

void ctool_kernel_selftest(void) {
  static const char output_name[] = "/tmp/ctool-core-selftest.bin";
  static const char marker[] = "ctool-kernel-ok";
  ctool_job_config_t config = ctool_kernel_job_config(ctool_default_limits());
  ctool_invocation_request_t request;
  ctool_invocation_result_t result;
  ctool_status_t status;
  char contents[sizeof(marker) - 1u];
  vfs_stat_t stat;
  ctool_u32 index;
  int read_result;
  request.input_path = ctool_string("/bin/ls.cc");
  request.output_path = ctool_string(output_name);
  status = ctool_invoke(&config, &request, ctool_kernel_selftest_body,
                        (void *)0, &result);
  if (status != CTOOL_OK || result.output_committed == CTOOL_FALSE ||
      result.output_bytes != (ctool_u32)(sizeof(marker) - 1u)) {
    kernel_panic("Cupid toolchain core self-test invocation failed (%u)",
                 (uint32_t)status);
  }
  read_result = vfs_read_all(output_name, contents,
                             (uint32_t)(sizeof(marker) - 1u));
  if (read_result != (int)(sizeof(marker) - 1u)) {
    (void)vfs_unlink(output_name);
    kernel_panic("Cupid toolchain core self-test read failed (%d)",
                 read_result);
  }
  for (index = 0u; index < (ctool_u32)(sizeof(marker) - 1u); index++) {
    if (contents[index] != marker[index]) {
      (void)vfs_unlink(output_name);
      kernel_panic("Cupid toolchain core self-test output mismatch");
    }
  }
  if (vfs_unlink(output_name) != VFS_OK) {
    kernel_panic("Cupid toolchain core self-test cleanup failed");
  }
  request.input_path = ctool_string("/bin/ctool-definitely-missing.cc");
  status = ctool_invoke(&config, &request, ctool_kernel_selftest_body,
                        (void *)0, &result);
  if (status != CTOOL_ERR_NOT_FOUND ||
      result.output_committed != CTOOL_FALSE ||
      vfs_stat(output_name, &stat) != VFS_ENOENT) {
    kernel_panic("Cupid toolchain core missing-input self-test failed (%u)",
                 (uint32_t)status);
  }
  KINFO("Cupid toolchain core self-test passed");
  ctool_kernel_elf32_selftest();
  ctool_kernel_x86_selftest();
  ctool_kernel_cupiddis_selftest();
}
