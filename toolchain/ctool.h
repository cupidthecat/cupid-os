#ifndef CUPID_TOOLCHAIN_CORE_H
#define CUPID_TOOLCHAIN_CORE_H

/*
 * Platform-neutral foundations shared by the Cupid compiler, assembler,
 * linker/object pipeline, and disassembler.  This header intentionally uses
 * no libc or kernel headers so the same implementation can run hosted or in
 * the freestanding i386 kernel.
 */

typedef unsigned char ctool_u8;
typedef unsigned short ctool_u16;
typedef unsigned int ctool_u32;
typedef unsigned long long ctool_u64;
typedef int ctool_i32;
typedef int ctool_bool;

#define CTOOL_FALSE 0
#define CTOOL_TRUE 1

typedef enum {
  CTOOL_OK = 0,
  CTOOL_ERR_INVALID_ARGUMENT,
  CTOOL_ERR_INPUT,
  CTOOL_ERR_NOT_FOUND,
  CTOOL_ERR_IO,
  CTOOL_ERR_NO_MEMORY,
  CTOOL_ERR_LIMIT,
  CTOOL_ERR_OVERFLOW,
  CTOOL_ERR_PATH,
  CTOOL_ERR_PATH_ESCAPE,
  CTOOL_ERR_UNSUPPORTED,
  CTOOL_ERR_INTERNAL
} ctool_status_t;

typedef struct {
  const ctool_u8 *data;
  ctool_u32 size;
} ctool_bytes_t;

typedef struct {
  ctool_u8 *data;
  ctool_u32 size;
} ctool_mut_bytes_t;

typedef struct {
  const char *data;
  ctool_u32 size;
} ctool_string_t;

typedef struct {
  void *context;
  void *(*allocate)(void *context, ctool_u32 bytes);
  void (*release)(void *context, void *allocation, ctool_u32 bytes);
} ctool_allocator_t;

typedef struct {
  void *context;
  ctool_status_t (*file_size)(void *context, ctool_string_t logical_path,
                              ctool_u32 *size_out);
  ctool_status_t (*read_exact)(void *context, ctool_string_t logical_path,
                               ctool_u8 *destination, ctool_u32 size);
  ctool_status_t (*write_all)(void *context, ctool_string_t logical_path,
                              ctool_bytes_t contents);
} ctool_file_store_t;

typedef struct {
  void *context;
  ctool_status_t (*write)(void *context, ctool_bytes_t text);
} ctool_text_sink_t;

/* Adapters consume calls synchronously and must not retain passed views.
 * Logical file paths are canonical absolute '/' paths with a trailing NUL
 * outside their recorded size.  read_exact/write_all succeed only after the
 * complete byte count has transferred. */

typedef struct {
  ctool_u32 arena_block_bytes;
  ctool_u32 arena_bytes;
  ctool_u32 source_bytes;
  ctool_u32 output_bytes;
  ctool_u32 path_bytes;
  ctool_u32 diagnostic_count;
  ctool_u32 diagnostic_message_bytes;
} ctool_limits_t;

/* A job reserves diagnostic_count fixed path/message slots at open using
 * path_bytes and diagnostic_message_bytes as per-entry limits.  Diagnostic
 * storage is bounded independently from the rewindable operation arena. */

typedef struct ctool_arena ctool_arena_t;
typedef struct ctool_buffer ctool_buffer_t;
typedef struct ctool_job ctool_job_t;

typedef struct {
  const void *owner;
  void *block;
  ctool_u32 used;
  ctool_u32 generation;
} ctool_arena_mark_t;

typedef struct {
  ctool_string_t text;
} ctool_path_t;

typedef struct {
  ctool_path_t path;
  ctool_bytes_t contents;
} ctool_source_t;

typedef enum {
  CTOOL_DIAG_NOTE = 0,
  CTOOL_DIAG_WARNING,
  CTOOL_DIAG_ERROR,
  CTOOL_DIAG_FATAL
} ctool_diag_severity_t;

typedef struct {
  ctool_diag_severity_t severity;
  ctool_u32 code;
  ctool_string_t path;
  ctool_u32 line;
  ctool_u32 column;
  ctool_string_t message;
} ctool_diagnostic_t;

typedef struct {
  ctool_allocator_t allocator;
  ctool_file_store_t files;
  ctool_text_sink_t diagnostics;
  ctool_limits_t limits;
} ctool_job_config_t;

typedef struct {
  ctool_string_t input_path;
  ctool_string_t output_path;
} ctool_invocation_request_t;

typedef struct {
  ctool_job_t *job;
  const ctool_source_t *input;
  ctool_buffer_t *output;
} ctool_invocation_t;

typedef ctool_status_t (*ctool_invocation_body_t)(ctool_invocation_t *invocation,
                                                  void *user_data);

typedef struct {
  ctool_status_t body_status;
  ctool_u32 input_bytes;
  ctool_u32 output_bytes;
  ctool_u32 diagnostic_count;
  ctool_bool output_committed;
} ctool_invocation_result_t;

ctool_string_t ctool_string(const char *text);
ctool_bytes_t ctool_bytes(const void *data, ctool_u32 size);
ctool_limits_t ctool_default_limits(void);
const char *ctool_status_name(ctool_status_t status);

ctool_status_t ctool_arena_open(ctool_allocator_t allocator,
                                ctool_u32 block_bytes,
                                ctool_u32 byte_limit,
                                ctool_arena_t **arena_out);
void ctool_arena_close(ctool_arena_t *arena);
ctool_status_t ctool_arena_alloc(ctool_arena_t *arena, ctool_u32 bytes,
                                 ctool_u32 alignment, void **allocation_out);
