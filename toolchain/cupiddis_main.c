#include "ctool.h"
#include "ctool_host.h"
#include "cupiddis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDDIS_HOST_SOURCE_BYTES 67108864u
#define CUPIDDIS_HOST_ARENA_BYTES 134217728u

typedef struct {
  ctool_bool raw;
  ctool_bool nm;
  ctool_bool have_mode;
  ctool_bool have_base;
  ctool_bool have_views;
  ctool_x86_mode_t mode;
  ctool_u32 base_address;
  ctool_u32 views;
  ctool_dis_raw_range_t *mode_ranges;
  ctool_u32 mode_change_count;
  ctool_u32 mode_range_capacity;
  const char *input;
} cupiddis_cli_t;

static void cupiddis_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupiddis [--headers] [--sections] [--symbols] "
      "[--relocations] [--disassemble] [--all] [--nm] FILE\n"
      "       cupiddis --raw --mode 16|32 "
      "[--mode-at OFFSET:16|32]... --base ADDRESS FILE\n");
}

static int cupiddis_parse_u32_span(const char *text, size_t size,
                                   ctool_u32 *value_out) {
  ctool_u32 base = 10u;
  ctool_u32 value = 0u;
  size_t index = 0u;
  if (text == (const char *)0 || size == 0u ||
      value_out == (ctool_u32 *)0) {
    return 0;
  }
  if (size >= 2u && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16u;
    index = 2u;
    if (index == size) {
      return 0;
    }
  }
  while (index < size) {
    ctool_u32 digit;
    char character = text[index];
    if (character >= '0' && character <= '9') {
      digit = (ctool_u32)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = 10u + (ctool_u32)(character - 'a');
    } else if (character >= 'A' && character <= 'F') {
      digit = 10u + (ctool_u32)(character - 'A');
    } else {
      return 0;
    }
    if (digit >= base || value > (4294967295u - digit) / base) {
      return 0;
    }
    value = value * base + digit;
    index++;
  }
  *value_out = value;
  return 1;
}

static int cupiddis_parse_u32(const char *text, ctool_u32 *value_out) {
  return text == (const char *)0
             ? 0
             : cupiddis_parse_u32_span(text, strlen(text), value_out);
}

static int cupiddis_parse_mode(const char *text, ctool_x86_mode_t *mode_out) {
  if (text == (const char *)0 || mode_out == (ctool_x86_mode_t *)0) {
    return 0;
  }
  if (strcmp(text, "16") == 0) {
    *mode_out = CTOOL_X86_MODE_16;
    return 1;
  }
  if (strcmp(text, "32") == 0) {
    *mode_out = CTOOL_X86_MODE_32;
    return 1;
  }
  return 0;
}

static int cupiddis_parse_mode_change(const char *text,
                                      ctool_dis_raw_range_t *range_out) {
  const char *separator;
  size_t offset_size;
  if (text == (const char *)0 || range_out == (ctool_dis_raw_range_t *)0) {
    return 0;
  }
  separator = strchr(text, ':');
  if (separator == (const char *)0 || separator == text ||
      separator[1] == '\0' || strchr(separator + 1, ':') != (char *)0) {
    return 0;
  }
  offset_size = (size_t)(separator - text);
  return cupiddis_parse_u32_span(text, offset_size, &range_out->offset) != 0 &&
                 cupiddis_parse_mode(separator + 1, &range_out->mode) != 0
             ? 1
             : 0;
}

static int cupiddis_append_mode_change(cupiddis_cli_t *cli,
                                       ctool_dis_raw_range_t range) {
  ctool_u32 required;
  if (cli->mode_change_count > 4294967293u) {
    return 0;
  }
  required = cli->mode_change_count + 2u;
  if (required > cli->mode_range_capacity) {
    ctool_u32 capacity = cli->mode_range_capacity == 0u
                             ? 4u
                             : cli->mode_range_capacity;
    ctool_dis_raw_range_t *resized;
    size_t allocation_size;
    while (capacity < required) {
      if (capacity > 2147483647u) {
        capacity = required;
        break;
      }
      capacity *= 2u;
    }
    allocation_size = (size_t)capacity * sizeof(*resized);
    if (capacity != 0u &&
        allocation_size / sizeof(*resized) != (size_t)capacity) {
      return 0;
    }
    resized = (ctool_dis_raw_range_t *)realloc(
        cli->mode_ranges, allocation_size);
    if (resized == (ctool_dis_raw_range_t *)0) {
      return 0;
    }
    cli->mode_ranges = resized;
    cli->mode_range_capacity = capacity;
  }
  cli->mode_ranges[cli->mode_change_count + 1u] = range;
  cli->mode_change_count++;
  return 1;
}

