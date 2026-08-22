#ifndef CUPID_TOOLCHAIN_CUPIDDIS_H
#define CUPID_TOOLCHAIN_CUPIDDIS_H

#include "ctool.h"
#include "elf32.h"
#include "x86.h"

/* Platform-neutral inspection seam shared by the hosted and kernel CupidDis
 * drivers.  Parsing and decoding stay behind this interface; reports retain
 * typed file metadata so in-process consumers never need to scrape text. */

typedef enum {
  CTOOL_DIS_INPUT_RAW = 0,
  CTOOL_DIS_INPUT_ELF32
} ctool_dis_input_t;

#define CTOOL_DIS_VIEW_HEADER 0x00000001u
#define CTOOL_DIS_VIEW_SECTIONS 0x00000002u
#define CTOOL_DIS_VIEW_SYMBOLS 0x00000004u
#define CTOOL_DIS_VIEW_RELOCATIONS 0x00000008u
#define CTOOL_DIS_VIEW_DISASSEMBLY 0x00000010u
#define CTOOL_DIS_VIEW_ALL 0x0000001fu

/* Raw, relocatable ELF32, and linked ELF32 callers can ask CupidDis to
 * classify constant direct relative targets.  An ELF request must include
 * the disassembly view.
 * Inspection still succeeds for structurally valid code, so a caller that
 * enforces this policy rejects a report when the input-specific invalid target
 * counters below are nonzero. */
#define CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS 0x00000001u
/* Static linked ELF32 callers can also ask CupidDis to classify the entry
 * point and each defined function symbol against decoded instruction starts.
 * Invalid anchors are reported through the typed counters below. */
#define CTOOL_DIS_POLICY_CODE_ANCHORS 0x00000002u
#define CTOOL_DIS_POLICY_ALL                                               \
  (CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS | CTOOL_DIS_POLICY_CODE_ANCHORS)

typedef enum {
  CTOOL_DIS_TEXT_CUPID = 0,
  CTOOL_DIS_TEXT_NM
} ctool_dis_text_t;

typedef struct {
  ctool_u32 address;
  ctool_string_t name;
} ctool_dis_label_t;

#define CTOOL_DIS_RAW_RANGE_MAP ((ctool_x86_mode_t)0)
/* Compatibility alias for the former map selector. */
#define CTOOL_DIS_RAW_MODE_MAP CTOOL_DIS_RAW_RANGE_MAP

typedef enum {
  CTOOL_DIS_RAW_RANGE_CODE16 = 1,
  CTOOL_DIS_RAW_RANGE_CODE32,
  CTOOL_DIS_RAW_RANGE_DATA
} ctool_dis_raw_range_kind_t;

/* A mapped raw request starts with one range at offset zero.  Later ranges
 * change the code or data kind at strictly increasing byte offsets.  The
 * caller owns boundary selection.  A fixed-mode request leaves the range
 * pointer null and the count zero. */
typedef struct {
  ctool_u32 offset;
  ctool_dis_raw_range_kind_t kind;
} ctool_dis_raw_range_t;

/* Decode counts cover code regions selected for disassembly.  Bytes in
 * declared raw DATA ranges and non-executable ELF regions are not decoded or
 * counted.  Relocation counts cover relocations whose targets are executable
 * ET_REL sections.  An unmatched relocation does not name a compatible
 * four-byte field in a decoded instruction.  Direct-relative target counts
 * are populated only when the matching policy is selected.  Outside-image and
 * data counts belong to raw or linked ET_EXEC input; for ET_EXEC they mean an
 * address outside every load region or inside loaded bytes without file-backed
 * executable code.  Outside-section counts belong to ET_REL.  Far pointers
 * and register or memory targets are outside that policy. */
typedef struct {
  ctool_u64 known_count;
  ctool_u64 unknown_count;
  ctool_u64 invalid_count;
  ctool_u64 truncated_count;
  ctool_u64 executable_relocation_count;
  ctool_u64 unmatched_executable_relocation_count;
  ctool_u64 direct_relative_target_count;
  ctool_u64 direct_relative_outside_image_count;
  ctool_u64 direct_relative_data_count;
  ctool_u64 direct_relative_wrong_mode_count;
  ctool_u64 direct_relative_mid_instruction_count;
  ctool_u64 direct_relative_outside_section_count;
  ctool_u64 code_anchor_count;
  ctool_u64 code_anchor_outside_executable_count;
  ctool_u64 code_anchor_mid_instruction_count;
} ctool_dis_decode_summary_t;

typedef struct {
  ctool_dis_input_t input;
  ctool_u32 views;
  ctool_u32 policies;
  ctool_x86_mode_t raw_mode;
  ctool_u32 raw_base_address;
  const ctool_dis_raw_range_t *raw_ranges;
  ctool_u32 raw_range_count;
  const ctool_dis_label_t *labels;
  ctool_u32 label_count;
} ctool_dis_request_t;

typedef struct {
  const ctool_source_t *source;
  ctool_dis_input_t input;
  ctool_u32 views;
  ctool_u32 policies;
  ctool_x86_mode_t mode;
  ctool_u32 base_address;
  const ctool_dis_raw_range_t *raw_ranges;
  ctool_u32 raw_range_count;
  const ctool_dis_label_t *labels;
  ctool_u32 label_count;
  const ctool_u32 *raw_label_order;
  ctool_u32 raw_label_order_count;
  ctool_elf32_object_t elf32;
  const ctool_u32 *function_order;
  ctool_u32 function_order_count;
  const ctool_u32 *symbol_order;
  ctool_u32 symbol_order_count;
  const ctool_u32 *relocation_order;
  ctool_u32 relocation_order_count;
  const ctool_u32 *relocation_site_order;
  ctool_u32 relocation_site_order_count;
  ctool_dis_decode_summary_t decode_summary;
} ctool_dis_report_t;

typedef enum {
  CTOOL_DIS_DIAG_INVALID_REQUEST = 0x05000001u,
  CTOOL_DIS_DIAG_ADDRESS_OVERFLOW = 0x05000002u,
  CTOOL_DIS_DIAG_OUTPUT = 0x05000003u
} ctool_dis_diag_code_t;

ctool_status_t ctool_dis_inspect(ctool_job_t *job,
                                  const ctool_source_t *source,
                                  const ctool_dis_request_t *request,
                                  ctool_dis_report_t *report_out);
ctool_status_t ctool_dis_inspect_indexed(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    const ctool_source_t *source, const ctool_dis_request_t *request,
    ctool_dis_report_t *report_out);
ctool_status_t ctool_dis_render(ctool_job_t *job,
                                 const ctool_dis_report_t *report,
                                 ctool_dis_text_t text,
                                 ctool_text_sink_t output);

/* The source descriptor and bytes, raw mode ranges, raw labels and their
 * names, and all ELF names and payload views are borrowed and must outlive
 * rendering.  ELF metadata arrays and every derived
 * raw/function/symbol/relocation order array are owned by the inspecting job's
 * arena, so that job must also outlive rendering.  Only an unmodified report
 * returned successfully by ctool_dis_inspect may be passed to
 * ctool_dis_render.
 *
 * Inspection failures zero report_out and rewind allocations made by the
 * operation.  Indexed inspection borrows an immutable prepared decoder that
 * must outlive the call.  Rendering is streaming and deterministic; a failing
 * output adapter may already have accepted an earlier prefix. */

#endif