ctool_status_t ctool_arena_alloc_zero(ctool_arena_t *arena, ctool_u32 count,
                                      ctool_u32 element_bytes,
                                      ctool_u32 alignment,
                                      void **allocation_out);
ctool_arena_mark_t ctool_arena_mark(const ctool_arena_t *arena);
ctool_status_t ctool_arena_rewind(ctool_arena_t *arena,
                                  ctool_arena_mark_t mark);
ctool_status_t ctool_arena_copy_bytes(ctool_arena_t *arena,
                                      ctool_bytes_t input,
                                      ctool_bytes_t *copy_out);
ctool_status_t ctool_arena_copy_string(ctool_arena_t *arena,
                                       ctool_string_t input,
                                       ctool_string_t *copy_out);

/* Arena allocations are aligned and initially zero.  Marks belong to one
 * arena, may be nested, and remain usable while their block/offset still
 * exists.  All arena-owned views expire when rewound past or closed. */

ctool_status_t ctool_buffer_open(ctool_allocator_t allocator,
                                 ctool_u32 initial_capacity,
                                 ctool_u32 byte_limit,
                                 ctool_buffer_t **buffer_out);
void ctool_buffer_close(ctool_buffer_t *buffer);
void ctool_buffer_clear(ctool_buffer_t *buffer);
ctool_bytes_t ctool_buffer_view(const ctool_buffer_t *buffer);
ctool_u32 ctool_buffer_mark(const ctool_buffer_t *buffer);
ctool_status_t ctool_buffer_rewind(ctool_buffer_t *buffer, ctool_u32 mark);
ctool_status_t ctool_buffer_reserve_zero(ctool_buffer_t *buffer,
                                         ctool_u32 bytes,
                                         ctool_u32 *offset_out,
                                         ctool_mut_bytes_t *reserved_out);
ctool_status_t ctool_buffer_append(ctool_buffer_t *buffer,
                                   ctool_bytes_t bytes);
ctool_status_t ctool_buffer_fill(ctool_buffer_t *buffer, ctool_u8 value,
                                 ctool_u32 count);
ctool_status_t ctool_buffer_put_u8(ctool_buffer_t *buffer, ctool_u8 value);
ctool_status_t ctool_buffer_put_le16(ctool_buffer_t *buffer, ctool_u16 value);
ctool_status_t ctool_buffer_put_le32(ctool_buffer_t *buffer, ctool_u32 value);
ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, ctool_u64 value);
ctool_status_t ctool_buffer_patch_u8(ctool_buffer_t *buffer,
                                     ctool_u32 offset, ctool_u8 value);
ctool_status_t ctool_buffer_patch_le16(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u16 value);
ctool_status_t ctool_buffer_patch_le32(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u32 value);
ctool_status_t ctool_buffer_patch_le64(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u64 value);

/* Buffers are explicit owned handles: the caller that opens one closes it.
 * Append accepts a view into the same buffer, including across growth.
 * Growth, reserve, and patch failures leave the existing byte sequence
 * unchanged; multi-byte helpers always use little-endian order. */

ctool_status_t ctool_path_root(ctool_arena_t *arena, ctool_path_t *path_out);
ctool_status_t ctool_path_resolve(ctool_arena_t *arena,
                                  const ctool_path_t *base_directory,
                                  ctool_string_t spelling,
                                  ctool_u32 max_bytes,
                                  ctool_path_t *path_out);
ctool_status_t ctool_path_parent(const ctool_path_t *path,
                                 ctool_path_t *parent_out);
ctool_string_t ctool_path_basename(const ctool_path_t *path);
ctool_bool ctool_path_equal(const ctool_path_t *left,
                            const ctool_path_t *right);
ctool_bool ctool_path_is_canonical(const ctool_path_t *path);

/* Path outputs are arena-owned, canonical, absolute logical paths.  They use
 * '/' regardless of host syntax, collapse '.' and duplicate separators, and
 * reject '..' that would escape the logical root. */

ctool_status_t ctool_job_open(const ctool_job_config_t *config,
                              ctool_job_t **job_out);
void ctool_job_close(ctool_job_t *job);
ctool_arena_t *ctool_job_arena(ctool_job_t *job);
ctool_status_t ctool_job_open_buffer(ctool_job_t *job,
                                     ctool_u32 initial_capacity,
                                     ctool_u32 byte_limit,
                                     ctool_buffer_t **buffer_out);
ctool_status_t ctool_job_load_source(ctool_job_t *job,
                                     const ctool_path_t *path,
                                     ctool_source_t *source_out);
ctool_status_t ctool_job_write(ctool_job_t *job, const ctool_path_t *path,
                               ctool_bytes_t contents);
ctool_status_t ctool_job_emit(ctool_job_t *job,
                              const ctool_diagnostic_t *diagnostic);
ctool_u32 ctool_job_diagnostic_count(const ctool_job_t *job);
const ctool_diagnostic_t *ctool_job_diagnostic(const ctool_job_t *job,
                                               ctool_u32 index);
ctool_bool ctool_job_has_errors(const ctool_job_t *job);
ctool_status_t ctool_job_render_diagnostics(const ctool_job_t *job);

/* Job source/path views are arena-owned and live until job close or an arena
 * rewind past them.  Diagnostic views live until job close and survive every
 * arena rewind.  Buffers opened through a job still have explicit caller
 * ownership and must close before the job's adapter context becomes invalid. */

ctool_status_t ctool_invoke(const ctool_job_config_t *config,
                            const ctool_invocation_request_t *request,
                            ctool_invocation_body_t body, void *user_data,
                            ctool_invocation_result_t *result_out);

/* Invocation writes its buffered output only after the body returns OK and no
 * error/fatal diagnostic exists.  This is a semantic commit gate, not an
 * atomic filesystem-replacement guarantee. */

#endif