static int cupiddis_take_value(int argc, char **argv, int *index,
                               const char *argument, const char *prefix,
                               const char **value_out) {
  size_t prefix_size = strlen(prefix);
  if (strncmp(argument, prefix, prefix_size) == 0 &&
      argument[prefix_size] == '=') {
    *value_out = argument + prefix_size + 1u;
    return 1;
  }
  if (strcmp(argument, prefix) == 0) {
    if (*index + 1 >= argc) {
      return -1;
    }
    *index = *index + 1;
    *value_out = argv[*index];
    return 1;
  }
  return 0;
}

static int cupiddis_parse_cli(int argc, char **argv, cupiddis_cli_t *cli) {
  int index;
  (void)memset(cli, 0, sizeof(*cli));
  for (index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    if (strcmp(argument, "--raw") == 0) {
      cli->raw = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--nm") == 0 || strcmp(argument, "-n") == 0) {
      cli->nm = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--headers") == 0) {
      cli->views |= CTOOL_DIS_VIEW_HEADER;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--sections") == 0) {
      cli->views |= CTOOL_DIS_VIEW_SECTIONS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--symbols") == 0) {
      cli->views |= CTOOL_DIS_VIEW_SYMBOLS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--relocations") == 0) {
      cli->views |= CTOOL_DIS_VIEW_RELOCATIONS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--disassemble") == 0) {
      cli->views |= CTOOL_DIS_VIEW_DISASSEMBLY;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--all") == 0) {
      cli->views = CTOOL_DIS_VIEW_ALL;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--mode",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->have_mode == CTOOL_TRUE ||
          cupiddis_parse_mode(value, &cli->mode) == 0) {
        return 0;
      }
      cli->have_mode = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--base",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->have_base == CTOOL_TRUE ||
          cupiddis_parse_u32(value, &cli->base_address) == 0) {
        return 0;
      }
      cli->have_base = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--mode-at",
                                &value);
    if (taken != 0) {
      ctool_dis_raw_range_t range;
      if (taken < 0 || cupiddis_parse_mode_change(value, &range) == 0 ||
          cupiddis_append_mode_change(cli, range) == 0) {
        return 0;
      }
      continue;
    }
    if (argument[0] == '-') {
      return 0;
    }
    if (cli->input != (const char *)0) {
      return 0;
    }
    cli->input = argument;
  }
  if (cli->input == (const char *)0) {
    return 0;
  }
  if (cli->raw == CTOOL_TRUE) {
    if (cli->nm == CTOOL_TRUE || cli->have_mode == CTOOL_FALSE ||
        cli->have_base == CTOOL_FALSE ||
        (cli->have_views == CTOOL_TRUE &&
         cli->views != CTOOL_DIS_VIEW_DISASSEMBLY)) {
      return 0;
    }
    if (cli->have_views == CTOOL_FALSE) {
      cli->views = CTOOL_DIS_VIEW_DISASSEMBLY;
    }
    if (cli->mode_change_count != 0u) {
      cli->mode_ranges[0].offset = 0u;
      cli->mode_ranges[0].mode = cli->mode;
    }
  } else {
    if (cli->have_mode == CTOOL_TRUE || cli->have_base == CTOOL_TRUE ||
        cli->mode_change_count != 0u ||
        (cli->nm == CTOOL_TRUE && cli->have_views == CTOOL_TRUE)) {
      return 0;
    }
    if (cli->nm == CTOOL_TRUE) {
      cli->views = CTOOL_DIS_VIEW_SYMBOLS;
    } else if (cli->have_views == CTOOL_FALSE) {
      cli->views = CTOOL_DIS_VIEW_ALL;
    }
  }
  return 1;
}

