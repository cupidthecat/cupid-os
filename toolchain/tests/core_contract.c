#include "ctool.h"
#include "ctool_host.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  void *allocation;
  ctool_u32 bytes;
} recycling_allocator_t;

static void *recycling_allocate(void *context, ctool_u32 bytes) {
  recycling_allocator_t *allocator = (recycling_allocator_t *)context;
  if (allocator->allocation != (void *)0 && allocator->bytes == bytes) {
    void *allocation = allocator->allocation;
    allocator->allocation = (void *)0;
    allocator->bytes = 0u;
    return allocation;
  }
  return malloc((size_t)bytes);
}

static void recycling_release(void *context, void *allocation,
                              ctool_u32 bytes) {
  recycling_allocator_t *allocator = (recycling_allocator_t *)context;
  if (allocator->allocation == (void *)0) {
    allocator->allocation = allocation;
    allocator->bytes = bytes;
  } else {
    free(allocation);
  }
}

static void recycling_dispose(recycling_allocator_t *allocator) {
  free(allocator->allocation);
  allocator->allocation = (void *)0;
  allocator->bytes = 0u;
}

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *operation) {
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected %s, got %s\n", operation,
                  ctool_status_name(expected), ctool_status_name(actual));
    return 0;
  }
  return 1;
}

static int check_string(ctool_string_t actual, const char *expected,
                        const char *operation) {
  size_t size = strlen(expected);
  if ((size_t)actual.size != size ||
      memcmp(actual.data, expected, size) != 0) {
    (void)fprintf(stderr, "%s: unexpected string\n", operation);
    return 0;
  }
  return 1;
}

static int open_host_job(const char *root, ctool_limits_t limits,
                         ctool_host_adapter_t *adapter,
                         ctool_job_config_t *config, ctool_job_t **job) {
  ctool_status_t status = ctool_host_adapter_init(adapter, root);
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  *config = ctool_host_job_config(adapter, limits);
  status = ctool_job_open(config, job);
  return check_status(status, CTOOL_OK, "job open");
}

