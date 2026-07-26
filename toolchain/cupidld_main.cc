#include "ctool.h"
#include "ctool_host.h"
#include "cupidld.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#define CUPIDLD_HOST_SOURCE_BYTES 67108864u
#define CUPIDLD_HOST_OUTPUT_BYTES 67108864u
#define CUPIDLD_HOST_ARENA_BYTES 268435456u
#define CUPIDLD_HOST_IMAGE_SPAN 67108864u

typedef struct {
  const char *machine;
  const char *script;
  const char *output;
  const char *entry;
  ctool_u32 text_address;
  ctool_bool have_text_address;
  const char **objects;
  ctool_u32 object_count;
} cupidld_cli_t;

static void cupidld_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidld -m elf_i386 -T SCRIPT -o OUTPUT OBJECT...\n"
      "       cupidld -m elf_i386 --text-address ADDRESS --entry SYMBOL "
      "-o OUTPUT OBJECT...\n");
}

static int cupidld_take_value(int argc, char **argv, int *index,
                              const char *argument, const char *option,
                              const char **value_out) {
  size_t option_size = strlen(option);
  if (strcmp(argument, option) == 0) {
    if (*index + 1 >= argc) {
      return -1;
    }
    *index = *index + 1;
    *value_out = argv[*index];
    return 1;
  }
  if (strncmp(argument, option, option_size) == 0 &&
      argument[option_size] == '=') {
    *value_out = argument + option_size + 1u;
    return 1;
  }
  return 0;
}

