#include "as_elf.h"

#define AS_ELF32_HEADER_BYTES 52u
#define AS_ELF32_PROGRAM_HEADER_BYTES 32u
#define AS_ELF32_CODE_OFFSET 0x80u
#define AS_ELF32_ALIGNMENT 4u
#define AS_ELF32_U32_MAX 4294967295u

static void as_zero_bytes(void *value, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)value;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static void as_artifact_zero_result(as_artifact_result_t *result) {
  as_zero_bytes(result, (ctool_u32)sizeof(*result));
}

static ctool_status_t as_artifact_fail(ctool_buffer_t *output,
                                       ctool_u32 mark,
                                       as_artifact_result_t *result,
                                       ctool_status_t status) {
  ctool_status_t rewind_status = ctool_buffer_rewind(output, mark);
  as_artifact_zero_result(result);
  return rewind_status == CTOOL_OK ? status : rewind_status;
}

static ctool_status_t as_artifact_validate_raw_ranges(
    ctool_bytes_t bytes, const ctool_asm_raw_range_t *ranges,
    ctool_u32 range_count) {
  ctool_u32 index;
  if ((bytes.size == 0u &&
       (ranges != (const ctool_asm_raw_range_t *)0 || range_count != 0u)) ||
      (bytes.size != 0u &&
       (ranges == (const ctool_asm_raw_range_t *)0 || range_count == 0u))) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < range_count; index++) {
    const ctool_asm_raw_range_t *range = &ranges[index];
    if ((index == 0u ? range->offset != 0u
                     : range->offset <= ranges[index - 1u].offset) ||
        range->offset >= bytes.size ||
        (range->kind != CTOOL_ASM_RAW_RANGE_CODE16 &&
         range->kind != CTOOL_ASM_RAW_RANGE_CODE32 &&
         range->kind != CTOOL_ASM_RAW_RANGE_DATA)) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  return CTOOL_OK;
}

static ctool_asm_raw_range_kind_t as_artifact_raw_kind_at_offset(
    const ctool_asm_raw_range_t *ranges, ctool_u32 range_count,
    ctool_u32 offset) {
  ctool_u32 index;
  ctool_asm_raw_range_kind_t kind = CTOOL_ASM_RAW_RANGE_DATA;
  for (index = 0u; index < range_count; index++) {
    if (ranges[index].offset > offset) break;
    kind = ranges[index].kind;
  }
  return kind;
}

static ctool_x86_mode_t as_artifact_raw_mode(
    ctool_asm_raw_range_kind_t kind) {
  return kind == CTOOL_ASM_RAW_RANGE_CODE16 ? CTOOL_X86_MODE_16
                                             : CTOOL_X86_MODE_32;
}

static ctool_bool as_artifact_raw_address_inside(ctool_bytes_t bytes,
                                                  ctool_u32 origin,
                                                  ctool_u32 address) {
  return address >= origin && address - origin < bytes.size ? CTOOL_TRUE
                                                            : CTOOL_FALSE;
}