static int run_foundations(void) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_buffer_t *buffer;
  ctool_bytes_t view;
  ctool_mut_bytes_t reserved;
  ctool_u32 offset;
  void *allocation;
  ctool_u8 *bytes;
  ctool_u32 index;
  ctool_status_t status;
  const ctool_u8 expected[] = {'A', 'B', 0x78u, 0x56u, 0x34u, 0x12u};
  if (!open_host_job(".", ctool_default_limits(), &adapter, &config, &job)) {
    return 1;
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  status = ctool_arena_alloc(arena, 17u, 16u, &allocation);
  if (!check_status(status, CTOOL_OK, "arena allocation") ||
      ((uintptr_t)allocation & (uintptr_t)15u) != (uintptr_t)0u) {
    ctool_job_close(job);
    return 1;
  }
  bytes = (ctool_u8 *)allocation;
  for (index = 0u; index < 17u; index++) {
    if (bytes[index] != 0u) {
      (void)fprintf(stderr, "arena allocation was not zeroed\n");
      ctool_job_close(job);
      return 1;
    }
    bytes[index] = 0xa5u;
  }
  status = ctool_arena_rewind(arena, mark);
  if (!check_status(status, CTOOL_OK, "arena rewind")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_arena_alloc_zero(arena, 17u, 1u, 16u, &allocation);
  if (!check_status(status, CTOOL_OK, "zero arena allocation")) {
    ctool_job_close(job);
    return 1;
  }
  bytes = (ctool_u8 *)allocation;
  for (index = 0u; index < 17u; index++) {
    if (bytes[index] != 0u) {
      (void)fprintf(stderr, "rewound arena allocation was not zeroed\n");
      ctool_job_close(job);
      return 1;
    }
  }
  status = ctool_job_open_buffer(job, 2u, 64u, &buffer);
  if (!check_status(status, CTOOL_OK, "buffer open")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_buffer_append(buffer, ctool_bytes("AB", 2u));
  if (status == CTOOL_OK) {
    status = ctool_buffer_reserve_zero(buffer, 4u, &offset, &reserved);
  }
  if (status == CTOOL_OK && (offset != 2u || reserved.size != 4u)) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(buffer, offset, 0x12345678u);
  }
  view = ctool_buffer_view(buffer);
  if (!check_status(status, CTOOL_OK, "buffer emit") ||
      view.size != (ctool_u32)sizeof(expected) ||
      memcmp(view.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "buffer bytes differ\n");
    ctool_buffer_close(buffer);
    ctool_job_close(job);
    return 1;
  }
  status = ctool_buffer_append(buffer, view);
  view = ctool_buffer_view(buffer);
  if (!check_status(status, CTOOL_OK, "buffer self append") ||
      view.size != (ctool_u32)(sizeof(expected) * 2u) ||
      memcmp(view.data, expected, sizeof(expected)) != 0 ||
      memcmp(view.data + sizeof(expected), expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "buffer self-append bytes differ\n");
    ctool_buffer_close(buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(buffer);
  ctool_job_close(job);
  {
    recycling_allocator_t recycling;
    ctool_allocator_t allocator;
    ctool_arena_t *recycling_arena;
    ctool_arena_mark_t empty_mark;
    ctool_arena_mark_t stale_mark;
    void *first;
    void *second;
    recycling.allocation = (void *)0;
    recycling.bytes = 0u;
    allocator.context = &recycling;
    allocator.allocate = recycling_allocate;
    allocator.release = recycling_release;
    status = ctool_arena_open(allocator, 64u, 128u, &recycling_arena);
    if (!check_status(status, CTOOL_OK, "recycling arena open")) {
      recycling_dispose(&recycling);
      return 1;
    }
    empty_mark = ctool_arena_mark(recycling_arena);
    status = ctool_arena_alloc(recycling_arena, 48u, 16u, &first);
    if (status == CTOOL_OK) {
      (void)memset(first, 0xa5, 48u);
      stale_mark = ctool_arena_mark(recycling_arena);
      status = ctool_arena_rewind(recycling_arena, empty_mark);
    }
    if (status == CTOOL_OK) {
      status = ctool_arena_alloc(recycling_arena, 48u, 16u, &second);
    }
    if (!check_status(status, CTOOL_OK, "recycled arena allocation") ||
        first != second) {
      ctool_arena_close(recycling_arena);
      recycling_dispose(&recycling);
      return 1;
    }
    bytes = (ctool_u8 *)second;
    for (index = 0u; index < 48u; index++) {
      if (bytes[index] != 0u) {
        (void)fprintf(stderr, "recycled arena block was not zeroed\n");
        ctool_arena_close(recycling_arena);
        recycling_dispose(&recycling);
        return 1;
      }
    }
    status = ctool_arena_rewind(recycling_arena, stale_mark);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "stale arena mark")) {
      ctool_arena_close(recycling_arena);
      recycling_dispose(&recycling);
      return 1;
    }
    ctool_arena_close(recycling_arena);
    recycling_dispose(&recycling);
  }
  (void)puts("foundations: ok");
  return 0;
}

static int run_paths(void) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_arena_t *arena;
  ctool_path_t root;
  ctool_path_t base;
  ctool_path_t path;
  ctool_path_t parent;
  ctool_arena_mark_t outer_mark;
  ctool_status_t status;
  if (!open_host_job(".", ctool_default_limits(), &adapter, &config, &job)) {
    return 1;
  }
  arena = ctool_job_arena(job);
  status = ctool_path_root(arena, &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(arena, &root,
                                ctool_string("src//./front/../main.c"), 128u,
                                &path);
  }
  if (!check_status(status, CTOOL_OK, "first path") ||
      !check_string(path.text, "/src/main.c", "first path text")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_path_resolve(arena, &root, ctool_string("/src/include"),
                              128u, &base);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(arena, &base,
                                ctool_string(".././shared\\types.h"), 128u,
                                &path);
  }
  if (!check_status(status, CTOOL_OK, "second path") ||
      !check_string(path.text, "/src/shared/types.h", "second path text")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_path_parent(&path, &parent);
  if (!check_status(status, CTOOL_OK, "path parent") ||
      !check_string(parent.text, "/src/shared", "parent text") ||
      !check_string(ctool_path_basename(&path), "types.h", "basename")) {
    ctool_job_close(job);
    return 1;
  }
  outer_mark = ctool_arena_mark(arena);
  status = ctool_path_resolve(arena, &base, ctool_string("../../../escape"),
                              128u, &path);
  if (!check_status(status, CTOOL_ERR_PATH_ESCAPE, "path escape")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_arena_rewind(arena, outer_mark);
  if (!check_status(status, CTOOL_OK, "outer arena rewind")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("paths: ok");
  return 0;
}

static int run_diagnostics(void) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_diagnostic_t diagnostic;
  void *sentinel;
  ctool_status_t status;
  if (!open_host_job(".", ctool_default_limits(), &adapter, &config, &job)) {
    return 1;
  }
  arena = ctool_job_arena(job);
  status = ctool_arena_alloc(arena, 16u, 1u, &sentinel);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  mark = ctool_arena_mark(arena);
  diagnostic.severity = CTOOL_DIAG_WARNING;
  diagnostic.code = 0x1000001u;
  diagnostic.path = ctool_string("/src/main.c");
  diagnostic.line = 4u;
  diagnostic.column = 7u;
  diagnostic.message = ctool_string("first warning");
  status = ctool_job_emit(job, &diagnostic);
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = 0x1000002u;
  diagnostic.line = 5u;
  diagnostic.column = 2u;
  diagnostic.message = ctool_string("then error");
  if (status == CTOOL_OK) {
    status = ctool_job_emit(job, &diagnostic);
  }
  if (!check_status(status, CTOOL_OK, "diagnostic emit") ||
      ctool_job_diagnostic_count(job) != 2u ||
      ctool_job_diagnostic(job, 0u)->code != 0x1000001u ||
      ctool_job_has_errors(job) == CTOOL_FALSE) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_arena_rewind(arena, mark);
  if (!check_status(status, CTOOL_OK, "diagnostic-independent rewind") ||
      !check_string(ctool_job_diagnostic(job, 0u)->path, "/src/main.c",
                    "rewound diagnostic path") ||
      !check_string(ctool_job_diagnostic(job, 0u)->message, "first warning",
                    "rewound first diagnostic") ||
      !check_string(ctool_job_diagnostic(job, 1u)->message, "then error",
                    "rewound second diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_job_render_diagnostics(job);
  if (!check_status(status, CTOOL_OK, "diagnostic render")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("diagnostics: ok");
  return 0;
}

static int run_host_io(const char *root_name) {
  static const ctool_u8 payload[] = {
      'C', 'u', 'p', 'i', 'd', ' ', 't', 'o', 'o', 'l', 'c', 'h', 'a', 'i', 'n',
      ' ', 'c', 'o', 'r', 'e', 0x00u, 0x7fu, 0xffu};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_path_t root;
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  if (!open_host_job(root_name, ctool_default_limits(), &adapter, &config,
                     &job)) {
    return 1;
  }
  status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string("nested/roundtrip.bin"), 128u,
                                &path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_write(job, &path,
                             ctool_bytes(payload, (ctool_u32)sizeof(payload)));
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &path, &source);
  }
  if (!check_status(status, CTOOL_OK, "host file roundtrip") ||
      source.contents.size != (ctool_u32)sizeof(payload) ||
      memcmp(source.contents.data, payload, sizeof(payload)) != 0 ||
      source.contents.data[source.contents.size] != 0u) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("host-io: ok");
  return 0;
}

static int run_missing(const char *root_name) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_path_t root;
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  if (!open_host_job(root_name, ctool_default_limits(), &adapter, &config,
                     &job)) {
    return 1;
  }
  status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string("definitely-missing.cc"), 128u,
                                &path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &path, &source);
  }
  if (!check_status(status, CTOOL_ERR_NOT_FOUND, "missing host input")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("missing: ok");
  return 0;
}

static int run_limits(void) {
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *buffer;
  ctool_path_t root;
  ctool_path_t path;
  ctool_diagnostic_t diagnostic;
  ctool_status_t status;
  ctool_u32 before;
  void *allocation;
  limits.diagnostic_count = 1u;
  if (!open_host_job(".", limits, &adapter, &config, &job)) {
    return 1;
  }
  status = ctool_arena_alloc(ctool_job_arena(job), 4u, 3u, &allocation);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "bad alignment")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_job_open_buffer(job, 2u, 4u, &buffer);
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(buffer, ctool_bytes("1234", 4u));
  }
  before = ctool_buffer_view(buffer).size;
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(buffer, 5u);
  }
  if (!check_status(status, CTOOL_ERR_LIMIT, "buffer limit") || before != 4u ||
      ctool_buffer_view(buffer).size != before) {
    ctool_buffer_close(buffer);
    ctool_job_close(job);
    return 1;
  }
  status = ctool_buffer_patch_le32(buffer, 2u, 1u);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "patch bounds")) {
    ctool_buffer_close(buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(buffer);
  status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string("too-long"), 3u, &path);
  }
  if (!check_status(status, CTOOL_ERR_LIMIT, "path limit")) {
    ctool_job_close(job);
    return 1;
  }
  diagnostic.severity = CTOOL_DIAG_NOTE;
  diagnostic.code = 1u;
  diagnostic.path = ctool_string("/limit");
  diagnostic.line = 1u;
  diagnostic.column = 1u;
  diagnostic.message = ctool_string("one");
  status = ctool_job_emit(job, &diagnostic);
  if (status == CTOOL_OK) {
    status = ctool_job_emit(job, &diagnostic);
  }
  if (!check_status(status, CTOOL_ERR_LIMIT, "diagnostic limit") ||
      ctool_job_diagnostic_count(job) != 1u) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("limits: ok");
  return 0;
}