static ctool_status_t cupiddis_stdout_write(void *context,
                                            ctool_bytes_t text) {
  FILE *stream = (FILE *)context;
  size_t written;
  if (text.size == 0u) {
    return CTOOL_OK;
  }
  written = fwrite(text.data, 1u, (size_t)text.size, stream);
  return written == (size_t)text.size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t cupiddis_flush_output(ctool_job_t *job,
                                             ctool_string_t path) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  if (fflush(stdout) == 0 && ferror(stdout) == 0) {
    return CTOOL_OK;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = CTOOL_DIS_DIAG_OUTPUT;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = 0u;
  diagnostic.message =
      ctool_string("CupidDis could not complete report output");
  emitted = ctool_job_emit(job, &diagnostic);
  return emitted == CTOOL_OK ? CTOOL_ERR_IO : emitted;
}

static int cupiddis_split_path(const char *path, char **root_out,
                               const char **name_out) {
  size_t size = strlen(path);
  size_t separator = size;
  size_t root_size;
  char *root;
  while (separator != 0u) {
    char character = path[separator - 1u];
    if (character == '/' || character == '\\') {
      separator--;
      break;
    }
    separator--;
  }
  if (size == 0u || (path[size - 1u] == '/' || path[size - 1u] == '\\')) {
    return 0;
  }
  if (separator == 0u && path[0] != '/' && path[0] != '\\') {
    root = (char *)malloc(2u);
    if (root == (char *)0) {
      return 0;
    }
    root[0] = '.';
    root[1] = '\0';
    *name_out = path;
  } else {
    root_size = separator;
    if (separator == 0u ||
        (separator == 2u && path[1] == ':')) {
      root_size++;
    }
    root = (char *)malloc(root_size + 1u);
    if (root == (char *)0) {
      return 0;
    }
    (void)memcpy(root, path, root_size);
    root[root_size] = '\0';
    *name_out = path + separator + 1u;
  }
  *root_out = root;
  return 1;
}

static int cupiddis_is_elf(ctool_bytes_t bytes) {
  return bytes.size >= 4u && bytes.data[0] == 0x7fu &&
                 bytes.data[1] == (ctool_u8)'E' &&
                 bytes.data[2] == (ctool_u8)'L' &&
                 bytes.data[3] == (ctool_u8)'F'
             ? 1
             : 0;
}

int main(int argc, char **argv) {
  cupiddis_cli_t cli;
  char *native_root = (char *)0;
  const char *logical_name = (const char *)0;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_path_t root;
  ctool_path_t input_path;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_text_sink_t output;
  ctool_status_t status;
  int parsed = cupiddis_parse_cli(argc, argv, &cli);
  int exit_code = 1;
  if (parsed < 0) {
    cupiddis_usage(stdout);
    free(cli.mode_ranges);
    return 0;
  }
  if (parsed == 0) {
    cupiddis_usage(stderr);
    free(cli.mode_ranges);
    return 2;
  }
  if (!cupiddis_split_path(cli.input, &native_root, &logical_name)) {
    (void)fprintf(stderr, "cupiddis: invalid input path\n");
    free(cli.mode_ranges);
    return 1;
  }
  limits.source_bytes = CUPIDDIS_HOST_SOURCE_BYTES;
  limits.arena_bytes = CUPIDDIS_HOST_ARENA_BYTES;
  status = ctool_host_adapter_init(&adapter, native_root);
  config = ctool_host_job_config(&adapter, limits);
  if (status == CTOOL_OK) {
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_root(ctool_job_arena(job), &root);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string(logical_name), limits.path_bytes,
                                &input_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &input_path, &source);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupiddis: cannot load %s (%s)\n", cli.input,
                  ctool_status_name(status));
    goto done;
  }
  if (cli.raw == CTOOL_FALSE && !cupiddis_is_elf(source.contents)) {
    (void)fprintf(stderr,
                  "cupiddis: input is not ELF32; raw input requires --raw\n");
    goto done;
  }
  (void)memset(&request, 0, sizeof(request));
  request.input = cli.raw == CTOOL_TRUE ? CTOOL_DIS_INPUT_RAW
                                        : CTOOL_DIS_INPUT_ELF32;
  request.views = cli.views;
  request.raw_mode = cli.mode_change_count == 0u
                         ? cli.mode
                         : CTOOL_DIS_RAW_MODE_MAP;
  request.raw_base_address = cli.base_address;
  if (cli.mode_change_count != 0u) {
    request.raw_ranges = cli.mode_ranges;
    request.raw_range_count = cli.mode_change_count + 1u;
  }
  status = ctool_dis_inspect(job, &source, &request, &report);
  output.context = stdout;
  output.write = cupiddis_stdout_write;
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report,
                              cli.nm == CTOOL_TRUE ? CTOOL_DIS_TEXT_NM
                                                   : CTOOL_DIS_TEXT_CUPID,
                              output);
  }
  if (status == CTOOL_OK) {
    status = cupiddis_flush_output(job, source.path.text);
  }
  if (status != CTOOL_OK) {
    if (ctool_job_diagnostic_count(job) != 0u) {
      (void)ctool_job_render_diagnostics(job);
    } else {
      (void)fprintf(stderr, "cupiddis: inspection failed (%s)\n",
                    ctool_status_name(status));
    }
    goto done;
  }
  exit_code = 0;

done:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(native_root);
  free(cli.mode_ranges);
  return exit_code;
}