static ctool_status_t as_artifact_validate_raw_edges(
    ctool_bytes_t bytes, const ctool_asm_raw_range_t *ranges,
    ctool_u32 range_count, ctool_u32 origin,
    const ctool_asm_raw_edge_t *edges, ctool_u32 edge_count) {
  ctool_u32 index;
  if ((edge_count != 0u && edges == (const ctool_asm_raw_edge_t *)0) ||
      edge_count > bytes.size) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < edge_count; index++) {
    const ctool_asm_raw_edge_t *edge = &edges[index];
    ctool_asm_raw_range_kind_t source_kind;
    if (edge->source_offset >= bytes.size ||
        (index != 0u &&
         edge->source_offset <= edges[index - 1u].source_offset)) {
      return CTOOL_ERR_INTERNAL;
    }
    source_kind = as_artifact_raw_kind_at_offset(
        ranges, range_count, edge->source_offset);
    if (source_kind == CTOOL_ASM_RAW_RANGE_DATA ||
        (edge->kind != CTOOL_ASM_RAW_EDGE_RELATIVE &&
         edge->kind != CTOOL_ASM_RAW_EDGE_FAR &&
         edge->kind != CTOOL_ASM_RAW_EDGE_INDIRECT) ||
        (edge->class_id != CTOOL_ASM_RAW_EDGE_LOCAL &&
         edge->class_id != CTOOL_ASM_RAW_EDGE_EXTERNAL &&
         edge->class_id != CTOOL_ASM_RAW_EDGE_UNPROVABLE)) {
      return CTOOL_ERR_INTERNAL;
    }
    if (edge->class_id == CTOOL_ASM_RAW_EDGE_LOCAL) {
      ctool_asm_raw_range_kind_t target_kind;
      if (edge->kind == CTOOL_ASM_RAW_EDGE_INDIRECT ||
          edge->target_offset == CTOOL_ASM_RAW_EDGE_NO_TARGET ||
          (edge->target_mode != CTOOL_X86_MODE_16 &&
           edge->target_mode != CTOOL_X86_MODE_32) ||
          edge->target_offset >= bytes.size ||
          origin > AS_ELF32_U32_MAX - edge->target_offset ||
          origin + edge->target_offset != edge->target_address) {
        return CTOOL_ERR_INTERNAL;
      }
      target_kind = as_artifact_raw_kind_at_offset(
          ranges, range_count, edge->target_offset);
      if (target_kind == CTOOL_ASM_RAW_RANGE_DATA ||
          as_artifact_raw_mode(target_kind) != edge->target_mode ||
          (edge->kind == CTOOL_ASM_RAW_EDGE_RELATIVE &&
           edge->target_segment != 0u) ||
          edge->target_segment == CTOOL_ASM_RAW_EDGE_NO_TARGET) {
        return CTOOL_ERR_INTERNAL;
      }
    } else if (edge->class_id == CTOOL_ASM_RAW_EDGE_EXTERNAL) {
      if (edge->kind == CTOOL_ASM_RAW_EDGE_INDIRECT ||
          edge->target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
          (edge->target_mode != CTOOL_X86_MODE_16 &&
           edge->target_mode != CTOOL_X86_MODE_32) ||
          edge->target_segment == CTOOL_ASM_RAW_EDGE_NO_TARGET ||
          (edge->kind == CTOOL_ASM_RAW_EDGE_RELATIVE &&
           edge->target_segment != 0u) ||
          as_artifact_raw_address_inside(bytes, origin,
                                         edge->target_address) == CTOOL_TRUE) {
        return CTOOL_ERR_INTERNAL;
      }
    } else if (edge->kind != CTOOL_ASM_RAW_EDGE_INDIRECT ||
               edge->target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
               edge->target_address != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
               edge->target_mode != (ctool_x86_mode_t)0 ||
               edge->target_segment != CTOOL_ASM_RAW_EDGE_NO_TARGET) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t as_artifact_validate_raw(
    const ctool_asm_result_t *artifact, ctool_bytes_t bytes) {
  if (artifact->artifact != CTOOL_ASM_ARTIFACT_RAW ||
      artifact->bytes.data != bytes.data || artifact->bytes.size != bytes.size ||
      artifact->regions != (const ctool_asm_region_t *)0 ||
      artifact->region_count != 0u || artifact->has_entry != CTOOL_FALSE ||
      artifact->entry_symbol.data != (const char *)0 ||
      artifact->entry_symbol.size != 0u || artifact->entry_address != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (as_artifact_validate_raw_ranges(
          bytes, artifact->raw_ranges,
          artifact->raw_range_count) != CTOOL_OK) {
    return CTOOL_ERR_INTERNAL;
  }
  return as_artifact_validate_raw_edges(
      bytes, artifact->raw_ranges, artifact->raw_range_count,
      artifact->raw_origin, artifact->raw_edges,
      artifact->raw_edge_count);
}

static ctool_status_t as_artifact_validate_object(
    const ctool_asm_result_t *artifact, ctool_bytes_t bytes,
    ctool_bool require_entry) {
  if (artifact->artifact != CTOOL_ASM_ARTIFACT_ELF32_REL ||
      artifact->bytes.data != bytes.data || artifact->bytes.size != bytes.size ||
      bytes.size == 0u || artifact->regions != (const ctool_asm_region_t *)0 ||
      artifact->region_count != 0u || artifact->entry_address != 0u ||
      artifact->raw_ranges != (const ctool_asm_raw_range_t *)0 ||
      artifact->raw_range_count != 0u ||
      artifact->raw_edges != (const ctool_asm_raw_edge_t *)0 ||
      artifact->raw_edge_count != 0u || artifact->raw_origin != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if ((artifact->has_entry == CTOOL_TRUE &&
       (artifact->entry_symbol.data == (const char *)0 ||
        artifact->entry_symbol.size == 0u)) ||
      (artifact->has_entry == CTOOL_FALSE &&
       (artifact->entry_symbol.data != (const char *)0 ||
        artifact->entry_symbol.size != 0u)) ||
      (require_entry == CTOOL_TRUE && artifact->has_entry != CTOOL_TRUE)) {
    return CTOOL_ERR_INTERNAL;
  }
  return CTOOL_OK;
}

ctool_status_t as_artifact_assemble(ctool_job_t *job,
                                    const ctool_source_t *source,
                                    const as_artifact_request_t *request,
                                    ctool_buffer_t *output,
                                    as_artifact_result_t *result_out) {
  ctool_asm_request_t assembly_request;
  ctool_asm_result_t assembly_result;
  ctool_buffer_t *object_output = (ctool_buffer_t *)0;
  ctool_buffer_t *assembly_output = output;
  ctool_ld_result_t link_result;
  ctool_u32 mark;
  ctool_status_t status;

  if (result_out != (as_artifact_result_t *)0) {
    as_artifact_zero_result(result_out);
  }
  if (job == (ctool_job_t *)0 || source == (const ctool_source_t *)0 ||
      request == (const as_artifact_request_t *)0 ||
      output == (ctool_buffer_t *)0 ||
      result_out == (as_artifact_result_t *)0 ||
      ctool_buffer_view(output).size != 0u ||
      (request->format != AS_ARTIFACT_FORMAT_BIN &&
       request->format != AS_ARTIFACT_FORMAT_ELF32 &&
       request->format != AS_ARTIFACT_FORMAT_EXEC) ||
      (request->format == AS_ARTIFACT_FORMAT_EXEC &&
       (request->executable_text_address == 0u ||
        request->executable_maximum_span == 0u)) ||
      (request->format != AS_ARTIFACT_FORMAT_EXEC &&
       (request->executable_text_address != 0u ||
        request->executable_maximum_span != 0u))) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }

  mark = ctool_buffer_mark(output);
  as_zero_bytes(&assembly_request, (ctool_u32)sizeof(assembly_request));
  as_zero_bytes(&assembly_result, (ctool_u32)sizeof(assembly_result));
  as_zero_bytes(&link_result, (ctool_u32)sizeof(link_result));
  assembly_request.artifact =
      request->format == AS_ARTIFACT_FORMAT_BIN
          ? CTOOL_ASM_ARTIFACT_RAW
          : CTOOL_ASM_ARTIFACT_ELF32_REL;
  assembly_request.initial_mode = request->initial_mode;
  assembly_request.definitions = request->definitions;
  assembly_request.definition_count = request->definition_count;
  assembly_request.include_roots = request->include_roots;
  assembly_request.include_root_count = request->include_root_count;
  assembly_request.entry_candidates = request->entry_candidates;
  assembly_request.entry_candidate_count = request->entry_candidate_count;
  assembly_request.case_insensitive_symbols =
      request->case_insensitive_symbols;
  assembly_request.allow_implicit_externs = request->allow_implicit_externs;
  if (request->format == AS_ARTIFACT_FORMAT_BIN) {
    assembly_request.as.raw.initial_origin = request->initial_origin;
  }

  if (request->format == AS_ARTIFACT_FORMAT_EXEC) {
    status = ctool_job_open_buffer(job, 256u,
                                   ctool_job_limits(job)->output_bytes,
                                   &object_output);
    if (status != CTOOL_OK) {
      return as_artifact_fail(output, mark, result_out, status);
    }
    assembly_output = object_output;
  }
  status = ctool_asm_assemble(job, source, &assembly_request,
                              assembly_output, &assembly_result);
  if (status == CTOOL_OK && request->format == AS_ARTIFACT_FORMAT_BIN) {
    status = as_artifact_validate_raw(&assembly_result,
                                      ctool_buffer_view(output));
  }
  if (status == CTOOL_OK && request->format != AS_ARTIFACT_FORMAT_BIN) {
    status = as_artifact_validate_object(
        &assembly_result, ctool_buffer_view(assembly_output),
        request->format == AS_ARTIFACT_FORMAT_EXEC ? CTOOL_TRUE
                                                   : CTOOL_FALSE);
  }
  if (status == CTOOL_OK && request->format == AS_ARTIFACT_FORMAT_EXEC) {
    status = as_elf32_exec_link(job, &assembly_result,
                                request->executable_text_address,
                                request->executable_maximum_span,
                                output, &link_result);
  }
  if (status != CTOOL_OK) {
    if (object_output != (ctool_buffer_t *)0) {
      ctool_buffer_close(object_output);
    }
    return as_artifact_fail(output, mark, result_out, status);
  }

  result_out->format = request->format;
  result_out->bytes = ctool_buffer_view(output);
  result_out->entry_symbol = assembly_result.entry_symbol;
  if (request->format == AS_ARTIFACT_FORMAT_BIN) {
    result_out->raw_ranges = assembly_result.raw_ranges;
    result_out->raw_range_count = assembly_result.raw_range_count;
    result_out->raw_edges = assembly_result.raw_edges;
    result_out->raw_edge_count = assembly_result.raw_edge_count;
    result_out->raw_origin = assembly_result.raw_origin;
  } else if (request->format == AS_ARTIFACT_FORMAT_EXEC) {
    result_out->entry_address = link_result.entry;
    result_out->link = link_result;
  }
  if (object_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(object_output);
  }
  return CTOOL_OK;
}

static ctool_status_t as_artifact_append_text(ctool_buffer_t *output,
                                              const char *text) {
  ctool_u32 size = 0u;
  while (text[size] != '\0') size++;
  return ctool_buffer_append(output, ctool_bytes(text, size));
}

static ctool_status_t as_artifact_append_decimal(ctool_buffer_t *output,
                                                 ctool_u32 value) {
  char digits[10];
  ctool_u32 count = 0u;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value != 0u);
  while (count != 0u) {
    ctool_status_t status = ctool_buffer_put_u8(
        output, (ctool_u8)digits[count - 1u]);
    if (status != CTOOL_OK) return status;
    count--;
  }
  return CTOOL_OK;
}

static ctool_status_t as_artifact_append_hex32(ctool_buffer_t *output,
                                               ctool_u32 value) {
  static const char digits[] = "0123456789abcdef";
  ctool_u32 shift;
  ctool_status_t status = as_artifact_append_text(output, "0x");
  for (shift = 28u; status == CTOOL_OK; shift -= 4u) {
    status = ctool_buffer_put_u8(
        output, (ctool_u8)digits[(value >> shift) & 0x0fu]);
    if (shift == 0u) break;
  }
  return status;
}

static const char *as_artifact_raw_edge_kind_name(
    ctool_asm_raw_edge_kind_t kind) {
  if (kind == CTOOL_ASM_RAW_EDGE_RELATIVE) return "relative";
  if (kind == CTOOL_ASM_RAW_EDGE_FAR) return "far";
  return "indirect";
}

static const char *as_artifact_raw_edge_class_name(
    ctool_asm_raw_edge_class_t class_id) {
  if (class_id == CTOOL_ASM_RAW_EDGE_LOCAL) return "local";
  if (class_id == CTOOL_ASM_RAW_EDGE_EXTERNAL) return "external";
  return "unprovable";
}

ctool_status_t as_artifact_render_raw_map(
    const as_artifact_result_t *result, ctool_buffer_t *output) {
  ctool_u32 mark;
  ctool_u32 index;
  ctool_status_t status;
  if (result == (const as_artifact_result_t *)0 ||
      output == (ctool_buffer_t *)0 ||
      ctool_buffer_view(output).size != 0u ||
      result->format != AS_ARTIFACT_FORMAT_BIN ||
      (result->bytes.data == (const ctool_u8 *)0 &&
       result->bytes.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = as_artifact_validate_raw_ranges(
      result->bytes, result->raw_ranges, result->raw_range_count);
  if (status == CTOOL_OK) {
    status = as_artifact_validate_raw_edges(
        result->bytes, result->raw_ranges, result->raw_range_count,
        result->raw_origin, result->raw_edges, result->raw_edge_count);
  }
  if (status != CTOOL_OK) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  mark = ctool_buffer_mark(output);
  status = as_artifact_append_text(output, "cupid.raw-map.v2\nsize ");
  if (status == CTOOL_OK) {
    status = as_artifact_append_decimal(output, result->bytes.size);
  }
  if (status == CTOOL_OK) status = as_artifact_append_text(output, "\nbase ");
  if (status == CTOOL_OK) {
    status = as_artifact_append_hex32(output, result->raw_origin);
  }
  if (status == CTOOL_OK) status = as_artifact_append_text(output, "\nedges ");
  if (status == CTOOL_OK) {
    status = as_artifact_append_decimal(output, result->raw_edge_count);
  }
  if (status == CTOOL_OK) status = as_artifact_append_text(output, "\n");
  for (index = 0u; status == CTOOL_OK && index < result->raw_range_count;
       index++) {
    const ctool_asm_raw_range_t *range = &result->raw_ranges[index];
    const char *kind = range->kind == CTOOL_ASM_RAW_RANGE_CODE16
                           ? "code16"
                           : (range->kind == CTOOL_ASM_RAW_RANGE_CODE32
                                  ? "code32"
                                  : "data");
    status = as_artifact_append_text(output, "range ");
    if (status == CTOOL_OK) {
      status = as_artifact_append_hex32(output, range->offset);
    }
    if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
    if (status == CTOOL_OK) status = as_artifact_append_text(output, kind);
    if (status == CTOOL_OK) status = as_artifact_append_text(output, "\n");
  }
  for (index = 0u; status == CTOOL_OK && index < result->raw_edge_count;
       index++) {
    const ctool_asm_raw_edge_t *edge = &result->raw_edges[index];
    status = as_artifact_append_text(output, "edge ");
    if (status == CTOOL_OK) {
      status = as_artifact_append_hex32(output, edge->source_offset);
    }
    if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
    if (status == CTOOL_OK) {
      status = as_artifact_append_text(
          output, as_artifact_raw_edge_kind_name(edge->kind));
    }
    if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
    if (status == CTOOL_OK) {
      status = as_artifact_append_text(
          output, as_artifact_raw_edge_class_name(edge->class_id));
    }
    if (edge->class_id == CTOOL_ASM_RAW_EDGE_UNPROVABLE) {
      if (status == CTOOL_OK) {
        status = as_artifact_append_text(output, " - - unknown -\n");
      }
    } else if (edge->class_id == CTOOL_ASM_RAW_EDGE_EXTERNAL) {
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " - ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_hex32(output, edge->target_address);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_decimal(
            output, (ctool_u32)edge->target_mode);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_hex32(output, edge->target_segment);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, "\n");
    } else {
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_hex32(output, edge->target_offset);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_hex32(output, edge->target_address);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_decimal(
            output, (ctool_u32)edge->target_mode);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, " ");
      if (status == CTOOL_OK) {
        status = as_artifact_append_hex32(output, edge->target_segment);
      }
      if (status == CTOOL_OK) status = as_artifact_append_text(output, "\n");
    }
  }
  if (status != CTOOL_OK) {
    ctool_status_t rewind_status = ctool_buffer_rewind(output, mark);
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  return CTOOL_OK;
}

static ctool_bool as_token_equal(ctool_string_t token, const char *text) {
  ctool_u32 index;
  for (index = 0u; index < token.size; index++) {
    if (text[index] == '\0' || token.data[index] != text[index]) {
      return CTOOL_FALSE;
    }
  }
  return text[token.size] == '\0' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool as_string_equal(ctool_string_t left,
                                  ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) return CTOOL_FALSE;
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t as_command_tokenize(ctool_string_t arguments,
                                          ctool_string_t *tokens,
                                          ctool_u32 *count_out) {
  ctool_u32 cursor = 0u;
  ctool_u32 count = 0u;
  if ((arguments.data == (const char *)0 && arguments.size != 0u) ||
      tokens == (ctool_string_t *)0 || count_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (cursor < arguments.size) {
    ctool_u32 start;
    while (cursor < arguments.size &&
           (arguments.data[cursor] == ' ' || arguments.data[cursor] == '\t')) {
      cursor++;
    }
    if (cursor == arguments.size) break;
    if (count == 10u) return CTOOL_ERR_INPUT;
    start = cursor;
    while (cursor < arguments.size && arguments.data[cursor] != ' ' &&
           arguments.data[cursor] != '\t') {
      cursor++;
    }
    tokens[count].data = arguments.data + start;
    tokens[count].size = cursor - start;
    count++;
  }
  *count_out = count;
  return count == 0u ? CTOOL_ERR_INPUT : CTOOL_OK;
}

ctool_status_t as_command_parse(as_command_frontend_t frontend,
                                ctool_string_t arguments,
                                as_command_t *command_out) {
  ctool_string_t tokens[10];
  ctool_string_t source = {0};
  ctool_string_t output = {0};
  ctool_string_t map = {0};
  as_artifact_format_t format = AS_ARTIFACT_FORMAT_EXEC;
  ctool_bool have_format = CTOOL_FALSE;
  ctool_u32 count = 0u;
  ctool_u32 index;
  ctool_status_t status;
  if (command_out != (as_command_t *)0) {
    as_zero_bytes(command_out, (ctool_u32)sizeof(*command_out));
  }
  if ((frontend != AS_COMMAND_AS && frontend != AS_COMMAND_CUPIDASM) ||
      command_out == (as_command_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = as_command_tokenize(arguments, tokens, &count);
  if (status != CTOOL_OK) return status;
  for (index = 0u; index < count; index++) {
    ctool_string_t token = tokens[index];
    if (as_token_equal(token, "-f") == CTOOL_TRUE) {
      if (have_format == CTOOL_TRUE || index + 1u == count) {
        return CTOOL_ERR_INPUT;
      }
      token = tokens[++index];
      if (as_token_equal(token, "bin") == CTOOL_TRUE) {
        format = AS_ARTIFACT_FORMAT_BIN;
      } else if (as_token_equal(token, "elf32") == CTOOL_TRUE) {
        format = AS_ARTIFACT_FORMAT_ELF32;
      } else if (as_token_equal(token, "exec") == CTOOL_TRUE) {
        format = AS_ARTIFACT_FORMAT_EXEC;
      } else {
        return CTOOL_ERR_INPUT;
      }
      have_format = CTOOL_TRUE;
    } else if (as_token_equal(token, "-o") == CTOOL_TRUE) {
      if (output.size != 0u || index + 1u == count) return CTOOL_ERR_INPUT;
      output = tokens[++index];
    } else if (as_token_equal(token, "--map") == CTOOL_TRUE) {
      if (map.size != 0u || index + 1u == count) return CTOOL_ERR_INPUT;
      map = tokens[++index];
    } else if (token.data[0] == '-' || source.size != 0u) {
      return CTOOL_ERR_INPUT;
    } else {
      source = token;
    }
  }
  if (source.size == 0u) return CTOOL_ERR_INPUT;
  if (frontend == AS_COMMAND_AS && have_format == CTOOL_FALSE &&
      output.size == 0u && map.size == 0u) {
    command_out->kind = AS_COMMAND_JIT;
    command_out->source = source;
    return CTOOL_OK;
  }
  if (format == AS_ARTIFACT_FORMAT_BIN) {
    if (output.size == 0u || map.size == 0u) return CTOOL_ERR_INPUT;
  } else if (map.size != 0u) {
    return CTOOL_ERR_INPUT;
  }
  if (output.size == 0u &&
      (frontend != AS_COMMAND_CUPIDASM || have_format == CTOOL_TRUE)) {
    return CTOOL_ERR_INPUT;
  }
  if (output.size != 0u && map.size != 0u &&
      as_string_equal(output, map) == CTOOL_TRUE) {
    return CTOOL_ERR_INPUT;
  }
  command_out->kind = AS_COMMAND_ARTIFACT;
  command_out->format = format;
  command_out->source = source;
  command_out->output = output;
  command_out->map = map;
  return CTOOL_OK;
}

static ctool_status_t as_publication_remove_if_present(
    const as_artifact_publication_ops_t *ops, ctool_string_t path) {
  ctool_bool exists = CTOOL_FALSE;
  ctool_status_t status = ops->inspect(ops->context, path, &exists);
  if (status == CTOOL_OK && exists == CTOOL_TRUE) {
    status = ops->remove(ops->context, path);
  }
  return status;
}

static void as_publication_store_u32(ctool_u8 *destination,
                                     ctool_u32 value) {
  destination[0] = (ctool_u8)(value & 0xffu);
  destination[1] = (ctool_u8)((value >> 8u) & 0xffu);
  destination[2] = (ctool_u8)((value >> 16u) & 0xffu);
  destination[3] = (ctool_u8)((value >> 24u) & 0xffu);
}

static ctool_u32 as_publication_load_u32(const ctool_u8 *source) {
  return (ctool_u32)source[0] | ((ctool_u32)source[1] << 8u) |
         ((ctool_u32)source[2] << 16u) | ((ctool_u32)source[3] << 24u);
}

static ctool_bool as_publication_private_path_valid(ctool_string_t path,
                                                    const char *suffix) {
  ctool_u32 suffix_size = 0u;
  ctool_u32 component = 1u;
  ctool_u32 index;
  while (suffix[suffix_size] != '\0') suffix_size++;
  if (path.data == (const char *)0 || path.size <= suffix_size ||
      path.data[0] != '/') {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < path.size; index++) {
    char character = path.data[index];
    if (character == '\0' || character == '\\') return CTOOL_FALSE;
    if (character == '/') {
      ctool_u32 component_size = index - component;
      if (index != 0u &&
          (component_size == 0u ||
           (component_size == 1u && path.data[component] == '.') ||
           (component_size == 2u && path.data[component] == '.' &&
            path.data[component + 1u] == '.'))) {
        return CTOOL_FALSE;
      }
      component = index + 1u;
    }
  }
  for (index = 0u; index < suffix_size; index++) {
    if (path.data[path.size - suffix_size + index] != suffix[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool as_publication_private_pair_matches(
    ctool_string_t backup, ctool_string_t commit) {
  static const char backup_suffix[] = ".cupid-as-old";
  static const char commit_suffix[] = ".cupid-as-done";
  ctool_u32 backup_suffix_size =
      (ctool_u32)(sizeof(backup_suffix) - 1u);
  ctool_u32 commit_suffix_size =
      (ctool_u32)(sizeof(commit_suffix) - 1u);
  ctool_u32 backup_stem = backup.size - backup_suffix_size;
  ctool_u32 commit_stem = commit.size - commit_suffix_size;
  ctool_u32 index;
  if (backup_stem != commit_stem) return CTOOL_FALSE;
  for (index = 0u; index < backup_stem; index++) {
    if (backup.data[index] != commit.data[index]) return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t as_publication_render_commit(
    const as_artifact_publication_path_t *paths, ctool_u32 count,
    ctool_mut_bytes_t scratch, ctool_bytes_t *record_out) {
  static const ctool_u8 magic[8] = {'C', 'U', 'P', 'I', 'D', 'A', 'S', 1u};
  ctool_u32 required = 12u;
  ctool_u32 cursor;
  ctool_u32 index;
  if (scratch.data == (ctool_u8 *)0 || record_out == (ctool_bytes_t *)0 ||
      count == 0u || count > 2u || scratch.size < required) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  for (index = 0u; index < count; index++) {
    ctool_u32 field;
    for (field = 0u; field < 2u; field++) {
      ctool_u32 size = field == 0u ? paths[index].backup.size
                                  : paths[index].commit.size;
      if (required > 0xfffffffau ||
          size > 0xffffffffu - required - 5u) {
        return CTOOL_ERR_OVERFLOW;
      }
      required += 5u + size;
    }
  }
  if (required > scratch.size) return CTOOL_ERR_LIMIT;
  for (index = 0u; index < 8u; index++) scratch.data[index] = magic[index];
  as_publication_store_u32(scratch.data + 8u, count);
  cursor = 12u;
  for (index = 0u; index < count; index++) {
    ctool_u32 field;
    for (field = 0u; field < 2u; field++) {
      ctool_string_t value = field == 0u ? paths[index].backup
                                         : paths[index].commit;
      ctool_u32 character;
      as_publication_store_u32(scratch.data + cursor, value.size);
      cursor += 4u;
      for (character = 0u; character < value.size; character++) {
        scratch.data[cursor++] = (ctool_u8)value.data[character];
      }
      scratch.data[cursor++] = 0u;
    }
  }
  record_out->data = scratch.data;
  record_out->size = required;
  return CTOOL_OK;
}

static ctool_status_t as_publication_parse_commit(
    ctool_bytes_t record, ctool_string_t *backups, ctool_string_t *commits,
    ctool_u32 *count_out) {
  static const ctool_u8 magic[8] = {'C', 'U', 'P', 'I', 'D', 'A', 'S', 1u};
  ctool_u32 count;
  ctool_u32 cursor = 12u;
  ctool_u32 index;
  if (record.data == (const ctool_u8 *)0 || backups == (ctool_string_t *)0 ||
      commits == (ctool_string_t *)0 ||
      count_out == (ctool_u32 *)0 || record.size < cursor) {
    return CTOOL_ERR_INPUT;
  }
  for (index = 0u; index < 8u; index++) {
    if (record.data[index] != magic[index]) return CTOOL_ERR_INPUT;
  }
  count = as_publication_load_u32(record.data + 8u);
  if (count == 0u || count > 2u) return CTOOL_ERR_INPUT;
  for (index = 0u; index < count; index++) {
    ctool_u32 field;
    for (field = 0u; field < 2u; field++) {
      ctool_string_t *value = field == 0u ? &backups[index]
                                         : &commits[index];
      ctool_u32 size;
      if (cursor > record.size || record.size - cursor < 4u) {
        return CTOOL_ERR_INPUT;
      }
      size = as_publication_load_u32(record.data + cursor);
      cursor += 4u;
      if (size == 0u || cursor > record.size ||
          size > record.size - cursor ||
          record.size - cursor - size < 1u ||
          record.data[cursor + size] != 0u) {
        return CTOOL_ERR_INPUT;
      }
      value->data = (const char *)(record.data + cursor);
      value->size = size;
      if (as_publication_private_path_valid(
              *value, field == 0u ? ".cupid-as-old"
                                  : ".cupid-as-done") == CTOOL_FALSE) {
        return CTOOL_ERR_INPUT;
      }
      cursor += size + 1u;
    }
    if (as_publication_private_pair_matches(backups[index], commits[index]) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
  }
  if (cursor != record.size) return CTOOL_ERR_INPUT;
  if (count == 2u &&
      (as_string_equal(backups[0], backups[1]) == CTOOL_TRUE ||
       as_string_equal(commits[0], commits[1]) == CTOOL_TRUE)) {
    return CTOOL_ERR_INPUT;
  }
  *count_out = count;
  return CTOOL_OK;
}

static ctool_status_t as_publication_read_commit(
    const as_artifact_publication_ops_t *ops, ctool_string_t commit,
    ctool_mut_bytes_t scratch, ctool_string_t *backups,
    ctool_string_t *commits, ctool_u32 *count_out) {
  ctool_u32 record_size = 0u;
  ctool_status_t status = ops->read(ops->context, commit, scratch,
                                    &record_size);
  if (status != CTOOL_OK) return status;
  if (record_size > scratch.size) return CTOOL_ERR_IO;
  return as_publication_parse_commit(ctool_bytes(scratch.data, record_size),
                                     backups, commits, count_out);
}

static ctool_bool as_publication_commit_matches(
    const ctool_string_t *backups, const ctool_string_t *commits,
    ctool_u32 backup_count,
    const as_artifact_publication_path_t *paths, ctool_u32 path_count) {
  ctool_u32 index;
  if (backup_count != path_count) return CTOOL_FALSE;
  for (index = 0u; index < path_count; index++) {
    if (as_string_equal(backups[index], paths[index].backup) == CTOOL_FALSE ||
        as_string_equal(commits[index], paths[index].commit) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t as_publication_prepare_path(
    const as_artifact_publication_ops_t *ops,
    const as_artifact_publication_path_t *path) {
  ctool_bool backup_exists = CTOOL_FALSE;
  ctool_bool target_exists = CTOOL_FALSE;
  ctool_status_t status = as_publication_remove_if_present(
      ops, path->candidate);
  if (status == CTOOL_OK) {
    status = ops->inspect(ops->context, path->backup, &backup_exists);
  }
  if (status == CTOOL_OK && backup_exists == CTOOL_TRUE) {
    status = ops->inspect(ops->context, path->target, &target_exists);
  }
  if (status == CTOOL_OK && backup_exists == CTOOL_TRUE) {
    if (target_exists == CTOOL_TRUE) {
      status = ops->remove(ops->context, path->target);
    }
    if (status == CTOOL_OK) {
      status = ops->replace(ops->context, path->backup, path->target);
    }
  }
  return status;
}

static ctool_status_t as_publication_prepare(
    const as_artifact_publication_ops_t *ops,
    const as_artifact_publication_path_t *paths, ctool_u32 count,
    ctool_mut_bytes_t scratch) {
  ctool_status_t status = CTOOL_OK;
  ctool_u32 index;
  for (index = 0u; index < count && status == CTOOL_OK; index++) {
    ctool_bool committed = CTOOL_FALSE;
    status = ops->inspect(ops->context, paths[index].commit, &committed);
    if (status == CTOOL_OK && committed == CTOOL_TRUE) {
      ctool_string_t backups[2];
      ctool_string_t commits[2];
      ctool_u32 record_count = 0u;
      ctool_u32 cleanup;
      ctool_bool marker_listed = CTOOL_FALSE;
      status = as_publication_read_commit(
          ops, paths[index].commit, scratch, backups, commits, &record_count);
      for (cleanup = 0u; cleanup < record_count && status == CTOOL_OK;
           cleanup++) {
        if (as_string_equal(commits[cleanup], paths[index].commit) ==
            CTOOL_TRUE) {
          marker_listed = CTOOL_TRUE;
        }
      }
      if (status == CTOOL_OK && marker_listed == CTOOL_FALSE) {
        status = CTOOL_ERR_INPUT;
      }
      for (cleanup = 0u; cleanup < record_count && status == CTOOL_OK;
           cleanup++) {
        status = as_publication_remove_if_present(ops, backups[cleanup]);
      }
      for (cleanup = 0u; cleanup < record_count && status == CTOOL_OK;
           cleanup++) {
        status = as_publication_remove_if_present(ops, commits[cleanup]);
      }
    }
  }
  for (index = 0u; index < count && status == CTOOL_OK; index++) {
    status = as_publication_prepare_path(ops, &paths[index]);
  }
  return status;
}

ctool_status_t as_artifact_publish(
    const as_artifact_publication_ops_t *ops,
    const as_artifact_publication_request_t *request) {
  as_artifact_publication_path_t paths[2];
  ctool_bytes_t contents[2];
  ctool_bool existed[2] = {CTOOL_FALSE, CTOOL_FALSE};
  ctool_bool backed_up[2] = {CTOOL_FALSE, CTOOL_FALSE};
  ctool_bool published[2] = {CTOOL_FALSE, CTOOL_FALSE};
  ctool_string_t all_paths[8];
  ctool_u32 count;
  ctool_u32 path_count;
  ctool_u32 index;
  ctool_u32 other;
  ctool_status_t status;
  ctool_status_t rollback_status = CTOOL_OK;
  ctool_status_t cleanup_status = CTOOL_OK;
  ctool_bool have_map;
  ctool_bytes_t commit_record;

  if (ops == (const as_artifact_publication_ops_t *)0 ||
      request == (const as_artifact_publication_request_t *)0 ||
      ops->inspect == (ctool_status_t (*)(void *, ctool_string_t,
                                          ctool_bool *))0 ||
      ops->read == (ctool_status_t (*)(void *, ctool_string_t,
                                       ctool_mut_bytes_t, ctool_u32 *))0 ||
      ops->write_new == (ctool_status_t (*)(void *, ctool_string_t,
                                            ctool_bytes_t))0 ||
      ops->replace == (ctool_status_t (*)(void *, ctool_string_t,
                                          ctool_string_t))0 ||
      ops->remove == (ctool_status_t (*)(void *, ctool_string_t))0 ||
      request->artifact.target.data == (const char *)0 ||
      request->artifact.target.size == 0u ||
      request->artifact.candidate.data == (const char *)0 ||
      request->artifact.candidate.size == 0u ||
      request->artifact.backup.data == (const char *)0 ||
      request->artifact.backup.size == 0u ||
      request->artifact.commit.data == (const char *)0 ||
      request->artifact.commit.size == 0u ||
      as_publication_private_path_valid(request->artifact.backup,
                                        ".cupid-as-old") == CTOOL_FALSE ||
      as_publication_private_path_valid(request->artifact.commit,
                                        ".cupid-as-done") == CTOOL_FALSE ||
      as_publication_private_pair_matches(request->artifact.backup,
                                          request->artifact.commit) ==
          CTOOL_FALSE ||
      request->scratch.data == (ctool_u8 *)0 || request->scratch.size == 0u ||
      (request->artifact_bytes.data == (const ctool_u8 *)0 &&
       request->artifact_bytes.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  have_map = request->map.target.size != 0u ? CTOOL_TRUE : CTOOL_FALSE;
  if ((have_map == CTOOL_TRUE &&
       (request->map.target.data == (const char *)0 ||
        request->map.candidate.data == (const char *)0 ||
        request->map.candidate.size == 0u ||
        request->map.backup.data == (const char *)0 ||
        request->map.backup.size == 0u ||
        request->map.commit.data == (const char *)0 ||
        request->map.commit.size == 0u ||
        as_publication_private_path_valid(request->map.backup,
                                          ".cupid-as-old") == CTOOL_FALSE ||
        as_publication_private_path_valid(request->map.commit,
                                          ".cupid-as-done") == CTOOL_FALSE ||
        as_publication_private_pair_matches(request->map.backup,
                                            request->map.commit) ==
            CTOOL_FALSE ||
        (request->map_bytes.data == (const ctool_u8 *)0 &&
         request->map_bytes.size != 0u))) ||
      (have_map == CTOOL_FALSE &&
       (request->map.candidate.size != 0u || request->map.backup.size != 0u ||
        request->map.commit.size != 0u ||
        request->map_bytes.size != 0u))) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }

  paths[0] = request->artifact;
  contents[0] = request->artifact_bytes;
  count = 1u;
  if (have_map == CTOOL_TRUE) {
    paths[1] = request->map;
    contents[1] = request->map_bytes;
    count = 2u;
  }
  path_count = count * 4u;
  for (index = 0u; index < count; index++) {
    all_paths[index * 4u] = paths[index].target;
    all_paths[index * 4u + 1u] = paths[index].candidate;
    all_paths[index * 4u + 2u] = paths[index].backup;
    all_paths[index * 4u + 3u] = paths[index].commit;
  }
  for (index = 0u; index < path_count; index++) {
    for (other = index + 1u; other < path_count; other++) {
      if (as_string_equal(all_paths[index], all_paths[other]) == CTOOL_TRUE) {
        return CTOOL_ERR_INVALID_ARGUMENT;
      }
    }
  }

  status = as_publication_prepare(ops, paths, count, request->scratch);
  if (status != CTOOL_OK) return status;
  status = as_publication_render_commit(paths, count, request->scratch,
                                        &commit_record);
  if (status != CTOOL_OK) return status;
  for (index = 0u; index < count; index++) {
    status = ops->write_new(ops->context, paths[index].candidate,
                            contents[index]);
    if (status != CTOOL_OK) goto rollback;
  }
  for (index = 0u; index < count; index++) {
    status = ops->inspect(ops->context, paths[index].target, &existed[index]);
    if (status != CTOOL_OK) goto rollback;
  }
  for (index = 0u; index < count; index++) {
    if (existed[index] == CTOOL_TRUE) {
      status = ops->replace(ops->context, paths[index].target,
                            paths[index].backup);
      if (status != CTOOL_OK) goto rollback;
      backed_up[index] = CTOOL_TRUE;
    }
  }
  for (index = 0u; index < count; index++) {
    status = ops->replace(ops->context, paths[index].candidate,
                          paths[index].target);
    if (status != CTOOL_OK) goto rollback;
    published[index] = CTOOL_TRUE;
  }
  for (index = 0u; index < count; index++) {
    status = ops->write_new(ops->context, paths[index].commit, commit_record);
    if (status != CTOOL_OK) {
      ctool_bool commit_exists = CTOOL_FALSE;
      ctool_status_t write_status = status;
      ctool_status_t inspect_status = ops->inspect(
          ops->context, paths[index].commit, &commit_exists);
      if (inspect_status != CTOOL_OK) return inspect_status;
      if (commit_exists == CTOOL_FALSE) goto rollback;
      {
        ctool_string_t committed_backups[2];
        ctool_string_t committed_markers[2];
        ctool_u32 committed_count = 0u;
        ctool_status_t read_status = as_publication_read_commit(
            ops, paths[index].commit, request->scratch, committed_backups,
            committed_markers, &committed_count);
        if (read_status == CTOOL_ERR_INPUT ||
            read_status == CTOOL_ERR_LIMIT ||
            (read_status == CTOOL_OK &&
             as_publication_commit_matches(
                 committed_backups, committed_markers, committed_count,
                 paths, count) == CTOOL_FALSE)) {
          ctool_status_t remove_status =
              ops->remove(ops->context, paths[index].commit);
          if (remove_status != CTOOL_OK) return remove_status;
          status = write_status;
          goto rollback;
        }
        if (read_status != CTOOL_OK) return read_status;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    if (backed_up[index] == CTOOL_TRUE) {
      ctool_status_t cleanup =
          as_publication_remove_if_present(ops, paths[index].backup);
      if (cleanup_status == CTOOL_OK && cleanup != CTOOL_OK) {
        cleanup_status = cleanup;
      }
    }
  }
  if (cleanup_status == CTOOL_OK) {
    for (index = 0u; index < count; index++) {
      (void)as_publication_remove_if_present(ops, paths[index].commit);
    }
  }
  return CTOOL_OK;

rollback:
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup;
    if (published[index] == CTOOL_TRUE) {
      cleanup = as_publication_remove_if_present(ops, paths[index].target);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup;
    if (backed_up[index] == CTOOL_TRUE) {
      cleanup = ops->replace(ops->context, paths[index].backup,
                             paths[index].target);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
      if (cleanup == CTOOL_OK) backed_up[index] = CTOOL_FALSE;
    }
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup =
        as_publication_remove_if_present(ops, paths[index].candidate);
    if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
      rollback_status = cleanup;
    }
    if (backed_up[index] == CTOOL_FALSE) {
      cleanup = as_publication_remove_if_present(ops, paths[index].backup);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    (void)as_publication_remove_if_present(ops, paths[index].commit);
  }
  return rollback_status == CTOOL_OK ? status : rollback_status;
}

static void as_elf32_zero_link_result(ctool_ld_result_t *result) {
  result->bytes = 0u;
  result->entry = 0u;
  result->load_address = 0u;
  result->loaded_end = 0u;
  result->memory_end = 0u;
  result->output_section_count = 0u;
  result->resolved_symbol_count = 0u;
  result->applied_relocation_count = 0u;
  result->imported_symbol_count = 0u;
  result->imported_library_count = 0u;
}

ctool_status_t as_elf32_exec_link(ctool_job_t *job,
                                  const ctool_asm_result_t *artifact,
                                  ctool_u32 text_address,
                                  ctool_u32 maximum_image_span,
                                  ctool_buffer_t *output,
                                  ctool_ld_result_t *result_out) {
  ctool_source_t object;
  ctool_ld_request_t request;
  if (result_out != (ctool_ld_result_t *)0) {
    as_elf32_zero_link_result(result_out);
  }
  if (job == (ctool_job_t *)0 ||
      artifact == (const ctool_asm_result_t *)0 ||
      output == (ctool_buffer_t *)0 ||
      result_out == (ctool_ld_result_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (ctool_buffer_view(output).size != 0u ||
      artifact->artifact != CTOOL_ASM_ARTIFACT_ELF32_REL ||
      artifact->bytes.data == (const ctool_u8 *)0 ||
      artifact->bytes.size == 0u ||
      artifact->regions != (const ctool_asm_region_t *)0 ||
      artifact->region_count != 0u ||
      artifact->has_entry != CTOOL_TRUE ||
      artifact->entry_symbol.data == (const char *)0 ||
      artifact->entry_symbol.size == 0u || artifact->entry_address != 0u ||
      artifact->raw_ranges != (const ctool_asm_raw_range_t *)0 ||
      artifact->raw_range_count != 0u ||
      artifact->raw_edges != (const ctool_asm_raw_edge_t *)0 ||
      artifact->raw_edge_count != 0u || artifact->raw_origin != 0u ||
      maximum_image_span == 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  object.path.text = ctool_string("/cupidasm-aot.o");
  object.contents = artifact->bytes;
  request.objects = &object;
  request.object_count = 1u;
  request.image_kind = CTOOL_LD_IMAGE_ELF32;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = text_address;
  request.layout.as.fixed_text.entry_symbol = artifact->entry_symbol;
  request.pe32_imports = (const ctool_ld_pe32_import_t *)0;
  request.pe32_import_count = 0u;
  request.maximum_image_span = maximum_image_span;
  return ctool_ld_link(job, &request, output, result_out);
}

static ctool_status_t as_elf32_rollback(ctool_buffer_t *output,
                                        ctool_u32 mark,
                                        ctool_status_t status) {
  ctool_status_t rewind_status = ctool_buffer_rewind(output, mark);
  return rewind_status == CTOOL_OK ? status : rewind_status;
}

static ctool_status_t as_elf32_patch_header(ctool_buffer_t *output,
                                            ctool_u32 entry,
                                            ctool_u16 program_count) {
  ctool_status_t status = ctool_buffer_patch_u8(output, 0u, 0x7fu);
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 1u, (ctool_u8)'E');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 2u, (ctool_u8)'L');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 3u, (ctool_u8)'F');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 4u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 5u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 6u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 16u, 2u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 18u, 3u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 20u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 24u, entry);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 28u, AS_ELF32_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 40u, AS_ELF32_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 42u,
                                     AS_ELF32_PROGRAM_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 44u, program_count);
  }
  return status;
}

static ctool_status_t as_elf32_patch_program_header(
    ctool_buffer_t *output, ctool_u32 table_offset, ctool_u32 file_offset,
    const ctool_asm_region_t *region, ctool_u32 flags) {
  ctool_status_t status = ctool_buffer_patch_le32(output, table_offset, 1u);
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 4u, file_offset);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 8u,
                                     region->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 12u,
                                     region->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 16u,
                                     region->file_size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 20u,
                                     region->memory_size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 24u, flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 28u,
                                     AS_ELF32_ALIGNMENT);
  }
  return status;
}

static ctool_status_t as_elf32_align_up(ctool_u32 value,
                                        ctool_u32 alignment,
                                        ctool_u32 *aligned_out) {
  ctool_u32 remainder = value & (alignment - 1u);
  ctool_u32 addition = remainder == 0u ? 0u : alignment - remainder;
  if (value > AS_ELF32_U32_MAX - addition) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = value + addition;
  return CTOOL_OK;
}

ctool_status_t as_elf32_exec_write(const ctool_asm_result_t *artifact,
                                   ctool_buffer_t *output) {
  const ctool_asm_region_t *code;
  const ctool_asm_region_t *data = (const ctool_asm_region_t *)0;
  ctool_u32 code_end;
  ctool_u32 code_file_end;
  ctool_u32 data_end = 0u;
  ctool_u32 data_file_offset = 0u;
  ctool_u32 data_program_flags = CTOOL_ELF32_PF_R;
  ctool_u32 payload_size;
  ctool_u32 mark;
  ctool_status_t status;
  if (artifact == (const ctool_asm_result_t *)0 ||
      output == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (ctool_buffer_view(output).size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  mark = ctool_buffer_mark(output);
  if (artifact->artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      artifact->regions == (const ctool_asm_region_t *)0 ||
      (artifact->region_count != 1u && artifact->region_count != 2u) ||
      artifact->has_entry != CTOOL_TRUE ||
      (artifact->bytes.data == (const ctool_u8 *)0 &&
       artifact->bytes.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  code = &artifact->regions[0];
  if ((code->flags & (CTOOL_ELF32_SHF_ALLOC |
                      CTOOL_ELF32_SHF_EXECINSTR)) !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      (code->flags & CTOOL_ELF32_SHF_WRITE) != 0u ||
      code->output_offset != 0u || code->file_size == 0u ||
      code->memory_size < code->file_size ||
      (code->address & (AS_ELF32_ALIGNMENT - 1u)) != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (code->address > AS_ELF32_U32_MAX - code->memory_size ||
      AS_ELF32_CODE_OFFSET > AS_ELF32_U32_MAX - code->file_size) {
    return CTOOL_ERR_OVERFLOW;
  }
  code_end = code->address + code->memory_size;
  code_file_end = AS_ELF32_CODE_OFFSET + code->file_size;
  payload_size = code->file_size;
  if (artifact->region_count == 2u) {
    data = &artifact->regions[1];
    if ((data->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
        (data->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ||
        data->output_offset != code->file_size || data->memory_size == 0u ||
        data->memory_size < data->file_size ||
        (data->address & (AS_ELF32_ALIGNMENT - 1u)) != 0u) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (payload_size > AS_ELF32_U32_MAX - data->file_size) {
      return CTOOL_ERR_OVERFLOW;
    }
    payload_size += data->file_size;
    if (data->address > AS_ELF32_U32_MAX - data->memory_size) {
      return CTOOL_ERR_OVERFLOW;
    }
    data_end = data->address + data->memory_size;
    if (code->address < data_end && data->address < code_end) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if ((data->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      data_program_flags |= CTOOL_ELF32_PF_W;
    }
    status = as_elf32_align_up(code_file_end, AS_ELF32_ALIGNMENT,
                               &data_file_offset);
    if (status != CTOOL_OK ||
        data_file_offset > AS_ELF32_U32_MAX - data->file_size) {
      return CTOOL_ERR_OVERFLOW;
    }
  }
  if (payload_size != artifact->bytes.size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (artifact->entry_address < code->address ||
      artifact->entry_address - code->address >= code->file_size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }

  status = ctool_buffer_fill(output, 0u,
                             AS_ELF32_HEADER_BYTES +
                                 AS_ELF32_PROGRAM_HEADER_BYTES *
                                     artifact->region_count);
  if (status == CTOOL_OK) {
    status = as_elf32_patch_header(
        output, artifact->entry_address,
        (ctool_u16)artifact->region_count);
  }
  if (status == CTOOL_OK) {
    status = as_elf32_patch_program_header(
        output, AS_ELF32_HEADER_BYTES, AS_ELF32_CODE_OFFSET, code,
        CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0) {
    status = as_elf32_patch_program_header(
        output, AS_ELF32_HEADER_BYTES + AS_ELF32_PROGRAM_HEADER_BYTES,
        data_file_offset, data, data_program_flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_fill(
        output, 0u, AS_ELF32_CODE_OFFSET - ctool_buffer_view(output).size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(
        output, ctool_bytes(artifact->bytes.data + code->output_offset,
                            code->file_size));
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0) {
    status = ctool_buffer_fill(
        output, 0u, data_file_offset - ctool_buffer_view(output).size);
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0 &&
      data->file_size != 0u) {
    status = ctool_buffer_append(
        output, ctool_bytes(artifact->bytes.data + data->output_offset,
                            data->file_size));
  }
  return status == CTOOL_OK ? CTOOL_OK
                            : as_elf32_rollback(output, mark, status);
}