static ctool_status_t invocation_success(ctool_invocation_t *invocation,
                                         void *user_data) {
  ctool_status_t status;
  (void)user_data;
  status = ctool_buffer_append(invocation->output, invocation->input->contents);
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(invocation->output,
                                 ctool_bytes(":built", 6u));
  }
  return status;
}

static ctool_status_t invocation_error(ctool_invocation_t *invocation,
                                       void *user_data) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t status;
  (void)user_data;
  status = ctool_buffer_append(invocation->output,
                               ctool_bytes("must-not-commit", 15u));
  if (status != CTOOL_OK) {
    return status;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = 0x1000003u;
  diagnostic.path = invocation->input->path.text;
  diagnostic.line = 1u;
  diagnostic.column = 1u;
  diagnostic.message = ctool_string("deliberate failure");
  return ctool_job_emit(invocation->job, &diagnostic);
}

static int run_invocation(const char *root_name) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_invocation_request_t request;
  ctool_invocation_result_t result;
  ctool_status_t status;
  if (!check_status(ctool_host_adapter_init(&adapter, root_name), CTOOL_OK,
                    "invocation adapter")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  request.input_path = ctool_string("/input.txt");
  request.output_path = ctool_string("/nested/success.bin");
  status = ctool_invoke(&config, &request, invocation_success, (void *)0,
                        &result);
  if (!check_status(status, CTOOL_OK, "successful invocation") ||
      result.body_status != CTOOL_OK ||
      result.output_committed == CTOOL_FALSE || result.input_bytes != 6u ||
      result.output_bytes != 12u) {
    return 1;
  }
  request.output_path = ctool_string("/nested/error.bin");
  status =
      ctool_invoke(&config, &request, invocation_error, (void *)0, &result);
  if (!check_status(status, CTOOL_ERR_INPUT, "failed invocation") ||
      result.body_status != CTOOL_OK ||
      result.output_committed != CTOOL_FALSE || result.diagnostic_count != 1u) {
    return 1;
  }
  (void)puts("invocation: ok");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "foundations") == 0) {
    return run_foundations();
  }
  if (argc == 2 && strcmp(argv[1], "paths") == 0) {
    return run_paths();
  }
  if (argc == 2 && strcmp(argv[1], "diagnostics") == 0) {
    return run_diagnostics();
  }
  if (argc == 3 && strcmp(argv[1], "host-io") == 0) {
    return run_host_io(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "missing") == 0) {
    return run_missing(argv[2]);
  }
  if (argc == 2 && strcmp(argv[1], "limits") == 0) {
    return run_limits();
  }
  if (argc == 3 && strcmp(argv[1], "invocation") == 0) {
    return run_invocation(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: core-contract foundations|paths|diagnostics|limits|"
                "host-io ROOT|missing ROOT|invocation ROOT\n");
  return 2;
}
