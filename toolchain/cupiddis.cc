#include "cupiddis.h"
#include "pe32_impl.h"

#define DIS_U32_MAX 4294967295u
#define DIS_ELF32_SHT_NULL 0u
#define DIS_ELF32_SHT_SYMTAB 2u
#define DIS_ELF32_SHT_STRTAB 3u
#define DIS_ELF32_SHT_REL 9u
#define DIS_ELF32_PT_GNU_STACK 0x6474e551u

static ctool_status_t dis_prepare_report_orders(ctool_job_t *job,
                                                 ctool_dis_report_t *report);
static ctool_status_t dis_prepare_raw_label_order(ctool_job_t *job,
                                                   ctool_dis_report_t *report);

typedef struct {
  const ctool_dis_report_t *report;
  ctool_u32 section_file_index;
  ctool_u8 *relocation_claimed;
} dis_relocation_ownership_t;

static ctool_bool
dis_field_accepts_relocation(const ctool_x86_field_t *field,
                             const ctool_elf32_relocation_t *relocation) {
  if (field->byte_width != 4u) {
    return CTOOL_FALSE;
  }
  if (relocation->type == CTOOL_ELF32_R_386_PC32) {
    return field->kind == CTOOL_X86_FIELD_RELATIVE ? CTOOL_TRUE
                                                    : CTOOL_FALSE;
  }
  if (relocation->type == CTOOL_ELF32_R_386_32) {
    return field->kind != CTOOL_X86_FIELD_RELATIVE ? CTOOL_TRUE
                                                    : CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_u32 dis_relocation_site_lower_bound(
    const ctool_dis_report_t *report, ctool_u32 section_file_index,
    ctool_u32 site) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 first = 0u;
  ctool_u32 last = report->relocation_site_order_count;
  while (first < last) {
    ctool_u32 middle = first + (last - first) / 2u;
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[report->relocation_site_order[middle]];
    if ((object->file_type == CTOOL_ELF32_ET_EXEC &&
         relocation->offset < site) ||
        (object->file_type == CTOOL_ELF32_ET_REL &&
         (relocation->target_section_file_index < section_file_index ||
          (relocation->target_section_file_index == section_file_index &&
           relocation->offset < site)))) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  return first;
}

static ctool_u32 dis_find_field_relocation(
    const ctool_dis_report_t *report, ctool_u32 section_file_index,
    ctool_u32 logical_address, ctool_u32 instruction_offset,
    const ctool_x86_field_t *field,
    const ctool_u8 *relocation_claimed) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 first;
  ctool_u32 site;
  if (object->file_type == CTOOL_ELF32_ET_EXEC) {
    if (logical_address > DIS_U32_MAX - (ctool_u32)field->byte_offset) {
      return DIS_U32_MAX;
    }
    site = logical_address + (ctool_u32)field->byte_offset;
  } else {
    if (object->file_type != CTOOL_ELF32_ET_REL ||
        section_file_index >= object->section_count ||
        instruction_offset > DIS_U32_MAX - (ctool_u32)field->byte_offset) {
      return DIS_U32_MAX;
    }
    site = instruction_offset + (ctool_u32)field->byte_offset;
  }
  first =
      dis_relocation_site_lower_bound(report, section_file_index, site);
  while (first < report->relocation_site_order_count) {
    ctool_u32 relocation_index = report->relocation_site_order[first];
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[relocation_index];
    if (relocation->offset != site ||
        (object->file_type == CTOOL_ELF32_ET_REL &&
         relocation->target_section_file_index != section_file_index)) {
      break;
    }
    if ((relocation_claimed == (const ctool_u8 *)0 ||
         relocation_claimed[relocation_index] == 0u) &&
        dis_field_accepts_relocation(field, relocation) == CTOOL_TRUE) {
      return relocation_index;
    }
    first++;
  }
  return DIS_U32_MAX;
}

static ctool_bool dis_field_has_relocation(
    const ctool_dis_report_t *report, ctool_u32 section_file_index,
    ctool_u32 instruction_offset, const ctool_x86_field_t *field) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 first;
  ctool_u32 site;
  if (object->file_type != CTOOL_ELF32_ET_REL ||
      instruction_offset > DIS_U32_MAX - (ctool_u32)field->byte_offset) {
    return CTOOL_FALSE;
  }
  site = instruction_offset + (ctool_u32)field->byte_offset;
  first =
      dis_relocation_site_lower_bound(report, section_file_index, site);
  if (first < report->relocation_site_order_count) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[report->relocation_site_order[first]];
    if (relocation->target_section_file_index == section_file_index &&
        relocation->offset == site) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static void dis_zero_report(ctool_dis_report_t *report) {
  ctool_u8 *bytes = (ctool_u8 *)report;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*report); index++) {
    bytes[index] = 0u;
  }
}