static int cupidld_parse_u32(const char *text, ctool_u32 *value_out) {
  ctool_u32 base = 10u;
  ctool_u32 value = 0u;
  ctool_u32 index = 0u;
  if (text == (const char *)0 || text[0] == '\0' ||
      value_out == (ctool_u32 *)0) {
    return 0;
  }
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16u;
    index = 2u;
    if (text[index] == '\0') {
      return 0;
    }
  }
  while (text[index] != '\0') {
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

static int cupidld_parse_cli(int argc, char **argv, cupidld_cli_t *cli) {
  const char **objects = cli->objects;
  int index;
  (void)memset(cli, 0, sizeof(*cli));
  cli->objects = objects;
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    return -1;
  }
  for (index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    taken = cupidld_take_value(argc, argv, &index, argument, "-m", &value);
    if (taken != 0) {
      if (taken < 0 || cli->machine != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->machine = value;
      continue;
    }
    taken = cupidld_take_value(argc, argv, &index, argument, "-T", &value);
    if (taken != 0) {
      if (taken < 0 || cli->script != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->script = value;
      continue;
    }
    taken = cupidld_take_value(argc, argv, &index, argument, "-o", &value);
    if (taken != 0) {
      if (taken < 0 || cli->output != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->output = value;
      continue;
    }
    taken = cupidld_take_value(argc, argv, &index, argument,
                               "--text-address", &value);
    if (taken != 0) {
      if (taken < 0 || cli->have_text_address == CTOOL_TRUE ||
          cupidld_parse_u32(value, &cli->text_address) == 0) {
        return 0;
      }
      cli->have_text_address = CTOOL_TRUE;
      continue;
    }
    taken = cupidld_take_value(argc, argv, &index, argument, "--entry",
                               &value);
    if (taken != 0) {
      if (taken < 0 || cli->entry != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->entry = value;
      continue;
    }
    if (argument[0] == '-') {
      return 0;
    }
    cli->objects[cli->object_count] = argument;
    cli->object_count++;
  }
  if (cli->machine == (const char *)0 ||
      strcmp(cli->machine, "elf_i386") != 0 ||
      cli->output == (const char *)0 || cli->object_count == 0u) {
    return 0;
  }
  if (cli->script != (const char *)0) {
    if (cli->have_text_address == CTOOL_TRUE || cli->entry != (const char *)0) {
      return 0;
    }
  } else if (cli->have_text_address == CTOOL_FALSE ||
             cli->entry == (const char *)0) {
    return 0;
  }
  return 1;
}

static char *cupidld_working_directory(void) {
  size_t capacity = 256u;
  while (capacity <= 1048576u) {
    char *directory = (char *)malloc(capacity);
    if (directory == (char *)0) {
      return (char *)0;
    }
#if defined(_WIN32)
    if (_getcwd(directory, (int)capacity) != (char *)0) {
      return directory;
    }
#else
    if (getcwd(directory, capacity) != (char *)0) {
      return directory;
    }
#endif
    if (errno != ERANGE) {
      free(directory);
      return (char *)0;
    }
    free(directory);
    capacity *= 2u;
  }
  return (char *)0;
}

static ctool_status_t cupidld_absolute_path(const char *working_directory,
                                            const char *path,
                                            char **absolute_out) {
  size_t path_size;
  char *absolute;
  if (working_directory == (const char *)0 || path == (const char *)0 ||
      absolute_out == (char **)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *absolute_out = (char *)0;
  path_size = strlen(path);
  if (path_size == 0u || path[path_size - 1u] == '/') {
    return CTOOL_ERR_PATH;
  }
#if defined(_WIN32)
  if (path[path_size - 1u] == '\\') {
    return CTOOL_ERR_PATH;
  }
#endif
#if defined(_WIN32)
  (void)working_directory;
  errno = 0;
  absolute = _fullpath((char *)0, path, 0u);
  if (absolute == (char *)0) {
    return errno == ENOMEM ? CTOOL_ERR_NO_MEMORY : CTOOL_ERR_PATH;
  }
  for (path_size = 0u; absolute[path_size] != '\0'; path_size++) {
    if (absolute[path_size] == '\\') {
      absolute[path_size] = '/';
    }
  }
  if (path_size < 4u || absolute[1] != ':' || absolute[2] != '/') {
    free(absolute);
    return CTOOL_ERR_UNSUPPORTED;
  }
#else
  if (path[0] == '/') {
    absolute = (char *)malloc(path_size + 1u);
    if (absolute != (char *)0) {
      (void)memcpy(absolute, path, path_size + 1u);
    }
  } else {
    size_t working_size = strlen(working_directory);
    if (working_size > (size_t)-1 - path_size - 2u) {
      return CTOOL_ERR_OVERFLOW;
    }
    absolute = (char *)malloc(working_size + path_size + 2u);
    if (absolute != (char *)0) {
      (void)memcpy(absolute, working_directory, working_size);
      absolute[working_size] = '/';
      (void)memcpy(absolute + working_size + 1u, path, path_size + 1u);
    }
  }
  if (absolute == (char *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
#endif
  *absolute_out = absolute;
  return CTOOL_OK;
}

static char cupidld_path_fold(char character) {
#if defined(_WIN32)
  if (character >= 'A' && character <= 'Z') {
    return (char)(character + ('a' - 'A'));
  }
#endif
  return character;
}

static ctool_status_t cupidld_common_native_root(char *const *paths,
                                                 ctool_u32 path_count,
                                                 char **root_out) {
  char *root;
  ctool_u32 index;
  if (paths == (char *const *)0 || path_count == 0u ||
      root_out == (char **)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
#if defined(_WIN32)
  if (paths[0][1] != ':' || paths[0][2] != '/') {
    return CTOOL_ERR_PATH;
  }
  for (index = 1u; index < path_count; index++) {
    if (paths[index][1] != ':' || paths[index][2] != '/' ||
        cupidld_path_fold(paths[index][0]) !=
            cupidld_path_fold(paths[0][0])) {
      return CTOOL_ERR_UNSUPPORTED;
    }
  }
  root = (char *)malloc(4u);
  if (root != (char *)0) {
    root[0] = paths[0][0];
    root[1] = ':';
    root[2] = '/';
    root[3] = '\0';
  }
#else
  (void)index;
  root = (char *)malloc(2u);
  if (root != (char *)0) {
    root[0] = '/';
    root[1] = '\0';
  }
#endif
  if (root == (char *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  *root_out = root;
  return CTOOL_OK;
}

static ctool_status_t cupidld_logical_path(ctool_job_t *job,
                                           const ctool_path_t *logical_root,
                                           ctool_string_t native_root,
                                           const char *absolute,
                                           ctool_u32 path_limit,
                                           ctool_path_t *path_out) {
  size_t absolute_size = strlen(absolute);
  size_t start = (size_t)native_root.size;
  ctool_string_t spelling;
  size_t index;
  if (absolute_size <= start) {
    return CTOOL_ERR_PATH;
  }
  for (index = 0u; index < start; index++) {
    if (cupidld_path_fold(native_root.data[index]) !=
        cupidld_path_fold(absolute[index])) {
      return CTOOL_ERR_PATH;
    }
  }
  if (absolute_size - start > 4294967295u) {
    return CTOOL_ERR_LIMIT;
  }
  spelling.data = absolute + start;
  spelling.size = (ctool_u32)(absolute_size - start);
  return ctool_path_resolve(ctool_job_arena(job), logical_root, spelling,
                            path_limit, path_out);
}

static void cupidld_free_paths(char **paths, ctool_u32 count) {
  ctool_u32 index;
  if (paths == (char **)0) {
    return;
  }
  for (index = 0u; index < count; index++) {
    free(paths[index]);
  }
  free(paths);
}

int main(int argc, char **argv) {
  cupidld_cli_t cli;
  const char **cli_objects;
  char *working_directory = (char *)0;
  char **native_paths = (char **)0;
  ctool_u32 native_path_count;
  ctool_u32 script_native_index = 0u;
  ctool_u32 output_native_index;
  char *native_root = (char *)0;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_path_t logical_root;
  ctool_source_t *objects = (ctool_source_t *)0;
  ctool_source_t script;
  ctool_path_t path;
  ctool_path_t output_path;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_status_t status = CTOOL_OK;
  ctool_u32 index;
  int parsed;
  int exit_code = 1;

  cli_objects = (const char **)calloc((size_t)(argc > 0 ? argc : 1),
                                      sizeof(const char *));
  if (cli_objects == (const char **)0) {
    (void)fprintf(stderr, "cupidld: argument allocation failed\n");
    return 1;
  }
  (void)memset(&cli, 0, sizeof(cli));
  cli.objects = cli_objects;
  parsed = cupidld_parse_cli(argc, argv, &cli);
  if (parsed < 0) {
    cupidld_usage(stdout);
    free(cli_objects);
    return 0;
  }
  if (parsed == 0) {
    cupidld_usage(stderr);
    free(cli_objects);
    return 2;
  }
  native_path_count = cli.object_count + 1u;
  if (cli.script != (const char *)0) {
    native_path_count++;
  }
  native_paths =
      (char **)calloc((size_t)native_path_count, sizeof(char *));
  objects = (ctool_source_t *)calloc((size_t)cli.object_count,
                                     sizeof(ctool_source_t));
  working_directory = cupidld_working_directory();
  if (native_paths == (char **)0 || objects == (ctool_source_t *)0 ||
      working_directory == (char *)0) {
    (void)fprintf(stderr, "cupidld: path allocation failed\n");
    goto done;
  }
  for (index = 0u; index < cli.object_count; index++) {
    status = cupidld_absolute_path(working_directory, cli.objects[index],
                                   &native_paths[index]);
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "cupidld: invalid input path %s (%s)\n",
                    cli.objects[index], ctool_status_name(status));
      goto done;
    }
  }
  output_native_index = cli.object_count;
  if (cli.script != (const char *)0) {
    script_native_index = cli.object_count;
    status = cupidld_absolute_path(working_directory, cli.script,
                                   &native_paths[script_native_index]);
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "cupidld: invalid script path %s (%s)\n",
                    cli.script, ctool_status_name(status));
      goto done;
    }
    output_native_index++;
  }
  status = cupidld_absolute_path(working_directory, cli.output,
                                 &native_paths[output_native_index]);
  if (status == CTOOL_OK) {
    status = cupidld_common_native_root(native_paths, native_path_count,
                                        &native_root);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidld: paths cannot share a host root (%s)\n",
                  ctool_status_name(status));
    goto done;
  }
  limits.source_bytes = CUPIDLD_HOST_SOURCE_BYTES;
  limits.output_bytes = CUPIDLD_HOST_OUTPUT_BYTES;
  limits.arena_bytes = CUPIDLD_HOST_ARENA_BYTES;
  status = ctool_host_adapter_init(&adapter, native_root);
  config = ctool_host_job_config(&adapter, limits);
  if (status == CTOOL_OK) {
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_root(ctool_job_arena(job), &logical_root);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidld: job setup failed (%s)\n",
                  ctool_status_name(status));
    goto done;
  }
  for (index = 0u; index < cli.object_count; index++) {
    status = cupidld_logical_path(job, &logical_root,
                                  ctool_string(native_root),
                                  native_paths[index], limits.path_bytes,
                                  &path);
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(job, &path, &objects[index]);
    }
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "cupidld: cannot load %s (%s)\n",
                    cli.objects[index], ctool_status_name(status));
      goto done;
    }
  }
  (void)memset(&script, 0, sizeof(script));
  if (cli.script != (const char *)0) {
    status = cupidld_logical_path(job, &logical_root,
                                  ctool_string(native_root),
                                  native_paths[script_native_index],
                                  limits.path_bytes, &path);
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(job, &path, &script);
    }
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "cupidld: cannot load %s (%s)\n", cli.script,
                    ctool_status_name(status));
      goto done;
    }
  }
  status = cupidld_logical_path(job, &logical_root,
                                ctool_string(native_root),
                                native_paths[output_native_index],
                                limits.path_bytes, &output_path);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 4096u, limits.output_bytes, &output);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidld: output setup failed (%s)\n",
                  ctool_status_name(status));
    goto done;
  }
  (void)memset(&request, 0, sizeof(request));
  request.objects = objects;
  request.object_count = cli.object_count;
  request.maximum_image_span = CUPIDLD_HOST_IMAGE_SPAN;
  if (cli.script != (const char *)0) {
    request.layout.kind = CTOOL_LD_LAYOUT_SCRIPT;
    request.layout.as.script = &script;
  } else {
    request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
    request.layout.as.fixed_text.base_address = cli.text_address;
    request.layout.as.fixed_text.entry_symbol = ctool_string(cli.entry);
  }
  (void)memset(&result, 0, sizeof(result));
  status = ctool_ld_link(job, &request, output, &result);
  if (status == CTOOL_OK) {
    status = ctool_job_write(job, &output_path, ctool_buffer_view(output));
  }
  if (status != CTOOL_OK) {
    if (ctool_job_diagnostic_count(job) != 0u) {
      (void)ctool_job_render_diagnostics(job);
    } else {
      (void)fprintf(stderr, "cupidld: link failed (%s)\n",
                    ctool_status_name(status));
    }
    goto done;
  }
  exit_code = 0;

done:
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(native_root);
  cupidld_free_paths(native_paths, native_path_count);
  free(working_directory);
  free(objects);
  free(cli_objects);
  return exit_code;
}
