#ifndef CUPID_KERNEL_AS_ELF_H
#define CUPID_KERNEL_AS_ELF_H

#include "ctool.h"
#include "cupidasm.h"
#include "cupidld.h"

typedef enum {
  AS_ARTIFACT_FORMAT_BIN = 1,
  AS_ARTIFACT_FORMAT_ELF32,
  AS_ARTIFACT_FORMAT_EXEC
} as_artifact_format_t;

typedef struct {
  as_artifact_format_t format;
  ctool_x86_mode_t initial_mode;
  ctool_u32 initial_origin;
  const ctool_asm_definition_t *definitions;
  ctool_u32 definition_count;
  const ctool_path_t *include_roots;
  ctool_u32 include_root_count;
  const ctool_string_t *entry_candidates;
  ctool_u32 entry_candidate_count;
  ctool_bool case_insensitive_symbols;
  ctool_bool allow_implicit_externs;
  ctool_u32 executable_text_address;
  ctool_u32 executable_maximum_span;
} as_artifact_request_t;

typedef struct {
  as_artifact_format_t format;
  ctool_bytes_t bytes;
  const ctool_asm_raw_range_t *raw_ranges;
  ctool_u32 raw_range_count;
  const ctool_asm_raw_edge_t *raw_edges;
  ctool_u32 raw_edge_count;
  ctool_u32 raw_origin;
  ctool_string_t entry_symbol;
  ctool_u32 entry_address;
  ctool_ld_result_t link;
} as_artifact_result_t;

/* Build one kernel-owned CupidASM artifact through the shared assembler.
 * Raw results retain their typed ranges and source-resolved control edges.
 * ELF32 results remain unlinked. Executable results pass the relocatable
 * object to CupidLD. Output must be empty, and any failure restores it to
 * empty and clears the result. */
ctool_status_t as_artifact_assemble(ctool_job_t *job,
                                    const ctool_source_t *source,
                                    const as_artifact_request_t *request,
                                    ctool_buffer_t *output,
                                    as_artifact_result_t *result_out);

/* Render the canonical cupid.raw-map.v2 map carried by one successful raw
 * result. The result borrows its byte, range, and control-edge views. Output
 * must be empty and stays empty when validation, formatting, or buffer growth
 * fails. */
ctool_status_t as_artifact_render_raw_map(
    const as_artifact_result_t *result, ctool_buffer_t *output);

typedef enum {
  AS_COMMAND_AS = 1,
  AS_COMMAND_CUPIDASM
} as_command_frontend_t;

typedef enum {
  AS_COMMAND_JIT = 1,
  AS_COMMAND_ARTIFACT
} as_command_kind_t;

typedef struct {
  as_command_kind_t kind;
  as_artifact_format_t format;
  ctool_string_t source;
  ctool_string_t output;
  ctool_string_t map;
} as_command_t;

/* Parse the in-OS `as` and `cupidasm` argument surfaces. Returned strings
 * borrow the caller's argument buffer. `as SOURCE` selects JIT. The older
 * AOT spellings select linked executable output. */
ctool_status_t as_command_parse(as_command_frontend_t frontend,
                                ctool_string_t arguments,
                                as_command_t *command_out);

typedef struct {
  void *context;
  ctool_status_t (*inspect)(void *context, ctool_string_t path,
                            ctool_bool *exists_out);
  ctool_status_t (*read)(void *context, ctool_string_t path,
                         ctool_mut_bytes_t destination,
                         ctool_u32 *size_out);
  ctool_status_t (*write_new)(void *context, ctool_string_t path,
                              ctool_bytes_t contents);
  ctool_status_t (*replace)(void *context, ctool_string_t source,
                            ctool_string_t destination);
  ctool_status_t (*remove)(void *context, ctool_string_t path);
} as_artifact_publication_ops_t;

typedef struct {
  ctool_string_t target;
  ctool_string_t candidate;
  ctool_string_t backup;
  ctool_string_t absent;
  ctool_string_t commit;
} as_artifact_publication_path_t;

typedef struct {
  as_artifact_publication_path_t artifact;
  as_artifact_publication_path_t map;
  ctool_bytes_t artifact_bytes;
  ctool_bytes_t map_bytes;
  ctool_mut_bytes_t scratch;
  ctool_mut_bytes_t peer_scratch;
} as_artifact_publication_request_t;

/* Publish an artifact and its optional range map as one recoverable pair.
 * Linked pending records are present before either target moves. Backups
 * represent targets that existed, while tombstones represent targets that
 * were absent. One linked committed record is enough to preserve the new
 * pair after an interrupted cleanup. Each private path must be its normalized
 * absolute target plus the documented suffix. The two caller-owned scratch
 * spans must be separate because recovery may inspect two records at the same
 * time. */
ctool_status_t as_artifact_publish(
    const as_artifact_publication_ops_t *ops,
    const as_artifact_publication_request_t *request);

/* Link one validated CupidASM relocatable object through CupidLD's fixed-text
 * profile.  The assembler result and its bytes are borrowed for the call.
 * Output must be empty.  Failure leaves it empty and zeros the link result. */
ctool_status_t as_elf32_exec_link(ctool_job_t *job,
                                  const ctool_asm_result_t *artifact,
                                  ctool_u32 text_address,
                                  ctool_u32 maximum_image_span,
                                  ctool_buffer_t *output,
                                  ctool_ld_result_t *result_out);

/* Serialize one validated CupidASM fixed image as a sectionless i386 ELF32
 * executable.  The artifact and its byte/region views are borrowed for the
 * call.  Output must be empty; after any failure from an empty output it
 * remains empty. */
ctool_status_t as_elf32_exec_write(const ctool_asm_result_t *artifact,
                                   ctool_buffer_t *output);

#endif