static ctool_status_t dis_emit(ctool_job_t *job, ctool_string_t path,
                               ctool_u32 code, ctool_u32 column,
                               const char *message, ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = column;
  diagnostic.message = ctool_string(message);
  emitted = ctool_job_emit(job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t dis_bad_request(ctool_job_t *job,
                                      const ctool_source_t *source,
                                      const char *message) {
  ctool_string_t path = ctool_string("");
  if (source != (const ctool_source_t *)0) {
    path = source->path.text;
  }
  return dis_emit(job, path, CTOOL_DIS_DIAG_INVALID_REQUEST, 0u, message,
                  CTOOL_ERR_INVALID_ARGUMENT);
}

typedef enum {
  DIS_RAW_MAP_VALID = 0,
  DIS_RAW_MAP_NO_RANGES,
  DIS_RAW_MAP_MISSING_STORAGE,
  DIS_RAW_MAP_EMPTY_INPUT,
  DIS_RAW_MAP_TOO_MANY_RANGES,
  DIS_RAW_MAP_NONZERO_START,
  DIS_RAW_MAP_INVALID_KIND,
  DIS_RAW_MAP_OUTSIDE_INPUT,
  DIS_RAW_MAP_UNORDERED
} dis_raw_map_issue_t;

static ctool_bool dis_x86_mode_valid(ctool_x86_mode_t mode) {
  return mode == CTOOL_X86_MODE_16 || mode == CTOOL_X86_MODE_32
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool
dis_raw_range_kind_valid(ctool_dis_raw_range_kind_t kind) {
  return kind == CTOOL_DIS_RAW_RANGE_CODE16 ||
                 kind == CTOOL_DIS_RAW_RANGE_CODE32 ||
                 kind == CTOOL_DIS_RAW_RANGE_DATA
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_x86_mode_t
dis_raw_range_mode(ctool_dis_raw_range_kind_t kind) {
  return kind == CTOOL_DIS_RAW_RANGE_CODE16 ? CTOOL_X86_MODE_16
                                            : CTOOL_X86_MODE_32;
}

static dis_raw_map_issue_t dis_raw_map_issue(
    ctool_u32 source_size, const ctool_dis_raw_range_t *ranges,
    ctool_u32 range_count) {
  ctool_u32 index;
  if (range_count == 0u) {
    return DIS_RAW_MAP_NO_RANGES;
  }
  if (ranges == (const ctool_dis_raw_range_t *)0) {
    return DIS_RAW_MAP_MISSING_STORAGE;
  }
  if (source_size == 0u) {
    return DIS_RAW_MAP_EMPTY_INPUT;
  }
  if (range_count > source_size) {
    return DIS_RAW_MAP_TOO_MANY_RANGES;
  }
  if (ranges[0].offset != 0u) {
    return DIS_RAW_MAP_NONZERO_START;
  }
  for (index = 0u; index < range_count; index++) {
    if (dis_raw_range_kind_valid(ranges[index].kind) == CTOOL_FALSE) {
      return DIS_RAW_MAP_INVALID_KIND;
    }
    if (ranges[index].offset >= source_size) {
      return DIS_RAW_MAP_OUTSIDE_INPUT;
    }
    if (index != 0u && ranges[index].offset <= ranges[index - 1u].offset) {
      return DIS_RAW_MAP_UNORDERED;
    }
  }
  return DIS_RAW_MAP_VALID;
}

static const char *dis_raw_map_message(dis_raw_map_issue_t issue) {
  switch (issue) {
  case DIS_RAW_MAP_NO_RANGES:
    return "raw range map requires at least one range";
  case DIS_RAW_MAP_MISSING_STORAGE:
    return "raw range map storage is missing";
  case DIS_RAW_MAP_EMPTY_INPUT:
    return "raw range map requires nonempty input";
  case DIS_RAW_MAP_TOO_MANY_RANGES:
    return "raw range map has too many ranges";
  case DIS_RAW_MAP_NONZERO_START:
    return "raw range map must start at offset zero";
  case DIS_RAW_MAP_INVALID_KIND:
    return "raw range kind must be code16, code32, or data";
  case DIS_RAW_MAP_OUTSIDE_INPUT:
    return "raw range start is outside input";
  case DIS_RAW_MAP_UNORDERED:
    return "raw range starts must increase without overlap";
  case DIS_RAW_MAP_VALID:
  default:
    return "raw range map is invalid";
  }
}

static ctool_dis_raw_range_kind_t dis_request_raw_kind_at_offset(
    const ctool_dis_request_t *request, ctool_u32 offset) {
  ctool_u32 index;
  ctool_dis_raw_range_kind_t kind = CTOOL_DIS_RAW_RANGE_DATA;
  for (index = 0u; index < request->raw_range_count; index++) {
    if (request->raw_ranges[index].offset > offset) {
      break;
    }
    kind = request->raw_ranges[index].kind;
  }
  return kind;
}

static ctool_bool dis_raw_address_inside_image(const ctool_source_t *source,
                                                ctool_u32 base_address,
                                                ctool_u32 address) {
  return address >= base_address &&
                 address - base_address < source->contents.size
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static const char *dis_raw_edge_issue(const ctool_source_t *source,
                                      const ctool_dis_request_t *request) {
  ctool_u32 index;
  if (request->raw_edge_metadata_present == CTOOL_FALSE) {
    if (request->raw_edges != (const ctool_dis_raw_edge_t *)0 ||
        request->raw_edge_count != 0u) {
      return "raw control-edge storage requires explicit metadata";
    }
    if ((request->policies & CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES) != 0u) {
      return "source control-edge checks require v2 raw metadata";
    }
    return (const char *)0;
  }
  if (request->raw_mode != CTOOL_DIS_RAW_RANGE_MAP) {
    return "raw control-edge metadata requires a range map";
  }
  if (request->raw_edge_count != 0u &&
      request->raw_edges == (const ctool_dis_raw_edge_t *)0) {
    return "raw control-edge metadata storage is missing";
  }
  if (request->raw_edge_count > source->contents.size) {
    return "raw control-edge metadata has too many rows";
  }
  for (index = 0u; index < request->raw_edge_count; index++) {
    const ctool_dis_raw_edge_t *edge = &request->raw_edges[index];
    ctool_dis_raw_range_kind_t source_kind;
    if (edge->source_offset >= source->contents.size) {
      return "raw control-edge source is outside input";
    }
    if (index != 0u &&
        edge->source_offset <= request->raw_edges[index - 1u].source_offset) {
      return "raw control-edge sources must increase without overlap";
    }
    source_kind = dis_request_raw_kind_at_offset(request,
                                                 edge->source_offset);
    if (source_kind == CTOOL_DIS_RAW_RANGE_DATA) {
      return "raw control-edge source must be inside code";
    }
    if (edge->kind != CTOOL_DIS_RAW_EDGE_RELATIVE &&
        edge->kind != CTOOL_DIS_RAW_EDGE_FAR &&
        edge->kind != CTOOL_DIS_RAW_EDGE_INDIRECT) {
      return "raw control-edge kind is invalid";
    }
    if (edge->class_id != CTOOL_DIS_RAW_EDGE_LOCAL &&
        edge->class_id != CTOOL_DIS_RAW_EDGE_EXTERNAL &&
        edge->class_id != CTOOL_DIS_RAW_EDGE_UNPROVABLE) {
      return "raw control-edge class is invalid";
    }
    if (edge->class_id == CTOOL_DIS_RAW_EDGE_LOCAL) {
      ctool_dis_raw_range_kind_t target_kind;
      if (edge->kind == CTOOL_DIS_RAW_EDGE_INDIRECT ||
          edge->target_offset == CTOOL_DIS_RAW_EDGE_NO_TARGET ||
          dis_x86_mode_valid(edge->target_mode) == CTOOL_FALSE ||
          edge->target_offset >= source->contents.size ||
          request->raw_base_address >
              DIS_U32_MAX - edge->target_offset ||
          request->raw_base_address + edge->target_offset !=
              edge->target_address) {
        return "raw local control-edge target is inconsistent";
      }
      target_kind =
          dis_request_raw_kind_at_offset(request, edge->target_offset);
      if (target_kind == CTOOL_DIS_RAW_RANGE_DATA ||
          dis_raw_range_mode(target_kind) != edge->target_mode) {
        return "raw local control-edge mode disagrees with the range map";
      }
      if ((edge->kind == CTOOL_DIS_RAW_EDGE_RELATIVE &&
           edge->target_segment != 0u) ||
          edge->target_segment == CTOOL_DIS_RAW_EDGE_NO_TARGET) {
        return "raw local control-edge segment is inconsistent";
      }
    } else if (edge->class_id == CTOOL_DIS_RAW_EDGE_EXTERNAL) {
      if (edge->kind == CTOOL_DIS_RAW_EDGE_INDIRECT ||
          edge->target_offset != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
          dis_x86_mode_valid(edge->target_mode) == CTOOL_FALSE ||
          edge->target_segment == CTOOL_DIS_RAW_EDGE_NO_TARGET ||
          (edge->kind == CTOOL_DIS_RAW_EDGE_RELATIVE &&
           edge->target_segment != 0u)) {
        return "raw external control-edge target is inconsistent";
      }
      if (dis_raw_address_inside_image(source, request->raw_base_address,
                                       edge->target_address) == CTOOL_TRUE) {
        return "raw external control-edge target is inside the image";
      }
    } else if (edge->kind != CTOOL_DIS_RAW_EDGE_INDIRECT ||
               edge->target_offset != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
               edge->target_address != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
               edge->target_mode != (ctool_x86_mode_t)0 ||
               edge->target_segment != CTOOL_DIS_RAW_EDGE_NO_TARGET) {
      return "raw unprovable control-edge target must stay unknown";
    }
  }
  return (const char *)0;
}

static ctool_i32 dis_signed_bits(ctool_u32 value) {
  if (value <= 0x7fffffffu) {
    return (ctool_i32)value;
  }
  if (value == 0x80000000u) {
    return (-2147483647 - 1);
  }
  return -(ctool_i32)((~value) + 1u);
}

static ctool_u32 dis_relative_target(
    ctool_u32 logical_address, const ctool_x86_decoded_t *decoded,
    const ctool_x86_operand_t *operand) {
  ctool_u32 target = logical_address + (ctool_u32)decoded->encoding.size +
                     (ctool_u32)dis_signed_bits(operand->as.value.bits);
  if (decoded->instruction.operand_bits == 16u) {
    target &= 0xffffu;
  }
  return target;
}

static void dis_instruction_start_set(ctool_u8 *starts, ctool_u32 offset) {
  starts[offset / 8u] |= (ctool_u8)(1u << (offset % 8u));
}

static ctool_bool dis_instruction_start_get(const ctool_u8 *starts,
                                             ctool_u32 offset) {
  return (starts[offset / 8u] & (ctool_u8)(1u << (offset % 8u))) != 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_dis_raw_range_kind_t
dis_raw_kind_at_offset(const ctool_dis_report_t *report, ctool_u32 offset) {
  ctool_u32 first;
  ctool_u32 last;
  if (report->mode != CTOOL_DIS_RAW_RANGE_MAP) {
    return report->mode == CTOOL_X86_MODE_16
               ? CTOOL_DIS_RAW_RANGE_CODE16
               : CTOOL_DIS_RAW_RANGE_CODE32;
  }
  first = 0u;
  last = report->raw_range_count;
  while (first < last) {
    ctool_u32 middle = first + (last - first) / 2u;
    if (report->raw_ranges[middle].offset <= offset) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  if (first != 0u) {
    return report->raw_ranges[first - 1u].kind;
  }
  return CTOOL_DIS_RAW_RANGE_DATA;
}

static ctool_status_t dis_scan_local_target_region(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report, ctool_u32 first, ctool_u32 last,
    ctool_x86_mode_t mode, ctool_u8 *instruction_starts,
    ctool_bool mark_starts) {
  ctool_bytes_t bytes;
  ctool_u32 offset = 0u;
  if (first == last) {
    return CTOOL_OK;
  }
  bytes = ctool_bytes(report->source->contents.data + first, last - first);
  while (offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_status_t status =
        decoder == (const ctool_x86_decoder_t *)0
            ? ctool_x86_decode(job, mode, bytes, offset, &decoded)
            : ctool_x86_decode_indexed(job, decoder, mode, bytes, offset,
                                       &decoded);
    if (status != CTOOL_OK) {
      return status;
    }
    if (decoded.kind == CTOOL_X86_DECODE_KNOWN) {
      ctool_u32 source_offset = first + offset;
      if (mark_starts == CTOOL_TRUE) {
        dis_instruction_start_set(instruction_starts, source_offset);
      } else {
        ctool_u32 operand_index;
        for (operand_index = 0u;
             operand_index < (ctool_u32)decoded.instruction.operand_count;
             operand_index++) {
          const ctool_x86_operand_t *operand =
              &decoded.instruction.operands[operand_index];
          ctool_u32 target;
          ctool_u32 target_offset;
          ctool_dis_raw_range_kind_t target_kind;
          ctool_x86_mode_t target_mode;
          if (operand->kind != CTOOL_X86_OPERAND_RELATIVE ||
              operand->as.value.kind != CTOOL_X86_VALUE_CONSTANT) {
            continue;
          }
          report->decode_summary.direct_relative_target_count++;
          target = dis_relative_target(report->base_address + source_offset,
                                       &decoded, operand);
          if (decoded.instruction.operand_bits == 16u) {
            target_offset =
                (target - (report->base_address & 0xffffu)) & 0xffffu;
            if (target_offset >= report->source->contents.size) {
              report->decode_summary.direct_relative_outside_image_count++;
              continue;
            }
          } else {
            if (target < report->base_address ||
                target - report->base_address >=
                    report->source->contents.size) {
              report->decode_summary.direct_relative_outside_image_count++;
              continue;
            }
            target_offset = target - report->base_address;
          }
          target_kind = dis_raw_kind_at_offset(report, target_offset);
          if (target_kind == CTOOL_DIS_RAW_RANGE_DATA) {
            report->decode_summary.direct_relative_data_count++;
            continue;
          }
          target_mode = dis_raw_range_mode(target_kind);
          if (target_mode != mode) {
            report->decode_summary.direct_relative_wrong_mode_count++;
            continue;
          }
          if (dis_instruction_start_get(instruction_starts, target_offset) ==
              CTOOL_FALSE) {
            report->decode_summary.direct_relative_mid_instruction_count++;
          }
        }
      }
    } else if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u || decoded.consumed > bytes.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    offset += (ctool_u32)decoded.consumed;
  }
  return CTOOL_OK;
}

static ctool_status_t dis_prepare_raw_local_target_summary(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_u8 *instruction_starts = (ctool_u8 *)0;
  ctool_u32 bitset_size = report->source->contents.size / 8u;
  ctool_u32 pass;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  ctool_status_t rewind_status;
  if (report->source->contents.size % 8u != 0u) {
    bitset_size++;
  }
  if (bitset_size != 0u) {
    status = ctool_arena_alloc_zero(arena, bitset_size,
                                    (ctool_u32)sizeof(ctool_u8),
                                    (ctool_u32)sizeof(ctool_u8),
                                    (void **)&instruction_starts);
  }
  for (pass = 0u; status == CTOOL_OK && pass < 2u; pass++) {
    if (report->mode != CTOOL_DIS_RAW_RANGE_MAP) {
      status = dis_scan_local_target_region(
          job, decoder, report, 0u, report->source->contents.size,
          report->mode, instruction_starts,
          pass == 0u ? CTOOL_TRUE : CTOOL_FALSE);
      continue;
    }
    for (index = 0u;
         status == CTOOL_OK && index < report->raw_range_count; index++) {
      ctool_u32 first = report->raw_ranges[index].offset;
      ctool_u32 last = index + 1u < report->raw_range_count
                           ? report->raw_ranges[index + 1u].offset
                           : report->source->contents.size;
      if (report->raw_ranges[index].kind == CTOOL_DIS_RAW_RANGE_DATA) {
        continue;
      }
      status = dis_scan_local_target_region(
          job, decoder, report, first, last,
          dis_raw_range_mode(report->raw_ranges[index].kind),
          instruction_starts, pass == 0u ? CTOOL_TRUE : CTOOL_FALSE);
    }
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  return status == CTOOL_OK ? rewind_status : status;
}

static ctool_bool dis_decoded_raw_edge(
    const ctool_x86_decoded_t *decoded, ctool_dis_raw_edge_kind_t *kind_out,
    const ctool_x86_operand_t **operand_out) {
  ctool_u32 index;
  if (decoded->kind != CTOOL_X86_DECODE_KNOWN) {
    return CTOOL_FALSE;
  }
  for (index = 0u;
       index < (ctool_u32)decoded->instruction.operand_count; index++) {
    const ctool_x86_operand_t *operand =
        &decoded->instruction.operands[index];
    if (operand->kind == CTOOL_X86_OPERAND_RELATIVE &&
        operand->as.value.kind == CTOOL_X86_VALUE_CONSTANT) {
      *kind_out = CTOOL_DIS_RAW_EDGE_RELATIVE;
      *operand_out = operand;
      return CTOOL_TRUE;
    }
  }
  if (decoded->instruction.mnemonic != CTOOL_X86_MN_CALL &&
      decoded->instruction.mnemonic != CTOOL_X86_MN_JMP) {
    return CTOOL_FALSE;
  }
  if (decoded->instruction.operand_count == 0u) {
    return CTOOL_FALSE;
  }
  if (decoded->instruction.operands[0].kind ==
      CTOOL_X86_OPERAND_FAR_POINTER) {
    *kind_out = CTOOL_DIS_RAW_EDGE_FAR;
    *operand_out = &decoded->instruction.operands[0];
    return CTOOL_TRUE;
  }
  if (decoded->instruction.operands[0].kind ==
          CTOOL_X86_OPERAND_REGISTER ||
      decoded->instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY) {
    *kind_out = CTOOL_DIS_RAW_EDGE_INDIRECT;
    *operand_out = &decoded->instruction.operands[0];
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_u32 dis_raw_edge_index(const ctool_dis_report_t *report,
                                    ctool_u32 source_offset) {
  ctool_u32 first = 0u;
  ctool_u32 last = report->raw_edge_count;
  while (first < last) {
    ctool_u32 middle = first + (last - first) / 2u;
    if (report->raw_edges[middle].source_offset < source_offset) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  if (first < report->raw_edge_count &&
      report->raw_edges[first].source_offset == source_offset) {
    return first;
  }
  return DIS_U32_MAX;
}

static ctool_bool dis_raw_target_offset(const ctool_dis_report_t *report,
                                        ctool_u32 target_address,
                                        ctool_u16 operand_bits,
                                        ctool_u32 *offset_out) {
  ctool_u32 offset;
  if (operand_bits == 16u) {
    offset = (target_address - (report->base_address & 0xffffu)) & 0xffffu;
    if (offset >= report->source->contents.size) {
      return CTOOL_FALSE;
    }
  } else {
    if (target_address < report->base_address ||
        target_address - report->base_address >=
            report->source->contents.size) {
      return CTOOL_FALSE;
    }
    offset = target_address - report->base_address;
  }
  *offset_out = offset;
  return CTOOL_TRUE;
}

static void dis_validate_raw_edge_target(
    ctool_dis_report_t *report, const ctool_dis_raw_edge_t *edge,
    ctool_x86_mode_t source_mode, ctool_u32 actual_address,
    ctool_u16 actual_bits, const ctool_u8 *instruction_starts,
    ctool_bool *invalid_out) {
  ctool_bool target_mismatch =
      actual_bits == 16u
          ? ((actual_address & 0xffffu) !=
                     (edge->target_address & 0xffffu)
                 ? CTOOL_TRUE
                 : CTOOL_FALSE)
          : (actual_address != edge->target_address ? CTOOL_TRUE
                                                     : CTOOL_FALSE);
  ctool_bool mode_mismatch = CTOOL_FALSE;
  if (edge->class_id == CTOOL_DIS_RAW_EDGE_LOCAL) {
    ctool_u32 actual_offset;
    if (dis_raw_target_offset(report, actual_address, actual_bits,
                              &actual_offset) == CTOOL_FALSE ||
        actual_offset != edge->target_offset ||
        dis_instruction_start_get(instruction_starts,
                                  edge->target_offset) == CTOOL_FALSE) {
      target_mismatch = CTOOL_TRUE;
    }
    if (dis_raw_target_offset(report, actual_address, actual_bits,
                              &actual_offset) == CTOOL_FALSE ||
        dis_raw_range_mode(dis_raw_kind_at_offset(report, actual_offset)) !=
            edge->target_mode) {
      mode_mismatch = CTOOL_TRUE;
    }
  } else {
    ctool_x86_mode_t actual_mode =
        edge->kind == CTOOL_DIS_RAW_EDGE_FAR
            ? (ctool_x86_mode_t)actual_bits
            : source_mode;
    if (actual_mode != edge->target_mode) {
      mode_mismatch = CTOOL_TRUE;
    }
  }
  if (target_mismatch == CTOOL_TRUE) {
    report->decode_summary.source_control_edge_target_mismatch_count++;
    *invalid_out = CTOOL_TRUE;
  }
  if (mode_mismatch == CTOOL_TRUE) {
    report->decode_summary.source_control_edge_target_mode_mismatch_count++;
    *invalid_out = CTOOL_TRUE;
  }
}

static ctool_status_t dis_scan_raw_source_edge_region(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report, ctool_u32 first, ctool_u32 last,
    ctool_x86_mode_t mode, ctool_u8 *instruction_starts,
    ctool_u8 *matched_edges, ctool_bool mark_starts) {
  ctool_bytes_t bytes;
  ctool_u32 offset = 0u;
  if (first == last) {
    return CTOOL_OK;
  }
  bytes = ctool_bytes(report->source->contents.data + first, last - first);
  while (offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_status_t status =
        decoder == (const ctool_x86_decoder_t *)0
            ? ctool_x86_decode(job, mode, bytes, offset, &decoded)
            : ctool_x86_decode_indexed(job, decoder, mode, bytes, offset,
                                       &decoded);
    if (status != CTOOL_OK) {
      return status;
    }
    if (decoded.kind == CTOOL_X86_DECODE_KNOWN) {
      ctool_u32 source_offset = first + offset;
      if (mark_starts == CTOOL_TRUE) {
        dis_instruction_start_set(instruction_starts, source_offset);
      } else {
        ctool_dis_raw_edge_kind_t decoded_kind;
        const ctool_x86_operand_t *operand =
            (const ctool_x86_operand_t *)0;
        if (dis_decoded_raw_edge(&decoded, &decoded_kind, &operand) ==
            CTOOL_TRUE) {
          ctool_u32 edge_index =
              dis_raw_edge_index(report, source_offset);
          if (edge_index == DIS_U32_MAX) {
            report->decode_summary.source_control_edge_count++;
            report->decode_summary.source_control_edge_invalid_count++;
            report->decode_summary
                .source_control_edge_source_mismatch_count++;
          } else {
            const ctool_dis_raw_edge_t *edge =
                &report->raw_edges[edge_index];
            ctool_bool invalid = CTOOL_FALSE;
            dis_instruction_start_set(matched_edges, edge_index);
            if (edge->kind != decoded_kind) {
              report->decode_summary
                  .source_control_edge_source_mismatch_count++;
              invalid = CTOOL_TRUE;
            } else if (decoded_kind == CTOOL_DIS_RAW_EDGE_INDIRECT) {
              if (edge->class_id != CTOOL_DIS_RAW_EDGE_UNPROVABLE) {
                report->decode_summary
                    .source_control_edge_source_mismatch_count++;
                invalid = CTOOL_TRUE;
              }
            } else if (decoded_kind == CTOOL_DIS_RAW_EDGE_RELATIVE) {
              ctool_u32 target = dis_relative_target(
                  report->base_address + source_offset, &decoded, operand);
              dis_validate_raw_edge_target(
                  report, edge, mode, target,
                  decoded.instruction.operand_bits, instruction_starts,
                  &invalid);
            } else {
              const ctool_x86_far_pointer_t *far =
                  &operand->as.far_pointer;
              if (far->offset.kind != CTOOL_X86_VALUE_CONSTANT ||
                  far->segment.kind != CTOOL_X86_VALUE_CONSTANT ||
                  far->segment.bits != edge->target_segment) {
                report->decode_summary
                    .source_control_edge_target_mismatch_count++;
                invalid = CTOOL_TRUE;
              }
              dis_validate_raw_edge_target(
                  report, edge, mode, far->offset.bits,
                  decoded.instruction.operand_bits, instruction_starts,
                  &invalid);
            }
            if (invalid == CTOOL_TRUE) {
              report->decode_summary.source_control_edge_invalid_count++;
            }
          }
        }
      }
    } else if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u || decoded.consumed > bytes.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    offset += (ctool_u32)decoded.consumed;
  }
  return CTOOL_OK;
}

static ctool_status_t dis_prepare_raw_source_edge_summary(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_u32 start_bytes = report->source->contents.size / 8u;
  ctool_u32 matched_bytes = report->raw_edge_count / 8u;
  ctool_u8 *instruction_starts = (ctool_u8 *)0;
  ctool_u8 *matched_edges = (ctool_u8 *)0;
  ctool_u32 pass;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  ctool_status_t rewind_status;
  if (report->source->contents.size % 8u != 0u) {
    start_bytes++;
  }
  if (report->raw_edge_count % 8u != 0u) {
    matched_bytes++;
  }
  if (start_bytes != 0u) {
    status = ctool_arena_alloc_zero(
        arena, start_bytes, (ctool_u32)sizeof(ctool_u8),
        (ctool_u32)sizeof(ctool_u8), (void **)&instruction_starts);
  }
  if (status == CTOOL_OK && matched_bytes != 0u) {
    status = ctool_arena_alloc_zero(
        arena, matched_bytes, (ctool_u32)sizeof(ctool_u8),
        (ctool_u32)sizeof(ctool_u8), (void **)&matched_edges);
  }
  report->decode_summary.source_control_edge_count =
      report->raw_edge_count;
  for (index = 0u; index < report->raw_edge_count; index++) {
    if (report->raw_edges[index].class_id == CTOOL_DIS_RAW_EDGE_LOCAL) {
      report->decode_summary.source_control_edge_local_count++;
    } else if (report->raw_edges[index].class_id ==
               CTOOL_DIS_RAW_EDGE_EXTERNAL) {
      report->decode_summary.source_control_edge_external_count++;
    } else {
      report->decode_summary.source_control_edge_unprovable_count++;
    }
  }
  for (pass = 0u; status == CTOOL_OK && pass < 2u; pass++) {
    for (index = 0u;
         status == CTOOL_OK && index < report->raw_range_count; index++) {
      ctool_u32 first = report->raw_ranges[index].offset;
      ctool_u32 last = index + 1u < report->raw_range_count
                           ? report->raw_ranges[index + 1u].offset
                           : report->source->contents.size;
      if (report->raw_ranges[index].kind == CTOOL_DIS_RAW_RANGE_DATA) {
        continue;
      }
      status = dis_scan_raw_source_edge_region(
          job, decoder, report, first, last,
          dis_raw_range_mode(report->raw_ranges[index].kind),
          instruction_starts, matched_edges,
          pass == 0u ? CTOOL_TRUE : CTOOL_FALSE);
    }
  }
  for (index = 0u; status == CTOOL_OK && index < report->raw_edge_count;
       index++) {
    if (dis_instruction_start_get(matched_edges, index) == CTOOL_FALSE) {
      report->decode_summary.source_control_edge_invalid_count++;
      report->decode_summary.source_control_edge_source_mismatch_count++;
    }
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  return status == CTOOL_OK ? rewind_status : status;
}

static ctool_status_t dis_scan_elf_local_target_section(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report, const ctool_elf32_section_t *section,
    ctool_u8 *instruction_starts, ctool_bool mark_starts) {
  ctool_u32 offset = 0u;
  while (offset < section->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_status_t status =
        decoder == (const ctool_x86_decoder_t *)0
            ? ctool_x86_decode(job, CTOOL_X86_MODE_32, section->contents,
                               offset, &decoded)
            : ctool_x86_decode_indexed(job, decoder, CTOOL_X86_MODE_32,
                                       section->contents, offset, &decoded);
    if (status != CTOOL_OK) {
      return status;
    }
    if (decoded.kind == CTOOL_X86_DECODE_KNOWN) {
      if (mark_starts == CTOOL_TRUE) {
        dis_instruction_start_set(instruction_starts, offset);
      } else {
        ctool_u32 operand_index;
        for (operand_index = 0u;
             operand_index < (ctool_u32)decoded.instruction.operand_count;
             operand_index++) {
          const ctool_x86_operand_t *operand =
              &decoded.instruction.operands[operand_index];
          ctool_bool relocated = CTOOL_FALSE;
          ctool_u32 field_index;
          ctool_u32 target;
          if (operand->kind != CTOOL_X86_OPERAND_RELATIVE ||
              operand->as.value.kind != CTOOL_X86_VALUE_CONSTANT) {
            continue;
          }
          for (field_index = 0u;
               field_index < (ctool_u32)decoded.encoding.field_count;
               field_index++) {
            const ctool_x86_field_t *field =
                &decoded.encoding.fields[field_index];
            if ((ctool_u32)field->operand_index == operand_index &&
                field->kind == CTOOL_X86_FIELD_RELATIVE &&
                dis_field_has_relocation(report, section->file_index, offset,
                                         field) == CTOOL_TRUE) {
              relocated = CTOOL_TRUE;
            }
          }
          if (relocated == CTOOL_TRUE) {
            continue;
          }
          report->decode_summary.direct_relative_target_count++;
          target = dis_relative_target(offset, &decoded, operand);
          if (target >= section->contents.size) {
            report->decode_summary.direct_relative_outside_section_count++;
          } else if (dis_instruction_start_get(instruction_starts, target) ==
                     CTOOL_FALSE) {
            report->decode_summary.direct_relative_mid_instruction_count++;
          }
        }
      }
    } else if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u ||
        decoded.consumed > section->contents.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    offset += (ctool_u32)decoded.consumed;
  }
  return CTOOL_OK;
}

static ctool_bool dis_rel_symbol_has_executable_section(
    const ctool_dis_report_t *report, const ctool_elf32_symbol_t *symbol,
    const ctool_elf32_section_t **section_out) {
  const ctool_elf32_section_t *section;
  if (symbol->section_file_index >= report->elf32.section_count) {
    return CTOOL_FALSE;
  }
  section = &report->elf32.sections[symbol->section_file_index];
  if (section->type != CTOOL_ELF32_SHT_PROGBITS ||
      (section->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
      symbol->value >= section->contents.size) {
    return CTOOL_FALSE;
  }
  *section_out = section;
  return CTOOL_TRUE;
}

static ctool_status_t dis_prepare_rel_policy_summaries(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_u32 index;
  if ((report->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      const ctool_elf32_section_t *section =
          (const ctool_elf32_section_t *)0;
      if (symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED ||
          symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION) {
        continue;
      }
      report->decode_summary.code_anchor_count++;
      if (dis_rel_symbol_has_executable_section(report, symbol, &section) ==
          CTOOL_FALSE) {
        report->decode_summary.code_anchor_outside_executable_count++;
      }
    }
  }
  for (index = 0u; index < report->elf32.section_count; index++) {
    const ctool_elf32_section_t *section = &report->elf32.sections[index];
    ctool_arena_mark_t mark;
    ctool_u8 *instruction_starts = (ctool_u8 *)0;
    ctool_u32 bitset_size;
    ctool_status_t status = CTOOL_OK;
    ctool_status_t rewind_status;
    if (section->type != CTOOL_ELF32_SHT_PROGBITS ||
        (section->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
        section->contents.size == 0u) {
      continue;
    }
    mark = ctool_arena_mark(arena);
    bitset_size = section->contents.size / 8u;
    if (section->contents.size % 8u != 0u) {
      bitset_size++;
    }
    status = ctool_arena_alloc_zero(
        arena, bitset_size, (ctool_u32)sizeof(ctool_u8),
        (ctool_u32)sizeof(ctool_u8), (void **)&instruction_starts);
    if (status == CTOOL_OK) {
      status = dis_scan_elf_local_target_section(
          job, decoder, report, section, instruction_starts, CTOOL_TRUE);
    }
    if (status == CTOOL_OK &&
        (report->policies & CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u) {
      status = dis_scan_elf_local_target_section(
          job, decoder, report, section, instruction_starts, CTOOL_FALSE);
    }
    if (status == CTOOL_OK &&
        (report->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u) {
      ctool_u32 symbol_index;
      for (symbol_index = 0u;
           symbol_index < report->elf32.symbol_count; symbol_index++) {
        const ctool_elf32_symbol_t *symbol =
            &report->elf32.symbols[symbol_index];
        const ctool_elf32_section_t *symbol_section =
            (const ctool_elf32_section_t *)0;
        if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
            symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
            dis_rel_symbol_has_executable_section(
                report, symbol, &symbol_section) == CTOOL_FALSE ||
            symbol_section != section) {
          continue;
        }
        if (dis_instruction_start_get(instruction_starts, symbol->value) ==
            CTOOL_FALSE) {
          report->decode_summary.code_anchor_mid_instruction_count++;
        }
      }
    }
    rewind_status = ctool_arena_rewind(arena, mark);
    if (status != CTOOL_OK) {
      return status;
    }
    if (rewind_status != CTOOL_OK) {
      return rewind_status;
    }
  }
  return CTOOL_OK;
}

typedef struct {
  ctool_u32 address;
  ctool_u32 memory_size;
  ctool_bytes_t contents;
} dis_exec_region_t;

static ctool_u32 dis_exec_region_count(const ctool_dis_report_t *report) {
  return report->input == CTOOL_DIS_INPUT_PE32
             ? report->pe32.section_count
             : report->elf32.program_header_count;
}

static ctool_bool dis_exec_code_region_at(
    const ctool_dis_report_t *report, ctool_u32 index,
    dis_exec_region_t *region_out) {
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    const ctool_pe32_section_t *section;
    if (index >= report->pe32.section_count) {
      return CTOOL_FALSE;
    }
    section = &report->pe32.sections[index];
    if ((section->characteristics & CTOOL_PE32_SCN_EXECUTE) == 0u ||
        section->contents.size == 0u) {
      return CTOOL_FALSE;
    }
    region_out->address = report->pe32.image_base +
                          section->virtual_address;
    region_out->memory_size = section->virtual_size;
    region_out->contents = section->contents;
    return CTOOL_TRUE;
  }
  if (index < report->elf32.program_header_count) {
    const ctool_elf32_program_header_t *program =
        &report->elf32.program_headers[index];
    if (program->type == CTOOL_ELF32_PT_LOAD &&
        (program->flags & CTOOL_ELF32_PF_X) != 0u &&
        program->contents.size != 0u) {
      region_out->address = program->virtual_address;
      region_out->memory_size = program->memory_size;
      region_out->contents = program->contents;
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool dis_exec_loaded_region_at(
    const ctool_dis_report_t *report, ctool_u32 index,
    ctool_u32 *address_out, ctool_u32 *size_out) {
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    if (index >= report->pe32.section_count) {
      return CTOOL_FALSE;
    }
    *address_out = report->pe32.image_base +
                   report->pe32.sections[index].virtual_address;
    *size_out = report->pe32.sections[index].virtual_size;
    return CTOOL_TRUE;
  }
  if (index < report->elf32.program_header_count &&
      report->elf32.program_headers[index].type == CTOOL_ELF32_PT_LOAD) {
    *address_out = report->elf32.program_headers[index].virtual_address;
    *size_out = report->elf32.program_headers[index].memory_size;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_bool
dis_exec_programs_are_static(const ctool_elf32_object_t *object) {
  ctool_u32 index;
  for (index = 0u; index < object->program_header_count; index++) {
    ctool_u32 type = object->program_headers[index].type;
    if (type == CTOOL_ELF32_PT_DYNAMIC ||
        type == CTOOL_ELF32_PT_INTERP) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool dis_exec_code_regions_overlap(
    const dis_exec_region_t *left, const dis_exec_region_t *right) {
  ctool_u64 left_start = (ctool_u64)left->address;
  ctool_u64 right_start = (ctool_u64)right->address;
  ctool_u64 left_end = left_start + (ctool_u64)left->contents.size;
  ctool_u64 right_end = right_start + (ctool_u64)right->contents.size;
  return left_start < right_end && right_start < left_end
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t dis_exec_code_map_size(
    ctool_job_t *job, ctool_dis_report_t *report,
    ctool_u32 *code_size_out) {
  ctool_u32 code_size = 0u;
  ctool_u32 index;
  for (index = 0u; index < dis_exec_region_count(report); index++) {
    dis_exec_region_t region;
    ctool_u32 other;
    if (dis_exec_code_region_at(report, index, &region) == CTOOL_FALSE) {
      continue;
    }
    if (region.contents.size > DIS_U32_MAX - code_size) {
      return dis_bad_request(job, report->source,
          (report->policies & CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u
              ? "executable local target code map exceeds supported size"
              : "executable code anchor map exceeds supported size");
    }
    for (other = 0u; other < index; other++) {
      dis_exec_region_t earlier;
      if (dis_exec_code_region_at(report, other, &earlier) == CTOOL_TRUE &&
          dis_exec_code_regions_overlap(&earlier, &region) == CTOOL_TRUE) {
        return dis_bad_request(
            job, report->source,
            report->input == CTOOL_DIS_INPUT_PE32
                ? ((report->policies &
                    CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u
                       ? "PE32 local target checks require non-overlapping "
                         "executable sections"
                       : "PE32 code anchor checks require non-overlapping "
                         "executable sections")
                : ((report->policies &
                    CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u
                       ? "executable local target checks require "
                         "non-overlapping executable load regions"
                       : "executable code anchor checks require "
                         "non-overlapping executable load regions"));
      }
    }
    code_size += region.contents.size;
  }
  *code_size_out = code_size;
  return CTOOL_OK;
}

static ctool_bool dis_exec_code_map_offset(
    const ctool_dis_report_t *report, ctool_u32 address,
    ctool_u32 *map_offset_out) {
  ctool_u32 map_base = 0u;
  ctool_u32 index;
  for (index = 0u; index < dis_exec_region_count(report); index++) {
    dis_exec_region_t region;
    if (dis_exec_code_region_at(report, index, &region) == CTOOL_FALSE) {
      continue;
    }
    if (address >= region.address &&
        address - region.address < region.contents.size) {
      *map_offset_out = map_base + (address - region.address);
      return CTOOL_TRUE;
    }
    map_base += region.contents.size;
  }
  return CTOOL_FALSE;
}

static ctool_bool dis_exec_address_is_loaded(
    const ctool_dis_report_t *report, ctool_u32 address) {
  ctool_u32 index;
  for (index = 0u; index < dis_exec_region_count(report); index++) {
    ctool_u32 region_address;
    ctool_u32 region_size;
    if (dis_exec_loaded_region_at(report, index, &region_address,
                                  &region_size) == CTOOL_TRUE &&
        address >= region_address &&
        address - region_address < region_size) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t dis_scan_exec_local_target_region(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report, const dis_exec_region_t *region,
    ctool_u32 map_base,
    ctool_u8 *instruction_starts, ctool_bool mark_starts) {
  ctool_u32 offset = 0u;
  while (offset < region->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_status_t status =
        decoder == (const ctool_x86_decoder_t *)0
            ? ctool_x86_decode(job, CTOOL_X86_MODE_32, region->contents,
                               offset, &decoded)
            : ctool_x86_decode_indexed(job, decoder, CTOOL_X86_MODE_32,
                                       region->contents, offset, &decoded);
    if (status != CTOOL_OK) {
      return status;
    }
    if (decoded.kind == CTOOL_X86_DECODE_KNOWN) {
      if (mark_starts == CTOOL_TRUE) {
        dis_instruction_start_set(instruction_starts, map_base + offset);
      } else {
        ctool_u32 operand_index;
        for (operand_index = 0u;
             operand_index < (ctool_u32)decoded.instruction.operand_count;
             operand_index++) {
          const ctool_x86_operand_t *operand =
              &decoded.instruction.operands[operand_index];
          ctool_u32 target;
          ctool_u32 target_map_offset;
          if (operand->kind != CTOOL_X86_OPERAND_RELATIVE ||
              operand->as.value.kind != CTOOL_X86_VALUE_CONSTANT) {
            continue;
          }
          report->decode_summary.direct_relative_target_count++;
          target = dis_relative_target(region->address + offset,
                                       &decoded, operand);
          if (dis_exec_code_map_offset(report, target, &target_map_offset) ==
              CTOOL_FALSE) {
            if (dis_exec_address_is_loaded(report, target) == CTOOL_TRUE) {
              report->decode_summary.direct_relative_data_count++;
            } else {
              report->decode_summary.direct_relative_outside_image_count++;
            }
          } else if (dis_instruction_start_get(
                         instruction_starts, target_map_offset) ==
                     CTOOL_FALSE) {
            report->decode_summary.direct_relative_mid_instruction_count++;
          }
        }
      }
    } else if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u ||
        decoded.consumed > region->contents.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    offset += (ctool_u32)decoded.consumed;
  }
  return CTOOL_OK;
}

static void dis_count_exec_code_anchor(
    const ctool_dis_report_t *report, const ctool_u8 *instruction_starts,
    ctool_u32 address, ctool_dis_decode_summary_t *summary) {
  ctool_u32 map_offset;
  summary->code_anchor_count++;
  if (dis_exec_code_map_offset(report, address, &map_offset) == CTOOL_FALSE) {
    summary->code_anchor_outside_executable_count++;
  } else if (dis_instruction_start_get(instruction_starts, map_offset) ==
             CTOOL_FALSE) {
    summary->code_anchor_mid_instruction_count++;
  }
}

static ctool_status_t dis_prepare_exec_policy_summaries(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_u32 code_size = 0u;
  ctool_u32 bitset_size;
  ctool_u8 *instruction_starts = (ctool_u8 *)0;
  ctool_status_t status =
      dis_exec_code_map_size(job, report, &code_size);
  ctool_status_t rewind_status;
  bitset_size = code_size / 8u;
  if (code_size % 8u != 0u) {
    bitset_size++;
  }
  if (status == CTOOL_OK && bitset_size != 0u) {
    status = ctool_arena_alloc_zero(
        arena, bitset_size, (ctool_u32)sizeof(ctool_u8),
        (ctool_u32)sizeof(ctool_u8), (void **)&instruction_starts);
  }
  if (status == CTOOL_OK) {
    ctool_u32 index;
    ctool_u32 map_base = 0u;
    for (index = 0u;
         status == CTOOL_OK && index < dis_exec_region_count(report);
         index++) {
      dis_exec_region_t region;
      if (dis_exec_code_region_at(report, index, &region) == CTOOL_FALSE) {
        continue;
      }
      status = dis_scan_exec_local_target_region(
          job, decoder, report, &region, map_base, instruction_starts,
          CTOOL_TRUE);
      map_base += region.contents.size;
    }
  }
  if (status == CTOOL_OK &&
      (report->policies & CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u) {
    ctool_u32 index;
    ctool_u32 map_base = 0u;
    for (index = 0u;
         status == CTOOL_OK && index < dis_exec_region_count(report);
         index++) {
      dis_exec_region_t region;
      if (dis_exec_code_region_at(report, index, &region) == CTOOL_FALSE) {
        continue;
      }
      status = dis_scan_exec_local_target_region(
          job, decoder, report, &region, map_base, instruction_starts,
          CTOOL_FALSE);
      map_base += region.contents.size;
    }
  }
  if (status == CTOOL_OK &&
      (report->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u) {
    ctool_u32 index;
    ctool_u32 entry_point = report->input == CTOOL_DIS_INPUT_PE32
                                ? report->pe32.entry_point
                                : report->elf32.entry_point;
    dis_count_exec_code_anchor(report, instruction_starts, entry_point,
                               &report->decode_summary);
    for (index = 0u;
         report->input == CTOOL_DIS_INPUT_ELF32 &&
         index < report->elf32.symbol_count;
         index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION) {
        dis_count_exec_code_anchor(report, instruction_starts, symbol->value,
                                   &report->decode_summary);
      }
    }
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  return status == CTOOL_OK ? rewind_status : status;
}

static ctool_status_t
dis_summarize_region(ctool_job_t *job, const ctool_x86_decoder_t *decoder,
                     ctool_bytes_t bytes,
                     ctool_x86_mode_t mode,
                     const dis_relocation_ownership_t *ownership,
                     ctool_dis_decode_summary_t *summary) {
  ctool_u32 offset = 0u;
  while (offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_status_t status =
        decoder == (const ctool_x86_decoder_t *)0
            ? ctool_x86_decode(job, mode, bytes, offset, &decoded)
            : ctool_x86_decode_indexed(job, decoder, mode, bytes, offset,
                                       &decoded);
    if (status != CTOOL_OK) {
      return status;
    }
    switch (decoded.kind) {
    case CTOOL_X86_DECODE_KNOWN:
      if (ownership != (const dis_relocation_ownership_t *)0 &&
          ownership->relocation_claimed != (ctool_u8 *)0) {
        ctool_u32 field_index;
        for (field_index = 0u; field_index < decoded.encoding.field_count;
             field_index++) {
          ctool_u32 relocation_index = dis_find_field_relocation(
              ownership->report, ownership->section_file_index, 0u, offset,
              &decoded.encoding.fields[field_index],
              ownership->relocation_claimed);
          if (relocation_index != DIS_U32_MAX) {
            ownership->relocation_claimed[relocation_index] = 1u;
          }
        }
      }
      summary->known_count++;
      break;
    case CTOOL_X86_DECODE_UNKNOWN:
      summary->unknown_count++;
      break;
    case CTOOL_X86_DECODE_INVALID:
      summary->invalid_count++;
      break;
    case CTOOL_X86_DECODE_TRUNCATED:
      summary->truncated_count++;
      decoded.consumed = decoded.encoding.size;
      break;
    default:
      return CTOOL_ERR_INTERNAL;
    }
    if (decoded.consumed == 0u || decoded.consumed > bytes.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    offset += (ctool_u32)decoded.consumed;
  }
  return CTOOL_OK;
}

static ctool_status_t dis_prepare_decode_summary(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_dis_report_t *report) {
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  if ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) {
    return CTOOL_OK;
  }
  if (report->input == CTOOL_DIS_INPUT_RAW) {
    if (report->mode != CTOOL_DIS_RAW_RANGE_MAP) {
      return dis_summarize_region(job, decoder, report->source->contents,
                                  report->mode,
                                  (const dis_relocation_ownership_t *)0,
                                  &report->decode_summary);
    }
    for (index = 0u; status == CTOOL_OK && index < report->raw_range_count;
         index++) {
      ctool_u32 first = report->raw_ranges[index].offset;
      ctool_u32 last = index + 1u < report->raw_range_count
                           ? report->raw_ranges[index + 1u].offset
                           : report->source->contents.size;
      if (report->raw_ranges[index].kind == CTOOL_DIS_RAW_RANGE_DATA) {
        continue;
      }
      status = dis_summarize_region(
          job, decoder,
          ctool_bytes(report->source->contents.data + first, last - first),
          dis_raw_range_mode(report->raw_ranges[index].kind),
          (const dis_relocation_ownership_t *)0,
          &report->decode_summary);
    }
    return status;
  }
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    for (index = 0u;
         status == CTOOL_OK && index < dis_exec_region_count(report);
         index++) {
      dis_exec_region_t region;
      if (dis_exec_code_region_at(report, index, &region) == CTOOL_FALSE) {
        continue;
      }
      status = dis_summarize_region(
          job, decoder, region.contents, report->mode,
          (const dis_relocation_ownership_t *)0,
          &report->decode_summary);
    }
    return status;
  }
  if (report->elf32.file_type == CTOOL_ELF32_ET_EXEC) {
    for (index = 0u;
         status == CTOOL_OK && index < report->elf32.program_header_count;
         index++) {
      const ctool_elf32_program_header_t *program =
          &report->elf32.program_headers[index];
      if (program->type != CTOOL_ELF32_PT_LOAD ||
          (program->flags & CTOOL_ELF32_PF_X) == 0u ||
          program->contents.size == 0u) {
        continue;
      }
      status = dis_summarize_region(job, decoder, program->contents,
                                    report->mode,
                                    (const dis_relocation_ownership_t *)0,
                                    &report->decode_summary);
    }
    return status;
  }
  {
    ctool_arena_t *arena = ctool_job_arena(job);
    ctool_arena_mark_t mark = ctool_arena_mark(arena);
    dis_relocation_ownership_t ownership;
    ctool_status_t rewind_status;
    ownership.report = report;
    ownership.section_file_index = 0u;
    ownership.relocation_claimed = (ctool_u8 *)0;
    if (report->elf32.relocation_count != 0u) {
      status = ctool_arena_alloc_zero(
          arena, report->elf32.relocation_count, (ctool_u32)sizeof(ctool_u8),
          (ctool_u32)sizeof(ctool_u8),
          (void **)&ownership.relocation_claimed);
    }
    for (index = 0u;
         status == CTOOL_OK && index < report->elf32.relocation_count;
         index++) {
      const ctool_elf32_relocation_t *relocation =
          &report->elf32.relocations[index];
      const ctool_elf32_section_t *target =
          &report->elf32.sections[relocation->target_section_file_index];
      if (target->type == CTOOL_ELF32_SHT_PROGBITS &&
          (target->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u) {
        report->decode_summary.executable_relocation_count++;
      }
    }
    for (index = 0u; status == CTOOL_OK && index < report->elf32.section_count;
         index++) {
      const ctool_elf32_section_t *section = &report->elf32.sections[index];
      if (section->type != CTOOL_ELF32_SHT_PROGBITS ||
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
          section->contents.size == 0u) {
        continue;
      }
      ownership.section_file_index = index;
      status = dis_summarize_region(job, decoder, section->contents,
                                    report->mode, &ownership,
                                    &report->decode_summary);
    }
    if (status == CTOOL_OK) {
      ctool_u64 matched = 0u;
      for (index = 0u;
           ownership.relocation_claimed != (ctool_u8 *)0 &&
           index < report->elf32.relocation_count;
           index++) {
        if (ownership.relocation_claimed[index] != 0u) {
          matched++;
        }
      }
      if (matched > report->decode_summary.executable_relocation_count) {
        status = CTOOL_ERR_INTERNAL;
      } else {
        report->decode_summary.unmatched_executable_relocation_count =
            report->decode_summary.executable_relocation_count - matched;
      }
    }
    rewind_status = ctool_arena_rewind(arena, mark);
    if (status == CTOOL_OK) {
      status = rewind_status;
    }
  }
  return status;
}

static ctool_status_t dis_inspect(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    const ctool_source_t *source, const ctool_dis_request_t *request,
    ctool_dis_report_t *report_out) {
  ctool_dis_request_t normalized_request;
  ctool_status_t status;
  ctool_u32 index;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  if (report_out != (ctool_dis_report_t *)0) {
    dis_zero_report(report_out);
  }
  if (job == (ctool_job_t *)0 || source == (const ctool_source_t *)0 ||
      request == (const ctool_dis_request_t *)0 ||
      report_out == (ctool_dis_report_t *)0 ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u) ||
      (source->path.text.data == (const char *)0 &&
       source->path.text.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (request->views == 0u ||
      (request->views & ~CTOOL_DIS_VIEW_ALL) != 0u) {
    return dis_bad_request(job, source, "CupidDis view selection is invalid");
  }
  if ((request->policies & ~CTOOL_DIS_POLICY_ALL) != 0u) {
    return dis_bad_request(job, source, "CupidDis policy selection is invalid");
  }
  if (request->views == CTOOL_DIS_VIEW_ALL &&
      request->input != CTOOL_DIS_INPUT_RAW) {
    normalized_request = *request;
    normalized_request.views =
        request->input == CTOOL_DIS_INPUT_PE32
            ? CTOOL_DIS_VIEW_HEADER | CTOOL_DIS_VIEW_SECTIONS |
                  CTOOL_DIS_VIEW_IMPORTS | CTOOL_DIS_VIEW_DISASSEMBLY
            : CTOOL_DIS_VIEW_HEADER | CTOOL_DIS_VIEW_SECTIONS |
                  CTOOL_DIS_VIEW_SYMBOLS | CTOOL_DIS_VIEW_RELOCATIONS |
                  CTOOL_DIS_VIEW_DISASSEMBLY;
    request = &normalized_request;
  }
  if (request->input == CTOOL_DIS_INPUT_RAW) {
    dis_raw_map_issue_t map_issue = DIS_RAW_MAP_VALID;
    const char *edge_issue;
    ctool_bool has_code16 = CTOOL_FALSE;
    if ((request->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u) {
      return dis_bad_request(
          job, source,
          "code anchor checks require ELF32 ET_REL or ET_EXEC input");
    }
    if (request->views != CTOOL_DIS_VIEW_DISASSEMBLY) {
      return dis_bad_request(job, source,
                             "raw input only supports disassembly");
    }
    if (request->raw_mode == CTOOL_DIS_RAW_RANGE_MAP) {
      map_issue = dis_raw_map_issue(source->contents.size,
                                    request->raw_ranges,
                                    request->raw_range_count);
      if (map_issue != DIS_RAW_MAP_VALID) {
        return dis_bad_request(job, source, dis_raw_map_message(map_issue));
      }
    } else if (dis_x86_mode_valid(request->raw_mode) == CTOOL_FALSE) {
      return dis_bad_request(job, source,
                             "raw input requires 16-bit or 32-bit mode");
    } else if (request->raw_ranges !=
                   (const ctool_dis_raw_range_t *)0 ||
               request->raw_range_count != 0u) {
      return dis_bad_request(job, source, "raw ranges require mapped mode");
    }
    edge_issue = dis_raw_edge_issue(source, request);
    if (edge_issue != (const char *)0) {
      return dis_bad_request(job, source, edge_issue);
    }
    if (request->raw_mode == CTOOL_X86_MODE_16) {
      has_code16 = CTOOL_TRUE;
    } else if (request->raw_mode == CTOOL_DIS_RAW_RANGE_MAP) {
      for (index = 0u; index < request->raw_range_count; index++) {
        if (request->raw_ranges[index].kind ==
            CTOOL_DIS_RAW_RANGE_CODE16) {
          has_code16 = CTOOL_TRUE;
        }
      }
    }
    if ((request->policies & CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u &&
        has_code16 == CTOOL_TRUE && source->contents.size > 65536u) {
      return dis_bad_request(
          job, source,
          "local target checks require code16 raw input at most 65536 bytes");
    }
    if (request->label_count != 0u &&
        request->labels == (const ctool_dis_label_t *)0) {
      return dis_bad_request(job, source, "raw label storage is missing");
    }
    for (index = 0u; index < request->label_count; index++) {
      if (request->labels[index].name.data == (const char *)0 &&
          request->labels[index].name.size != 0u) {
        return dis_bad_request(job, source, "raw label name is invalid");
      }
    }
    if (source->contents.size != 0u &&
        request->raw_base_address >
            DIS_U32_MAX - (source->contents.size - 1u)) {
      return dis_emit(job, source->path.text,
                      CTOOL_DIS_DIAG_ADDRESS_OVERFLOW, 0u,
                      "raw disassembly address range overflows",
                      CTOOL_ERR_OVERFLOW);
    }
    report_out->source = source;
    report_out->input = request->input;
    report_out->views = request->views;
    report_out->policies = request->policies;
    report_out->mode = request->raw_mode;
    report_out->base_address = request->raw_base_address;
    if (request->raw_mode == CTOOL_DIS_RAW_RANGE_MAP) {
      report_out->raw_ranges = request->raw_ranges;
      report_out->raw_range_count = request->raw_range_count;
    }
    report_out->raw_edges = request->raw_edges;
    report_out->raw_edge_count = request->raw_edge_count;
    report_out->raw_edge_metadata_present =
        request->raw_edge_metadata_present;
    report_out->labels = request->labels;
    report_out->label_count = request->label_count;
    arena = ctool_job_arena(job);
    mark = ctool_arena_mark(arena);
    status = dis_prepare_raw_label_order(job, report_out);
    if (status == CTOOL_OK) {
      status = dis_prepare_decode_summary(job, decoder, report_out);
    }
    if (status == CTOOL_OK &&
        (report_out->policies &
         CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u) {
      status =
          dis_prepare_raw_local_target_summary(job, decoder, report_out);
    }
    if (status == CTOOL_OK &&
        (report_out->policies &
         CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES) != 0u) {
      status =
          dis_prepare_raw_source_edge_summary(job, decoder, report_out);
    }
    if (status != CTOOL_OK) {
      (void)ctool_arena_rewind(arena, mark);
      dis_zero_report(report_out);
    }
    return status;
  }
  if (request->input == CTOOL_DIS_INPUT_PE32) {
    if ((request->views &
         (CTOOL_DIS_VIEW_SYMBOLS | CTOOL_DIS_VIEW_RELOCATIONS)) != 0u) {
      return dis_bad_request(
          job, source,
          "PE32 input does not carry CupidDis symbols or relocations");
    }
    if ((request->policies &
         CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u &&
        (request->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) {
      return dis_bad_request(
          job, source,
          "PE32 local target checks require the disassembly view");
    }
    if ((request->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u &&
        (request->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) {
      return dis_bad_request(
          job, source,
          "PE32 code anchor checks require the disassembly view");
    }
    if (request->label_count != 0u ||
        request->labels != (const ctool_dis_label_t *)0) {
      return dis_bad_request(job, source,
                             "PE32 input cannot carry raw labels");
    }
    arena = ctool_job_arena(job);
    mark = ctool_arena_mark(arena);
    status = ctool_pe32_read(job, source, &report_out->pe32);
    if (status != CTOOL_OK) {
      dis_zero_report(report_out);
      return status;
    }
    report_out->source = source;
    report_out->input = request->input;
    report_out->views = request->views;
    report_out->policies = request->policies;
    report_out->mode = CTOOL_X86_MODE_32;
    status = dis_prepare_decode_summary(job, decoder, report_out);
    if (status == CTOOL_OK &&
        (report_out->policies &
         (CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
          CTOOL_DIS_POLICY_CODE_ANCHORS)) != 0u) {
      status = dis_prepare_exec_policy_summaries(job, decoder, report_out);
    }
    if (status != CTOOL_OK) {
      (void)ctool_arena_rewind(arena, mark);
      dis_zero_report(report_out);
    }
    return status;
  }
  if (request->input != CTOOL_DIS_INPUT_ELF32) {
    return dis_bad_request(job, source, "CupidDis input kind is invalid");
  }
  if ((request->policies & CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES) != 0u ||
      request->raw_edge_metadata_present == CTOOL_TRUE ||
      request->raw_edges != (const ctool_dis_raw_edge_t *)0 ||
      request->raw_edge_count != 0u) {
    return dis_bad_request(
        job, source,
        "source control-edge checks require raw range-map input");
  }
  if ((request->views & CTOOL_DIS_VIEW_IMPORTS) != 0u) {
    return dis_bad_request(job, source,
                           "ELF32 input does not carry PE32 imports");
  }
  if ((request->policies &
       CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u &&
      (request->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) {
    return dis_bad_request(
        job, source,
        "ELF local target checks require the disassembly view");
  }
  if ((request->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u &&
      (request->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) {
    return dis_bad_request(
        job, source,
        "ELF code anchor checks require the disassembly view");
  }
  if (request->label_count != 0u ||
      request->labels != (const ctool_dis_label_t *)0) {
    return dis_bad_request(job, source,
                           "ELF input cannot carry raw labels");
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  status = ctool_elf32_read(job, source, &report_out->elf32);
  if (status != CTOOL_OK) {
    dis_zero_report(report_out);
    return status;
  }
  if ((request->policies &
       CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u &&
      report_out->elf32.file_type == CTOOL_ELF32_ET_EXEC &&
      dis_exec_programs_are_static(&report_out->elf32) == CTOOL_FALSE) {
    (void)ctool_arena_rewind(arena, mark);
    dis_zero_report(report_out);
    return dis_bad_request(
        job, source,
        "executable local target checks require a static image without "
        "PT_DYNAMIC or PT_INTERP");
  }
  if ((request->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u &&
      report_out->elf32.file_type == CTOOL_ELF32_ET_EXEC &&
      dis_exec_programs_are_static(&report_out->elf32) == CTOOL_FALSE) {
    (void)ctool_arena_rewind(arena, mark);
    dis_zero_report(report_out);
    return dis_bad_request(
        job, source,
        "executable code anchor checks require a static image without "
        "PT_DYNAMIC or PT_INTERP");
  }
  if ((request->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report_out->elf32.section_count; index++) {
      const ctool_elf32_section_t *section =
          &report_out->elf32.sections[index];
      if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u &&
          section->contents.size != 0u &&
          section->address > DIS_U32_MAX - (section->contents.size - 1u)) {
        (void)ctool_arena_rewind(arena, mark);
        dis_zero_report(report_out);
        return dis_emit(job, source->path.text,
                        CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                        section->file_offset,
                        "ELF disassembly address range overflows",
                        CTOOL_ERR_OVERFLOW);
      }
    }
  }
  if (report_out->elf32.file_type == CTOOL_ELF32_ET_REL &&
      (request->views &
       (CTOOL_DIS_VIEW_SYMBOLS | CTOOL_DIS_VIEW_DISASSEMBLY)) != 0u) {
    for (index = 0u; index < report_out->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol =
          &report_out->elf32.symbols[index];
      const ctool_elf32_section_t *section;
      if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
          ((request->views & CTOOL_DIS_VIEW_SYMBOLS) == 0u &&
           symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION)) {
        continue;
      }
      section = symbol->section_file_index < report_out->elf32.section_count
                    ? &report_out->elf32
                           .sections[symbol->section_file_index]
                    : (const ctool_elf32_section_t *)0;
      if (section != (const ctool_elf32_section_t *)0 &&
          section->address > DIS_U32_MAX - symbol->value) {
        (void)ctool_arena_rewind(arena, mark);
        dis_zero_report(report_out);
        return dis_emit(job, source->path.text,
                        CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                        symbol->file_index,
                        "ELF symbol address overflows", CTOOL_ERR_OVERFLOW);
      }
    }
  }
  report_out->source = source;
  report_out->input = request->input;
  report_out->views = request->views;
  report_out->policies = request->policies;
  report_out->mode = CTOOL_X86_MODE_32;
  status = dis_prepare_report_orders(job, report_out);
  if (status == CTOOL_OK) {
    status = dis_prepare_decode_summary(job, decoder, report_out);
  }
  if (status == CTOOL_OK &&
      report_out->elf32.file_type == CTOOL_ELF32_ET_REL &&
      (report_out->policies &
       (CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
        CTOOL_DIS_POLICY_CODE_ANCHORS)) != 0u) {
    status = dis_prepare_rel_policy_summaries(job, decoder, report_out);
  }
  if (status == CTOOL_OK &&
      report_out->elf32.file_type == CTOOL_ELF32_ET_EXEC &&
      (report_out->policies &
       (CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
        CTOOL_DIS_POLICY_CODE_ANCHORS)) != 0u) {
    status = dis_prepare_exec_policy_summaries(job, decoder, report_out);
  }
  if (status != CTOOL_OK) {
    (void)ctool_arena_rewind(arena, mark);
    dis_zero_report(report_out);
  }
  return status;
}

ctool_status_t ctool_dis_inspect(ctool_job_t *job,
                                  const ctool_source_t *source,
                                  const ctool_dis_request_t *request,
                                  ctool_dis_report_t *report_out) {
  return dis_inspect(job, (const ctool_x86_decoder_t *)0, source, request,
                     report_out);
}

ctool_status_t ctool_dis_inspect_indexed(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    const ctool_source_t *source, const ctool_dis_request_t *request,
    ctool_dis_report_t *report_out) {
  if (decoder == (const ctool_x86_decoder_t *)0) {
    if (report_out != (ctool_dis_report_t *)0) {
      dis_zero_report(report_out);
    }
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  return dis_inspect(job, decoder, source, request, report_out);
}

static ctool_status_t dis_write(ctool_text_sink_t output, const void *data,
                                ctool_u32 size) {
  if (size == 0u) {
    return CTOOL_OK;
  }
  return output.write(output.context, ctool_bytes(data, size));
}

static ctool_status_t dis_literal(ctool_text_sink_t output,
                                  const char *text) {
  ctool_string_t value = ctool_string(text);
  return dis_write(output, value.data, value.size);
}

static ctool_status_t dis_string(ctool_text_sink_t output,
                                 ctool_string_t text) {
  return dis_write(output, text.data, text.size);
}

static ctool_u32 dis_decimal_chars(char *output, ctool_u32 value) {
  char reverse[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    output[index] = reverse[count - index - 1u];
  }
  return count;
}

static ctool_status_t dis_decimal(ctool_text_sink_t output, ctool_u32 value) {
  char text[10];
  ctool_u32 count = dis_decimal_chars(text, value);
  return dis_write(output, text, count);
}

static ctool_status_t dis_hex_fixed(ctool_text_sink_t output, ctool_u32 value,
                                    ctool_u32 digits) {
  static const char hex[] = "0123456789ABCDEF";
  char text[8];
  ctool_u32 index;
  for (index = 0u; index < digits; index++) {
    ctool_u32 shift = (digits - index - 1u) * 4u;
    text[index] = hex[(value >> shift) & 0x0fu];
  }
  return dis_write(output, text, digits);
}

static ctool_status_t dis_hex_compact(ctool_text_sink_t output,
                                      ctool_u32 value) {
  ctool_u32 digits = 1u;
  ctool_u32 probe = value;
  while (probe > 0x0fu) {
    digits++;
    probe >>= 4u;
  }
  return dis_hex_fixed(output, value, digits);
}

static ctool_status_t dis_hex_u32(ctool_text_sink_t output, ctool_u32 value) {
  ctool_status_t status = dis_literal(output, "0x");
  return status == CTOOL_OK ? dis_hex_fixed(output, value, 8u) : status;
}

static ctool_status_t dis_hex_value(ctool_text_sink_t output,
                                    ctool_u32 value) {
  ctool_status_t status = dis_literal(output, "0x");
  return status == CTOOL_OK ? dis_hex_compact(output, value) : status;
}

static ctool_status_t dis_space(ctool_text_sink_t output) {
  return dis_literal(output, " ");
}

static const char *dis_file_type_name(ctool_u32 file_type) {
  if (file_type == (ctool_u32)CTOOL_ELF32_ET_REL) {
    return "REL";
  }
  if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
    return "EXEC";
  }
  return "UNKNOWN";
}

static const char *dis_section_type_name(ctool_u32 type) {
  switch (type) {
  case DIS_ELF32_SHT_NULL:
    return "NULL";
  case CTOOL_ELF32_SHT_PROGBITS:
    return "PROGBITS";
  case DIS_ELF32_SHT_SYMTAB:
    return "SYMTAB";
  case DIS_ELF32_SHT_STRTAB:
    return "STRTAB";
  case CTOOL_ELF32_SHT_NOBITS:
    return "NOBITS";
  case DIS_ELF32_SHT_REL:
    return "REL";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_program_type_name(ctool_u32 type) {
  if (type == CTOOL_ELF32_PT_LOAD) {
    return "LOAD";
  }
  if (type == CTOOL_ELF32_PT_TLS) {
    return "TLS";
  }
  if (type == DIS_ELF32_PT_GNU_STACK) {
    return "GNU_STACK";
  }
  return "UNKNOWN";
}

static const char *dis_binding_name(ctool_u32 binding) {
  switch (binding) {
  case CTOOL_ELF32_BIND_LOCAL:
    return "LOCAL";
  case CTOOL_ELF32_BIND_GLOBAL:
    return "GLOBAL";
  case CTOOL_ELF32_BIND_WEAK:
    return "WEAK";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_symbol_type_name(ctool_u32 type) {
  switch (type) {
  case CTOOL_ELF32_SYMBOL_NOTYPE:
    return "NOTYPE";
  case CTOOL_ELF32_SYMBOL_OBJECT:
    return "OBJECT";
  case CTOOL_ELF32_SYMBOL_FUNCTION:
    return "FUNC";
  case CTOOL_ELF32_SYMBOL_SECTION:
    return "SECTION";
  case CTOOL_ELF32_SYMBOL_FILE:
    return "FILE";
  case CTOOL_ELF32_SYMBOL_COMMON:
    return "COMMON";
  case CTOOL_ELF32_SYMBOL_TLS:
    return "TLS";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_visibility_name(ctool_u32 visibility) {
  switch (visibility) {
  case CTOOL_ELF32_VIS_DEFAULT:
    return "DEFAULT";
  case CTOOL_ELF32_VIS_INTERNAL:
    return "INTERNAL";
  case CTOOL_ELF32_VIS_HIDDEN:
    return "HIDDEN";
  case CTOOL_ELF32_VIS_PROTECTED:
    return "PROTECTED";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_relocation_name(ctool_u32 type) {
  if (type == CTOOL_ELF32_R_386_32) {
    return "R_386_32";
  }
  if (type == CTOOL_ELF32_R_386_PC32) {
    return "R_386_PC32";
  }
  return "R_386_UNKNOWN";
}

static const ctool_elf32_section_t *dis_section(
    const ctool_elf32_object_t *object, ctool_u32 file_index) {
  if (file_index >= object->section_count) {
    return (const ctool_elf32_section_t *)0;
  }
  return &object->sections[file_index];
}

static const ctool_elf32_symbol_t *dis_symbol(
    const ctool_elf32_object_t *object, ctool_u32 file_index) {
  if (file_index >= object->symbol_count) {
    return (const ctool_elf32_symbol_t *)0;
  }
  return &object->symbols[file_index];
}

static ctool_status_t dis_render_pe32_header(
    const ctool_dis_report_t *report, ctool_text_sink_t output) {
  const ctool_pe32_image_t *image = &report->pe32;
  ctool_status_t status = dis_literal(output, "PE32 i386 entry=");
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, image->entry_point);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " image-base=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, image->image_base);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " sections=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, image->section_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " imports=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, image->import_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " libraries=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, image->import_library_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " image-size=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, image->image_size);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " headers-size=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, image->headers_size);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "\n");
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_literal(output, "[directories]\nimport rva=");
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_hex_u32(output, image->import_directory_rva);
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_literal(output, " size=");
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_hex_u32(output, image->import_directory_size);
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_literal(output, "\niat rva=");
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_hex_u32(output, image->iat_directory_rva);
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_literal(output, " size=");
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_hex_u32(output, image->iat_directory_size);
  }
  if (status == CTOOL_OK && image->import_count != 0u) {
    status = dis_literal(output, "\n");
  }
  return status;
}

static ctool_status_t dis_render_header(const ctool_dis_report_t *report,
                                        ctool_text_sink_t output) {
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    return dis_render_pe32_header(report, output);
  }
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_status_t status = dis_literal(output, "ELF32 ");
  if (status == CTOOL_OK) {
    status = dis_literal(output, dis_file_type_name((ctool_u32)object->file_type));
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " i386 entry=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, object->entry_point);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " flags=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, object->flags);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " phnum=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, object->program_header_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " shnum=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, object->section_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "\n");
  }
  if (status == CTOOL_OK && object->program_header_count != 0u) {
    ctool_u32 index;
    status = dis_literal(output, "[program headers]\n");
    for (index = 0u; status == CTOOL_OK &&
                     index < object->program_header_count;
         index++) {
      const ctool_elf32_program_header_t *header =
          &object->program_headers[index];
      status = dis_literal(output, "[");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, header->file_index);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "] ");
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, dis_program_type_name(header->type));
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " off=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->file_offset);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " vaddr=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->virtual_address);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " filesz=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->file_size);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " memsz=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->memory_size);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " flags=");
      }
      if (status == CTOOL_OK && (header->flags & 4u) != 0u) {
        status = dis_literal(output, "R");
      }
      if (status == CTOOL_OK && (header->flags & 2u) != 0u) {
        status = dis_literal(output, "W");
      }
      if (status == CTOOL_OK && (header->flags & 1u) != 0u) {
        status = dis_literal(output, "X");
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " align=");
      }
      if (status == CTOOL_OK) {
        status = dis_decimal(output, header->alignment);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "\n");
      }
    }
  }
  return status;
}

static ctool_status_t dis_render_pe32_sections(
    const ctool_dis_report_t *report, ctool_text_sink_t output) {
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[sections]\n");
  for (index = 0u;
       status == CTOOL_OK && index < report->pe32.section_count; index++) {
    const ctool_pe32_section_t *section = &report->pe32.sections[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, section->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " flags=");
    }
    if (status == CTOOL_OK &&
        (section->characteristics & CTOOL_PE32_SCN_READ) != 0u) {
      status = dis_literal(output, "R");
    }
    if (status == CTOOL_OK &&
        (section->characteristics & CTOOL_PE32_SCN_WRITE) != 0u) {
      status = dis_literal(output, "W");
    }
    if (status == CTOOL_OK &&
        (section->characteristics & CTOOL_PE32_SCN_EXECUTE) != 0u) {
      status = dis_literal(output, "X");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " rva=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->virtual_address);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " vaddr=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, report->pe32.image_base +
                                      section->virtual_address);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " vsize=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->virtual_size);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " off=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->file_offset);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " rawsize=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->file_size);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_sections(const ctool_dis_report_t *report,
                                          ctool_text_sink_t output) {
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    return dis_render_pe32_sections(report, output);
  }
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[sections]\n");
  for (index = 0u; status == CTOOL_OK && index < object->section_count;
       index++) {
    const ctool_elf32_section_t *section = &object->sections[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, section->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = section->name.size == 0u ? dis_literal(output, "<null>")
                                        : dis_string(output, section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " type=");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_section_type_name(section->type));
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " flags=");
    }
    if (status == CTOOL_OK && (section->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      status = dis_literal(output, "W");
    }
    if (status == CTOOL_OK && (section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u) {
      status = dis_literal(output, "A");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u) {
      status = dis_literal(output, "X");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_MERGE) != 0u) {
      status = dis_literal(output, "M");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_STRINGS) != 0u) {
      status = dis_literal(output, "S");
    }
    if (status == CTOOL_OK && section->flags == 0u) {
      status = dis_literal(output, "-");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " addr=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->address);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " off=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->file_offset);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " size=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->size);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " align=");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, section->alignment);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_imports(const ctool_dis_report_t *report,
                                         ctool_text_sink_t output) {
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[imports]\n");
  for (index = 0u;
       status == CTOOL_OK && index < report->pe32.import_count; index++) {
    const ctool_pe32_import_t *import = &report->pe32.imports[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, import->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, import->library_name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "!");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, import->procedure_name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " lookup=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, import->lookup_rva);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " iat=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, import->iat_rva);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_symbol_section(
    const ctool_elf32_object_t *object, const ctool_elf32_symbol_t *symbol,
    ctool_text_sink_t output) {
  const ctool_elf32_section_t *section;
  switch (symbol->placement) {
  case CTOOL_ELF32_SYMBOL_UNDEFINED:
    return dis_literal(output, "UND");
  case CTOOL_ELF32_SYMBOL_ABSOLUTE:
    return dis_literal(output, "ABS");
  case CTOOL_ELF32_SYMBOL_COMMON_STORAGE:
    return dis_literal(output, "COMMON");
  case CTOOL_ELF32_SYMBOL_RESERVED:
    return dis_literal(output, "RESERVED");
  case CTOOL_ELF32_SYMBOL_DEFINED:
    section = dis_section(object, symbol->section_file_index);
    if (section == (const ctool_elf32_section_t *)0) {
      return dis_literal(output, "<bad-section>");
    }
    if (section->name.size == 0u) {
      return dis_literal(output, "<null>");
    }
    return dis_string(output, section->name);
  default:
    return dis_literal(output, "UNKNOWN");
  }
}

static ctool_status_t dis_render_symbols(const ctool_dis_report_t *report,
                                         ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[symbols]\n");
  for (index = 0u; status == CTOOL_OK && index < object->symbol_count;
       index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, symbol->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, symbol->value);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " size=");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, symbol->size);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_binding_name(symbol->binding));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_symbol_type_name(symbol->type));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_visibility_name(symbol->visibility));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_section(object, symbol, output);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = symbol->name.size == 0u ? dis_literal(output, "<anonymous>")
                                       : dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_symbol_addend(
    ctool_text_sink_t output, const ctool_elf32_object_t *object,
    const ctool_elf32_symbol_t *symbol, ctool_bool addend_known,
    ctool_i32 addend) {
  ctool_status_t status;
  if (symbol == (const ctool_elf32_symbol_t *)0) {
    status = dis_literal(output, "<symbol>");
  } else if (symbol->name.size != 0u) {
    status = dis_string(output, symbol->name);
  } else if (symbol->type == CTOOL_ELF32_SYMBOL_SECTION &&
             symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
    const ctool_elf32_section_t *section =
        dis_section(object, symbol->section_file_index);
    status = section == (const ctool_elf32_section_t *)0 ||
                     section->name.size == 0u
                 ? dis_literal(output, "<section>")
                 : dis_string(output, section->name);
  } else {
    status = dis_literal(output, "<symbol>");
  }
  if (status != CTOOL_OK || addend_known == CTOOL_FALSE || addend == 0) {
    return status;
  }
  status = dis_literal(output, addend < 0 ? "-" : "+");
  if (status != CTOOL_OK) {
    return status;
  }
  if (addend < 0) {
    ctool_u32 magnitude = (ctool_u32)(-(addend + 1)) + 1u;
    return dis_decimal(output, magnitude);
  }
  return dis_decimal(output, (ctool_u32)addend);
}

static ctool_status_t dis_render_relocations(const ctool_dis_report_t *report,
                                             ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[relocations]\n");
  for (index = 0u;
       status == CTOOL_OK && index < report->relocation_order_count;
       index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[report->relocation_order[index]];
    const ctool_elf32_section_t *relocation_section =
        dis_section(object, relocation->relocation_section_file_index);
    const ctool_elf32_section_t *target =
        dis_section(object, relocation->target_section_file_index);
    const ctool_elf32_symbol_t *symbol_value =
        dis_symbol(object, relocation->symbol_file_index);
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = relocation_section == (const ctool_elf32_section_t *)0 ||
                       relocation_section->name.size == 0u
                   ? dis_literal(output, "<rel>")
                   : dis_string(output, relocation_section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, relocation->entry_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = target == (const ctool_elf32_section_t *)0 ||
                       target->name.size == 0u
                   ? dis_literal(output, "<target>")
                   : dis_string(output, target->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, relocation->offset);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_relocation_name(relocation->type));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_addend(output, object, symbol_value,
                                        relocation->addend_known,
                                        relocation->addend);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_register(ctool_text_sink_t output,
                                          ctool_x86_reg_t reg_value) {
  ctool_string_t name = ctool_x86_register_name(reg_value);
  return name.size == 0u ? dis_literal(output, "<reg>")
                         : dis_string(output, name);
}

static const char *dis_memory_width(ctool_u16 width_bits) {
  switch (width_bits) {
  case 8u:
    return "byte ";
  case 16u:
    return "word ";
  case 32u:
    return "dword ";
  case 64u:
    return "qword ";
  case 80u:
    return "tword ";
  case 128u:
    return "oword ";
  default:
    return "";
  }
}

static ctool_status_t dis_render_memory(
    ctool_text_sink_t output, const ctool_x86_operand_t *operand,
    const ctool_elf32_object_t *object,
    const ctool_elf32_relocation_t *relocation) {
  const ctool_x86_memory_t *memory = &operand->as.memory;
  ctool_bool have_term = CTOOL_FALSE;
  ctool_status_t status = dis_literal(output,
                                      dis_memory_width(operand->width_bits));
  if (status == CTOOL_OK) {
    status = dis_literal(output, "[");
  }
  if (status == CTOOL_OK && memory->segment.class_id != CTOOL_X86_REG_NONE) {
    status = dis_render_register(output, memory->segment);
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
  }
  if (status == CTOOL_OK && memory->base.class_id != CTOOL_X86_REG_NONE) {
    status = dis_render_register(output, memory->base);
    have_term = CTOOL_TRUE;
  }
  if (status == CTOOL_OK && memory->index.class_id != CTOOL_X86_REG_NONE) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_render_register(output, memory->index);
    }
    if (status == CTOOL_OK && memory->scale > 1u) {
      status = dis_literal(output, "*");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, (ctool_u32)memory->scale);
      }
    }
    have_term = CTOOL_TRUE;
  }
  if (status == CTOOL_OK &&
      relocation != (const ctool_elf32_relocation_t *)0) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_addend(
          output, object, dis_symbol(object, relocation->symbol_file_index),
          relocation->addend_known, relocation->addend);
    }
    have_term = CTOOL_TRUE;
  } else if (status == CTOOL_OK && memory->displacement.kind ==
                                CTOOL_X86_VALUE_REFERENCE) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "<reference>");
    }
    have_term = CTOOL_TRUE;
  } else if (status == CTOOL_OK &&
             (memory->displacement_bits != 0u || have_term == CTOOL_FALSE)) {
    if (have_term == CTOOL_FALSE) {
      status = dis_hex_value(output, memory->displacement.bits);
    } else {
      ctool_i32 displacement = dis_signed_bits(memory->displacement.bits);
      status = dis_literal(output, displacement < 0 ? "-" : "+");
      if (status == CTOOL_OK) {
        ctool_u32 magnitude = displacement < 0
                                  ? (ctool_u32)(-(displacement + 1)) + 1u
                                  : (ctool_u32)displacement;
        status = dis_hex_value(output, magnitude);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "]");
  }
  return status;
}

static const ctool_elf32_relocation_t *dis_operand_relocation(
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index, ctool_u32 logical_address,
    ctool_u32 instruction_offset, const ctool_x86_decoded_t *decoded,
    ctool_u32 operand_index) {
  ctool_u32 field_index;
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      dis_section(object, section_file_index) ==
          (const ctool_elf32_section_t *)0) {
    return (const ctool_elf32_relocation_t *)0;
  }
  for (field_index = 0u; field_index < decoded->encoding.field_count;
       field_index++) {
    const ctool_x86_field_t *field = &decoded->encoding.fields[field_index];
    ctool_u32 relocation_index;
    if ((ctool_u32)field->operand_index != operand_index) {
      continue;
    }
    relocation_index = dis_find_field_relocation(
        report, section_file_index, logical_address, instruction_offset, field,
        (const ctool_u8 *)0);
    if (relocation_index != DIS_U32_MAX) {
      return &object->relocations[relocation_index];
    }
  }
  return (const ctool_elf32_relocation_t *)0;
}

static ctool_status_t dis_render_operand(
    ctool_text_sink_t output, const ctool_x86_operand_t *operand,
    ctool_u32 logical_address,
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index, ctool_u32 instruction_offset,
    const ctool_x86_decoded_t *decoded, ctool_u32 operand_index) {
  const ctool_elf32_relocation_t *relocation =
      object == (const ctool_elf32_object_t *)0
          ? (const ctool_elf32_relocation_t *)0
          : dis_operand_relocation(report, object, section_file_index,
                                   logical_address, instruction_offset,
                                   decoded, operand_index);
  if (relocation != (const ctool_elf32_relocation_t *)0 &&
      operand->kind != CTOOL_X86_OPERAND_MEMORY) {
    return dis_render_symbol_addend(
        output, object, dis_symbol(object, relocation->symbol_file_index),
        relocation->addend_known, relocation->addend);
  }
  switch (operand->kind) {
  case CTOOL_X86_OPERAND_REGISTER:
    return dis_render_register(output, operand->as.reg);
  case CTOOL_X86_OPERAND_IMMEDIATE:
    if (operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE) {
      return dis_literal(output, "<reference>");
    }
    return dis_hex_value(output, operand->as.value.bits);
  case CTOOL_X86_OPERAND_RELATIVE:
    if (operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE) {
      return dis_literal(output, "<reference>");
    }
    {
      return dis_hex_u32(
          output, dis_relative_target(logical_address, decoded, operand));
    }
  case CTOOL_X86_OPERAND_MEMORY:
    return dis_render_memory(output, operand, object, relocation);
  case CTOOL_X86_OPERAND_FAR_POINTER: {
    ctool_status_t status =
        dis_hex_value(output, operand->as.far_pointer.segment.bits);
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_value(output, operand->as.far_pointer.offset.bits);
    }
    return status;
  }
  case CTOOL_X86_OPERAND_NONE:
  default:
    return dis_literal(output, "<operand>");
  }
}

static ctool_status_t dis_render_data_bytes(ctool_text_sink_t output,
                                            const ctool_x86_decoded_t *decoded) {
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "db ");
  for (index = 0u; status == CTOOL_OK && index < decoded->encoding.size;
       index++) {
    if (index != 0u) {
      status = dis_literal(output, ", ");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "0x");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_fixed(output,
                             (ctool_u32)decoded->encoding.bytes[index], 2u);
    }
  }
  return status;
}

static ctool_status_t dis_render_instruction(
    ctool_text_sink_t output, ctool_u32 logical_address,
    ctool_u32 instruction_offset, const ctool_x86_decoded_t *decoded,
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index) {
  ctool_u32 index;
  ctool_status_t status = dis_hex_fixed(output, logical_address, 8u);
  if (status == CTOOL_OK) {
    status = dis_literal(output, ":  ");
  }
  for (index = 0u; status == CTOOL_OK && index < decoded->encoding.size;
       index++) {
    status = dis_hex_fixed(output, (ctool_u32)decoded->encoding.bytes[index],
                           2u);
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
  }
  if (status == CTOOL_OK) {
    status = dis_space(output);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (decoded->kind != CTOOL_X86_DECODE_KNOWN) {
    status = dis_render_data_bytes(output, decoded);
  } else {
    ctool_string_t mnemonic =
        ctool_x86_mnemonic_name(decoded->instruction.mnemonic);
    if ((decoded->instruction.prefixes & CTOOL_X86_PREFIX_LOCK) != 0u) {
      status = dis_literal(output, "lock ");
    }
    if (status == CTOOL_OK &&
        (decoded->instruction.prefixes & CTOOL_X86_PREFIX_REP) != 0u) {
      status = dis_literal(output, "rep ");
    }
    if (status == CTOOL_OK &&
        (decoded->instruction.prefixes & CTOOL_X86_PREFIX_REPNE) != 0u) {
      status = dis_literal(output, "repne ");
    }
    if (status == CTOOL_OK) {
      status = mnemonic.size == 0u ? dis_literal(output, "<unknown>")
                                   : dis_string(output, mnemonic);
    }
    if (status == CTOOL_OK && decoded->instruction.operand_count != 0u) {
      status = dis_space(output);
    }
    for (index = 0u; status == CTOOL_OK &&
                     index < decoded->instruction.operand_count;
         index++) {
      if (index != 0u) {
        status = dis_literal(output, ", ");
      }
      if (status == CTOOL_OK) {
        status = dis_render_operand(
            output, &decoded->instruction.operands[index], logical_address,
            report, object,
            section_file_index, instruction_offset, decoded, index);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "\n");
  }
  return status;
}

static ctool_status_t dis_render_raw_labels(
    const ctool_dis_report_t *report, ctool_u32 address, ctool_u32 *cursor,
    ctool_text_sink_t output) {
  ctool_status_t status = CTOOL_OK;
  while (*cursor < report->raw_label_order_count &&
         report->labels[report->raw_label_order[*cursor]].address < address) {
    (*cursor)++;
  }
  while (status == CTOOL_OK && *cursor < report->raw_label_order_count &&
         report->labels[report->raw_label_order[*cursor]].address == address) {
    const ctool_dis_label_t *label =
        &report->labels[report->raw_label_order[*cursor]];
    status = dis_hex_fixed(output, address, 8u);
    if (status == CTOOL_OK) {
      status = dis_literal(output, " <");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, label->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ">:\n");
    }
    (*cursor)++;
  }
  return status;
}

static ctool_u32 dis_symbol_address(const ctool_elf32_object_t *object,
                                    const ctool_elf32_symbol_t *symbol) {
  if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED) {
    return symbol->value;
  }
  if (object->file_type == CTOOL_ELF32_ET_REL) {
    const ctool_elf32_section_t *section =
        dis_section(object, symbol->section_file_index);
    return section == (const ctool_elf32_section_t *)0
               ? symbol->value
               : section->address + symbol->value;
  }
  return symbol->value;
}

static ctool_status_t dis_render_elf_labels(
    const ctool_dis_report_t *report, ctool_u32 section_file_index,
    ctool_u32 address, ctool_bool section_specific, ctool_u32 *cursor,
    ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_status_t status = CTOOL_OK;
  while (*cursor < report->function_order_count) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->function_order[*cursor]];
    if (section_specific == CTOOL_TRUE &&
        symbol->section_file_index < section_file_index) {
      (*cursor)++;
    } else if (section_specific == CTOOL_TRUE &&
               symbol->section_file_index > section_file_index) {
      return status;
    } else if (dis_symbol_address(object, symbol) < address) {
      (*cursor)++;
    } else {
      break;
    }
  }
  while (status == CTOOL_OK && *cursor < report->function_order_count) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->function_order[*cursor]];
    if ((section_specific == CTOOL_TRUE &&
         symbol->section_file_index != section_file_index) ||
        dis_symbol_address(object, symbol) != address) {
      break;
    }
    status = dis_hex_fixed(output, address, 8u);
    if (status == CTOOL_OK) {
      status = dis_literal(output, " <");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ">:\n");
    }
    (*cursor)++;
  }
  return status;
}

static ctool_status_t dis_render_region(
    ctool_job_t *job, const ctool_dis_report_t *report, ctool_bytes_t bytes,
    ctool_u32 base_address, ctool_x86_mode_t mode,
    const ctool_elf32_object_t *object, ctool_u32 section_file_index,
    ctool_bool section_specific, ctool_u32 *label_cursor,
    ctool_text_sink_t output) {
  ctool_u32 offset = 0u;
  ctool_status_t status = CTOOL_OK;
  if (bytes.size == 0u) {
    return CTOOL_OK;
  }
  while (status == CTOOL_OK && offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_u32 address = base_address + offset;
    if (object == (const ctool_elf32_object_t *)0) {
      status = dis_render_raw_labels(report, address, label_cursor, output);
    } else {
      status = dis_render_elf_labels(report, section_file_index, address,
                                     section_specific, label_cursor, output);
    }
    if (status == CTOOL_OK) {
      status = ctool_x86_decode(job, mode, bytes, offset, &decoded);
    }
    if (status != CTOOL_OK) {
      break;
    }
    if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u || decoded.consumed > bytes.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    status = dis_render_instruction(output, address, offset, &decoded, report,
                                    object, section_file_index);
    offset += (ctool_u32)decoded.consumed;
  }
  return status;
}

static ctool_status_t dis_render_raw_data_region(
    const ctool_dis_report_t *report, ctool_bytes_t bytes,
    ctool_u32 base_address, ctool_u32 *label_cursor,
    ctool_text_sink_t output) {
  ctool_u32 offset = 0u;
  ctool_status_t status = CTOOL_OK;
  while (status == CTOOL_OK && offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_u32 address = base_address + offset;
    ctool_u32 remaining = bytes.size - offset;
    ctool_u32 row_size =
        remaining < CTOOL_X86_MAX_INSTRUCTION_BYTES
            ? remaining
            : CTOOL_X86_MAX_INSTRUCTION_BYTES;
    ctool_u32 index;
    status = dis_render_raw_labels(report, address, label_cursor, output);
    if (status != CTOOL_OK) {
      break;
    }
    if (*label_cursor < report->raw_label_order_count) {
      ctool_u32 next_address =
          report->labels[report->raw_label_order[*label_cursor]].address;
      if (next_address > address && next_address - address < row_size) {
        row_size = next_address - address;
      }
    }
    for (index = 0u; index < (ctool_u32)sizeof(decoded); index++) {
      ((ctool_u8 *)(void *)&decoded)[index] = 0u;
    }
    decoded.kind = CTOOL_X86_DECODE_UNKNOWN;
    decoded.encoding.size = (ctool_u8)row_size;
    for (index = 0u; index < row_size; index++) {
      decoded.encoding.bytes[index] = bytes.data[offset + index];
    }
    status = dis_render_instruction(output, address, offset, &decoded, report,
                                    (const ctool_elf32_object_t *)0, 0u);
    offset += row_size;
  }
  return status;
}

static ctool_u32 dis_function_lower_bound(const ctool_dis_report_t *report,
                                           ctool_u32 address) {
  ctool_u32 first = 0u;
  ctool_u32 last = report->function_order_count;
  while (first < last) {
    ctool_u32 middle = first + (last - first) / 2u;
    const ctool_elf32_symbol_t *symbol =
        &report->elf32.symbols[report->function_order[middle]];
    if (dis_symbol_address(&report->elf32, symbol) < address) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  return first;
}

static ctool_status_t dis_render_disassembly(ctool_job_t *job,
                                             const ctool_dis_report_t *report,
                                             ctool_text_sink_t output) {
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 label_cursor = 0u;
  if (report->input == CTOOL_DIS_INPUT_RAW) {
    status = dis_literal(output, "[disassembly raw]\n");
    if (status == CTOOL_OK && report->mode != CTOOL_DIS_RAW_RANGE_MAP) {
      status = dis_render_region(job, report, report->source->contents,
                                 report->base_address, report->mode,
                                 (const ctool_elf32_object_t *)0, 0u,
                                 CTOOL_FALSE, &label_cursor, output);
    }
    for (index = 0u;
         status == CTOOL_OK && report->mode == CTOOL_DIS_RAW_RANGE_MAP &&
         index < report->raw_range_count;
         index++) {
      ctool_u32 first = report->raw_ranges[index].offset;
      ctool_u32 last = index + 1u < report->raw_range_count
                           ? report->raw_ranges[index + 1u].offset
                           : report->source->contents.size;
      ctool_bytes_t bytes = ctool_bytes(report->source->contents.data + first,
                                        last - first);
      if (report->raw_ranges[index].kind == CTOOL_DIS_RAW_RANGE_DATA) {
        status = dis_render_raw_data_region(
            report, bytes, report->base_address + first, &label_cursor,
            output);
      } else {
        status = dis_render_region(
            job, report, bytes, report->base_address + first,
            dis_raw_range_mode(report->raw_ranges[index].kind),
            (const ctool_elf32_object_t *)0, 0u, CTOOL_FALSE, &label_cursor,
            output);
      }
    }
    return status;
  }
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    status = CTOOL_OK;
    for (index = 0u;
         status == CTOOL_OK && index < report->pe32.section_count; index++) {
      const ctool_pe32_section_t *section = &report->pe32.sections[index];
      if ((section->characteristics & CTOOL_PE32_SCN_EXECUTE) == 0u ||
          section->contents.size == 0u) {
        continue;
      }
      status = dis_literal(output, "[disassembly ");
      if (status == CTOOL_OK) {
        status = dis_string(output, section->name);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "]\n");
      }
      if (status == CTOOL_OK) {
        label_cursor = 0u;
        status = dis_render_region(
            job, report, section->contents,
            report->pe32.image_base + section->virtual_address,
            report->mode, (const ctool_elf32_object_t *)0, 0u,
            CTOOL_FALSE, &label_cursor, output);
      }
    }
    return status;
  }
  if (report->elf32.file_type == CTOOL_ELF32_ET_EXEC) {
    status = CTOOL_OK;
    for (index = 0u; status == CTOOL_OK &&
                     index < report->elf32.program_header_count;
         index++) {
      const ctool_elf32_program_header_t *program =
          &report->elf32.program_headers[index];
      if (program->type != CTOOL_ELF32_PT_LOAD ||
          (program->flags & CTOOL_ELF32_PF_X) == 0u ||
          program->contents.size == 0u) {
        continue;
      }
      status = dis_literal(output, "[disassembly LOAD#");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, program->file_index);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "]\n");
      }
      if (status == CTOOL_OK) {
        label_cursor =
            dis_function_lower_bound(report, program->virtual_address);
        status = dis_render_region(
            job, report, program->contents, program->virtual_address,
            report->mode, &report->elf32, 0u, CTOOL_FALSE, &label_cursor,
            output);
      }
    }
    return status;
  }
  status = CTOOL_OK;
  for (index = 0u; status == CTOOL_OK &&
                   index < report->elf32.section_count;
       index++) {
    const ctool_elf32_section_t *section = &report->elf32.sections[index];
    if (section->type != CTOOL_ELF32_SHT_PROGBITS ||
        (section->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
        section->contents.size == 0u) {
      continue;
    }
    status = dis_literal(output, "[disassembly ");
    if (status == CTOOL_OK) {
      status = section->name.size == 0u ? dis_literal(output, "<section>")
                                        : dis_string(output, section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "]\n");
    }
    if (status == CTOOL_OK) {
      status = dis_render_region(job, report, section->contents,
                                  section->address, report->mode,
                                  &report->elf32,
                                  section->file_index, CTOOL_TRUE,
                                  &label_cursor, output);
    }
  }
  return status;
}

static char dis_nm_type(const ctool_elf32_object_t *object,
                         const ctool_elf32_symbol_t *symbol) {
  char type;
  const ctool_elf32_section_t *section;
  if (symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED) {
    if (symbol->binding != CTOOL_ELF32_BIND_WEAK) {
      return 'U';
    }
    return symbol->type == CTOOL_ELF32_SYMBOL_OBJECT ||
                   symbol->type == CTOOL_ELF32_SYMBOL_COMMON ||
                   symbol->type == CTOOL_ELF32_SYMBOL_TLS
               ? 'v'
               : 'w';
  }
  if (symbol->placement == CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
    return 'C';
  }
  if (symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
    type = 'A';
  } else {
    section = dis_section(object, symbol->section_file_index);
    if (section == (const ctool_elf32_section_t *)0) {
      type = '?';
    } else if ((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ||
               symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION) {
      type = 'T';
    } else if (section->type == CTOOL_ELF32_SHT_NOBITS) {
      type = 'B';
    } else if ((section->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      type = 'D';
    } else {
      type = 'R';
    }
  }
  if (symbol->binding == CTOOL_ELF32_BIND_WEAK) {
    return symbol->type == CTOOL_ELF32_SYMBOL_OBJECT ||
                   symbol->type == CTOOL_ELF32_SYMBOL_COMMON ||
                   symbol->type == CTOOL_ELF32_SYMBOL_TLS
               ? 'V'
               : 'W';
  }
  if (symbol->binding == CTOOL_ELF32_BIND_LOCAL && type >= 'A' && type <= 'Z') {
    return (char)(type + ('a' - 'A'));
  }
  return type;
}

static ctool_bool dis_nm_before(const ctool_elf32_object_t *object,
                                ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_symbol_t *left_symbol = &object->symbols[left];
  const ctool_elf32_symbol_t *right_symbol = &object->symbols[right];
  ctool_u32 left_address = dis_symbol_address(object, left_symbol);
  ctool_u32 right_address = dis_symbol_address(object, right_symbol);
  if (left_address != right_address) {
    return left_address < right_address ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return left_symbol->file_index < right_symbol->file_index ? CTOOL_TRUE
                                                             : CTOOL_FALSE;
}

static void dis_index_swap(ctool_u32 *left, ctool_u32 *right) {
  ctool_u32 temporary = *left;
  *left = *right;
  *right = temporary;
}

static void dis_nm_sift(const ctool_elf32_object_t *object,
                        ctool_u32 *indices, ctool_u32 root,
                        ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_nm_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_nm_before(object, indices[root], indices[child]) == CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_nm(const ctool_elf32_object_t *object,
                        ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_nm_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_nm_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_relocation_before(const ctool_elf32_object_t *object,
                                        ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_relocation_t *left_relocation =
      &object->relocations[left];
  const ctool_elf32_relocation_t *right_relocation =
      &object->relocations[right];
  if (left_relocation->relocation_section_file_index !=
      right_relocation->relocation_section_file_index) {
    return left_relocation->relocation_section_file_index <
                   right_relocation->relocation_section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (left_relocation->entry_index != right_relocation->entry_index) {
    return left_relocation->entry_index < right_relocation->entry_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_relocation_sift(const ctool_elf32_object_t *object,
                                ctool_u32 *indices, ctool_u32 root,
                                ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_relocation_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_relocation_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_relocations(const ctool_elf32_object_t *object,
                                 ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_relocation_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_relocation_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_relocation_site_before(
    const ctool_elf32_object_t *object, ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_relocation_t *left_relocation =
      &object->relocations[left];
  const ctool_elf32_relocation_t *right_relocation =
      &object->relocations[right];
  if (object->file_type == CTOOL_ELF32_ET_EXEC &&
      left_relocation->offset != right_relocation->offset) {
    return left_relocation->offset < right_relocation->offset ? CTOOL_TRUE
                                                               : CTOOL_FALSE;
  }
  if (left_relocation->target_section_file_index !=
      right_relocation->target_section_file_index) {
    return left_relocation->target_section_file_index <
                   right_relocation->target_section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      left_relocation->offset != right_relocation->offset) {
    return left_relocation->offset < right_relocation->offset ? CTOOL_TRUE
                                                               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_relocation_site_sift(const ctool_elf32_object_t *object,
                                     ctool_u32 *indices, ctool_u32 root,
                                     ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_relocation_site_before(object, indices[child],
                                   indices[child + 1u]) == CTOOL_TRUE) {
      child++;
    }
    if (dis_relocation_site_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_relocation_sites(const ctool_elf32_object_t *object,
                                      ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_relocation_site_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_relocation_site_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_raw_label_before(const ctool_dis_report_t *report,
                                       ctool_u32 left, ctool_u32 right) {
  if (report->labels[left].address != report->labels[right].address) {
    return report->labels[left].address < report->labels[right].address
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_raw_label_sift(const ctool_dis_report_t *report,
                               ctool_u32 *indices, ctool_u32 root,
                               ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_raw_label_before(report, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_raw_label_before(report, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_raw_labels(const ctool_dis_report_t *report,
                                ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_raw_label_sift(report, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_raw_label_sift(report, indices, 0u, end);
  }
}

static ctool_status_t dis_prepare_raw_label_order(ctool_job_t *job,
                                                   ctool_dis_report_t *report) {
  ctool_u32 *order = (ctool_u32 *)0;
  ctool_u32 index;
  ctool_u32 count = 0u;
  ctool_status_t status;
  if (report->label_count == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(job), report->label_count, (ctool_u32)sizeof(ctool_u32),
      (ctool_u32)sizeof(ctool_u32), (void **)&order);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < report->label_count; index++) {
    if (report->labels[index].name.size != 0u) {
      order[count] = index;
      count++;
    }
  }
  dis_sort_raw_labels(report, order, count);
  report->raw_label_order = order;
  report->raw_label_order_count = count;
  return CTOOL_OK;
}

static ctool_bool dis_function_before(const ctool_elf32_object_t *object,
                                      ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_symbol_t *left_symbol = &object->symbols[left];
  const ctool_elf32_symbol_t *right_symbol = &object->symbols[right];
  ctool_u32 left_address;
  ctool_u32 right_address;
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      left_symbol->section_file_index != right_symbol->section_file_index) {
    return left_symbol->section_file_index < right_symbol->section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  left_address = dis_symbol_address(object, left_symbol);
  right_address = dis_symbol_address(object, right_symbol);
  if (left_address != right_address) {
    return left_address < right_address ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return left_symbol->file_index < right_symbol->file_index ? CTOOL_TRUE
                                                             : CTOOL_FALSE;
}

static void dis_function_sift(const ctool_elf32_object_t *object,
                              ctool_u32 *indices, ctool_u32 root,
                              ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_function_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_function_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_functions(const ctool_elf32_object_t *object,
                               ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_function_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_function_sift(object, indices, 0u, end);
  }
}

static ctool_status_t dis_prepare_report_orders(ctool_job_t *job,
                                                 ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_u32 *symbol_order = (ctool_u32 *)0;
  ctool_u32 *function_order = (ctool_u32 *)0;
  ctool_u32 *relocation_order = (ctool_u32 *)0;
  ctool_u32 *relocation_site_order = (ctool_u32 *)0;
  ctool_u32 index;
  ctool_u32 count = 0u;
  ctool_u32 function_count = 0u;
  ctool_status_t status = CTOOL_OK;
  if ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
          symbol->name.size != 0u) {
        function_count++;
      }
    }
  }
  if (report->views == CTOOL_DIS_VIEW_SYMBOLS &&
      report->elf32.symbol_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.symbol_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&symbol_order);
  }
  if (status == CTOOL_OK && function_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, function_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&function_order);
  }
  if (status == CTOOL_OK &&
      (report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u &&
      report->elf32.relocation_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.relocation_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&relocation_order);
  }
  if (status == CTOOL_OK &&
      (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u &&
      report->elf32.relocation_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.relocation_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&relocation_site_order);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (report->views == CTOOL_DIS_VIEW_SYMBOLS) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      if (report->elf32.symbols[index].name.size != 0u) {
        symbol_order[count] = index;
        count++;
      }
    }
    dis_sort_nm(&report->elf32, symbol_order, count);
    report->symbol_order = symbol_order;
    report->symbol_order_count = count;
  }
  count = 0u;
  if (function_count != 0u) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
          symbol->name.size != 0u) {
        function_order[count] = index;
        count++;
      }
    }
    dis_sort_functions(&report->elf32, function_order, count);
    report->function_order = function_order;
    report->function_order_count = count;
  }
  if ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u) {
    for (index = 0u; index < report->elf32.relocation_count; index++) {
      relocation_order[index] = index;
    }
    dis_sort_relocations(&report->elf32, relocation_order,
                         report->elf32.relocation_count);
    report->relocation_order = relocation_order;
    report->relocation_order_count = report->elf32.relocation_count;
  }
  if ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report->elf32.relocation_count; index++) {
      relocation_site_order[index] = index;
    }
    dis_sort_relocation_sites(&report->elf32, relocation_site_order,
                              report->elf32.relocation_count);
    report->relocation_site_order = relocation_site_order;
    report->relocation_site_order_count = report->elf32.relocation_count;
  }
  return CTOOL_OK;
}

static ctool_status_t dis_render_nm(const ctool_dis_report_t *report,
                                    ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  for (index = 0u;
       status == CTOOL_OK && index < report->symbol_order_count; index++) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->symbol_order[index]];
    char type = dis_nm_type(object, symbol);
    status = symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED
                 ? dis_literal(output, "        ")
                 : dis_hex_fixed(output, dis_symbol_address(object, symbol),
                                 8u);
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_write(output, &type, 1u);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_bool dis_report_shape_valid(const ctool_dis_report_t *report) {
  ctool_u32 index;
  if (report->source == (const ctool_source_t *)0 ||
      (report->source->contents.data == (const ctool_u8 *)0 &&
       report->source->contents.size != 0u) ||
      (report->source->path.text.data == (const char *)0 &&
       report->source->path.text.size != 0u) ||
      report->views == 0u || (report->views & ~CTOOL_DIS_VIEW_ALL) != 0u ||
      (report->policies & ~CTOOL_DIS_POLICY_ALL) != 0u) {
    return CTOOL_FALSE;
  }
  if (report->input == CTOOL_DIS_INPUT_RAW) {
    ctool_dis_request_t edge_request;
    if ((report->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u ||
        report->views != CTOOL_DIS_VIEW_DISASSEMBLY ||
        (report->label_count != 0u &&
         report->labels == (const ctool_dis_label_t *)0) ||
        (report->source->contents.size != 0u &&
         report->base_address >
             DIS_U32_MAX - (report->source->contents.size - 1u))) {
      return CTOOL_FALSE;
    }
    if (report->mode == CTOOL_DIS_RAW_RANGE_MAP) {
      if (dis_raw_map_issue(report->source->contents.size,
                            report->raw_ranges,
                            report->raw_range_count) != DIS_RAW_MAP_VALID) {
        return CTOOL_FALSE;
      }
    } else if (dis_x86_mode_valid(report->mode) == CTOOL_FALSE ||
               report->raw_ranges != (const ctool_dis_raw_range_t *)0 ||
               report->raw_range_count != 0u) {
      return CTOOL_FALSE;
    }
    {
      ctool_u8 *request_bytes = (ctool_u8 *)&edge_request;
      for (index = 0u; index < (ctool_u32)sizeof(edge_request); index++) {
        request_bytes[index] = 0u;
      }
    }
    edge_request.input = CTOOL_DIS_INPUT_RAW;
    edge_request.policies = report->policies;
    edge_request.raw_mode = report->mode;
    edge_request.raw_base_address = report->base_address;
    edge_request.raw_ranges = report->raw_ranges;
    edge_request.raw_range_count = report->raw_range_count;
    edge_request.raw_edges = report->raw_edges;
    edge_request.raw_edge_count = report->raw_edge_count;
    edge_request.raw_edge_metadata_present =
        report->raw_edge_metadata_present;
    if (dis_raw_edge_issue(report->source, &edge_request) !=
        (const char *)0) {
      return CTOOL_FALSE;
    }
    if ((report->policies & CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS) != 0u &&
        report->source->contents.size > 65536u) {
      if (report->mode == CTOOL_X86_MODE_16) {
        return CTOOL_FALSE;
      }
      for (index = 0u; index < report->raw_range_count; index++) {
        if (report->raw_ranges[index].kind ==
            CTOOL_DIS_RAW_RANGE_CODE16) {
          return CTOOL_FALSE;
        }
      }
    }
    for (index = 0u; index < report->label_count; index++) {
      if (report->labels[index].name.data == (const char *)0 &&
          report->labels[index].name.size != 0u) {
        return CTOOL_FALSE;
      }
    }
    if (report->raw_label_order_count > report->label_count ||
        (report->raw_label_order_count != 0u &&
         report->raw_label_order == (const ctool_u32 *)0)) {
      return CTOOL_FALSE;
    }
    for (index = 0u; index < report->raw_label_order_count; index++) {
      if (report->raw_label_order[index] >= report->label_count) {
        return CTOOL_FALSE;
      }
    }
    return CTOOL_TRUE;
  }
  if (report->input == CTOOL_DIS_INPUT_PE32) {
    if ((report->views &
         (CTOOL_DIS_VIEW_SYMBOLS | CTOOL_DIS_VIEW_RELOCATIONS)) != 0u ||
        (report->policies != 0u &&
         (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) ||
        report->mode != CTOOL_X86_MODE_32 ||
        report->source->contents.size < 2u ||
        report->source->contents.data[0] != (ctool_u8)'M' ||
        report->source->contents.data[1] != (ctool_u8)'Z' ||
        report->pe32.image.data != report->source->contents.data ||
        report->pe32.image.size != report->source->contents.size ||
        report->pe32.image_base != CTOOL_PE32_IMAGE_BASE ||
        report->pe32.entry_point !=
            report->pe32.image_base + report->pe32.entry_rva ||
        report->pe32.section_count == 0u ||
        report->pe32.section_count > 5u ||
        report->pe32.sections == (const ctool_pe32_section_t *)0 ||
        (report->pe32.import_library_count != 0u &&
         report->pe32.import_libraries ==
             (const ctool_pe32_import_library_t *)0) ||
        (report->pe32.import_count != 0u &&
         report->pe32.imports == (const ctool_pe32_import_t *)0) ||
        report->raw_ranges != (const ctool_dis_raw_range_t *)0 ||
        report->raw_range_count != 0u ||
        report->labels != (const ctool_dis_label_t *)0 ||
        report->label_count != 0u ||
        report->raw_label_order != (const ctool_u32 *)0 ||
        report->raw_label_order_count != 0u ||
        report->function_order != (const ctool_u32 *)0 ||
        report->function_order_count != 0u ||
        report->symbol_order != (const ctool_u32 *)0 ||
        report->symbol_order_count != 0u ||
        report->relocation_order != (const ctool_u32 *)0 ||
        report->relocation_order_count != 0u ||
        report->relocation_site_order != (const ctool_u32 *)0 ||
        report->relocation_site_order_count != 0u ||
        report->elf32.image.data != (const ctool_u8 *)0 ||
        report->elf32.image.size != 0u) {
      return CTOOL_FALSE;
    }
    for (index = 0u; index < report->pe32.section_count; index++) {
      const ctool_pe32_section_t *section = &report->pe32.sections[index];
      if (section->file_index != index || section->name.size == 0u ||
          section->name.data == (const char *)0 ||
          section->virtual_size == 0u ||
          (section->contents.size != 0u &&
           section->contents.data == (const ctool_u8 *)0) ||
          (section->contents.size != 0u &&
           (section->file_offset > report->source->contents.size ||
            section->contents.size > report->source->contents.size -
                                         section->file_offset ||
            section->contents.data != report->source->contents.data +
                                          section->file_offset))) {
        return CTOOL_FALSE;
      }
    }
    for (index = 0u; index < report->pe32.import_library_count; index++) {
      const ctool_pe32_import_library_t *library =
          &report->pe32.import_libraries[index];
      if (library->file_index != index || library->name.size == 0u ||
          library->name.data == (const char *)0 ||
          library->import_first > report->pe32.import_count ||
          library->import_count > report->pe32.import_count -
                                      library->import_first) {
        return CTOOL_FALSE;
      }
    }
    for (index = 0u; index < report->pe32.import_count; index++) {
      const ctool_pe32_import_t *import = &report->pe32.imports[index];
      if (import->file_index != index ||
          import->library_file_index >= report->pe32.import_library_count ||
          import->library_name.size == 0u ||
          import->library_name.data == (const char *)0 ||
          import->procedure_name.size == 0u ||
          import->procedure_name.data == (const char *)0) {
        return CTOOL_FALSE;
      }
    }
    return CTOOL_TRUE;
  }
  if (report->input != CTOOL_DIS_INPUT_ELF32 ||
      report->raw_edge_metadata_present == CTOOL_TRUE ||
      report->raw_edges != (const ctool_dis_raw_edge_t *)0 ||
      report->raw_edge_count != 0u ||
      (report->views & CTOOL_DIS_VIEW_IMPORTS) != 0u ||
      (report->policies != 0u &&
       (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u) ||
      report->mode != CTOOL_X86_MODE_32 ||
      (report->elf32.file_type != CTOOL_ELF32_ET_REL &&
       report->elf32.file_type != CTOOL_ELF32_ET_EXEC) ||
      ((report->policies & CTOOL_DIS_POLICY_CODE_ANCHORS) != 0u &&
       report->elf32.file_type != CTOOL_ELF32_ET_EXEC) ||
      report->source->contents.size < 18u ||
      report->source->contents.data[16] !=
          (ctool_u8)report->elf32.file_type ||
      report->source->contents.data[17] != 0u ||
      report->elf32.image.data != report->source->contents.data ||
      report->elf32.image.size != report->source->contents.size ||
      (report->elf32.program_header_count != 0u &&
       report->elf32.program_headers ==
           (const ctool_elf32_program_header_t *)0) ||
      (report->elf32.section_count != 0u &&
       report->elf32.sections == (const ctool_elf32_section_t *)0) ||
      (report->elf32.symbol_count != 0u &&
       report->elf32.symbols == (const ctool_elf32_symbol_t *)0) ||
      (report->elf32.relocation_count != 0u &&
       report->elf32.relocations == (const ctool_elf32_relocation_t *)0) ||
      (report->views != CTOOL_DIS_VIEW_SYMBOLS &&
       (report->symbol_order != (const ctool_u32 *)0 ||
        report->symbol_order_count != 0u)) ||
      report->symbol_order_count > report->elf32.symbol_count ||
      (report->symbol_order_count != 0u &&
       report->symbol_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u &&
       (report->function_order != (const ctool_u32 *)0 ||
        report->function_order_count != 0u)) ||
      report->function_order_count > report->elf32.symbol_count ||
      (report->function_order_count != 0u &&
       report->function_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) == 0u &&
       (report->relocation_order != (const ctool_u32 *)0 ||
        report->relocation_order_count != 0u)) ||
      ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u &&
       report->relocation_order_count != report->elf32.relocation_count) ||
      (report->relocation_order_count != 0u &&
       report->relocation_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u &&
       (report->relocation_site_order != (const ctool_u32 *)0 ||
        report->relocation_site_order_count != 0u)) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u &&
       report->relocation_site_order_count !=
           report->elf32.relocation_count) ||
      (report->relocation_site_order_count != 0u &&
       report->relocation_site_order == (const ctool_u32 *)0)) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < report->elf32.program_header_count; index++) {
    if (report->elf32.program_headers[index].contents.data ==
            (const ctool_u8 *)0 &&
        report->elf32.program_headers[index].contents.size != 0u) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.section_count; index++) {
    const ctool_elf32_section_t *section = &report->elf32.sections[index];
    if ((section->name.data == (const char *)0 && section->name.size != 0u) ||
        (section->contents.data == (const ctool_u8 *)0 &&
         section->contents.size != 0u) ||
        section->relocation_first > report->elf32.relocation_count ||
        section->relocation_count >
            report->elf32.relocation_count - section->relocation_first) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
    if ((symbol->name.data == (const char *)0 && symbol->name.size != 0u) ||
        (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
         symbol->section_file_index >= report->elf32.section_count)) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->symbol_order_count; index++) {
    if (report->symbol_order[index] >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->function_order_count; index++) {
    if (report->function_order[index] >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &report->elf32.relocations[index];
    if (relocation->relocation_section_file_index >=
            report->elf32.section_count ||
        relocation->target_section_file_index >=
            report->elf32.section_count ||
        relocation->symbol_file_index >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->relocation_order_count; index++) {
    if (report->relocation_order[index] >= report->elf32.relocation_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->relocation_site_order_count; index++) {
    if (report->relocation_site_order[index] >=
        report->elf32.relocation_count) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

ctool_status_t ctool_dis_render(ctool_job_t *job,
                                 const ctool_dis_report_t *report,
                                 ctool_dis_text_t text,
                                 ctool_text_sink_t output) {
  ctool_status_t status = CTOOL_OK;
  if (job == (ctool_job_t *)0 || report == (const ctool_dis_report_t *)0 ||
      output.write == (ctool_status_t (*)(void *, ctool_bytes_t))0 ||
      (text != CTOOL_DIS_TEXT_CUPID && text != CTOOL_DIS_TEXT_NM) ||
      dis_report_shape_valid(report) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (text == CTOOL_DIS_TEXT_NM) {
    if (report->input != CTOOL_DIS_INPUT_ELF32 ||
        report->views != CTOOL_DIS_VIEW_SYMBOLS) {
      return dis_bad_request(job, report->source,
                             "nm text requires an ELF symbols-only report");
    }
    status = dis_render_nm(report, output);
  } else {
    if ((report->views & CTOOL_DIS_VIEW_HEADER) != 0u) {
      status = dis_render_header(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_SECTIONS) != 0u) {
      status = dis_render_sections(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_SYMBOLS) != 0u) {
      status = dis_render_symbols(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u) {
      status = dis_render_relocations(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_IMPORTS) != 0u) {
      status = dis_render_imports(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
      status = dis_render_disassembly(job, report, output);
    }
  }
  if (status != CTOOL_OK) {
    ctool_status_t emitted =
        dis_emit(job, report->source->path.text, CTOOL_DIS_DIAG_OUTPUT, 0u,
                 "CupidDis could not complete report output", status);
    if (emitted != status) {
      return emitted;
    }
  }
  return status;
}
