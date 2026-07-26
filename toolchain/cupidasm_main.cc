#include "ctool.h"
#include "ctool_host.h"
#include "cupidasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

#define CUPIDASM_HOST_SOURCE_BYTES 67108864u
#define CUPIDASM_HOST_ARENA_BYTES 134217728u

typedef struct {
  ctool_asm_artifact_kind_t artifact;
  const char *input;
  const char *output;
} cupidasm_cli_t;

static void cupidasm_usage(FILE *stream) {
  (void)fprintf(stream,
                "usage: cupidasm -f bin|elf32 INPUT -o OUTPUT\n");
}

static int cupidasm_take_value(int argc, char **argv, int *index,
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

static int cupidasm_parse_cli(int argc, char **argv, cupidasm_cli_t *cli) {
  int index;
  ctool_bool have_format = CTOOL_FALSE;
  (void)memset(cli, 0, sizeof(*cli));
  for (index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    taken = cupidasm_take_value(argc, argv, &index, argument, "-f", &value);
    if (taken != 0) {
      if (taken < 0 || have_format == CTOOL_TRUE) {
        return 0;
      }
      if (strcmp(value, "bin") == 0) {
        cli->artifact = CTOOL_ASM_ARTIFACT_RAW;
      } else if (strcmp(value, "elf32") == 0) {
        cli->artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
      } else {
        return 0;
      }
      have_format = CTOOL_TRUE;
      continue;
    }
    taken = cupidasm_take_value(argc, argv, &index, argument, "-o", &value);
    if (taken != 0) {
      if (taken < 0 || cli->output != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->output = value;
      continue;
    }
    if (argument[0] == '-' || cli->input != (const char *)0) {
      return 0;
    }
    cli->input = argument;
  }
  return have_format == CTOOL_TRUE && cli->input != (const char *)0 &&
                 cli->output != (const char *)0
             ? 1
             : 0;
}

static char *cupidasm_logical_path_copy(const char *path) {
  size_t size = strlen(path);
  size_t index;
  char *copy = (char *)malloc(size + 1u);
  if (copy == (char *)0) {
    return (char *)0;
  }
  for (index = 0u; index < size; index++) {
    copy[index] = path[index] == '\\' ? '/' : path[index];
  }
  copy[size] = '\0';
  return copy;
}

static int cupidasm_split_path(const char *path, char **root_out,
                               char **name_out) {
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
  if (size == 0u || path[size - 1u] == '/' || path[size - 1u] == '\\') {
    return 0;
  }
  if (separator == 0u && path[0] != '/' && path[0] != '\\') {
    root = (char *)malloc(2u);
    if (root == (char *)0) {
      return 0;
    }
    root[0] = '.';
    root[1] = '\0';
    *name_out = cupidasm_logical_path_copy(path);
  } else {
    root_size = separator;
    if (separator == 0u || (separator == 2u && path[1] == ':')) {
      root_size++;
    }
    root = (char *)malloc(root_size + 1u);
    if (root == (char *)0) {
      return 0;
    }
    (void)memcpy(root, path, root_size);
    root[root_size] = '\0';
    *name_out = cupidasm_logical_path_copy(path + separator + 1u);
  }
  if (*name_out == (char *)0) {
    free(root);
    return 0;
  }
  *root_out = root;
  return 1;
}

static int cupidasm_path_is_absolute(const char *path) {
  if (path[0] == '\0') {
    return 0;
  }
  return path[0] == '/' || path[0] == '\\' || path[1] == ':'
             ? 1
             : 0;
}

static int cupidasm_use_working_root(const char *path, char **root_out,
                                     char **name_out) {
  char *root = (char *)malloc(2u);
  char *name;
  if (root == (char *)0) {
    return 0;
  }
  name = cupidasm_logical_path_copy(path);
  if (name == (char *)0) {
    free(root);
    return 0;
  }
  root[0] = '.';
  root[1] = '\0';
  *root_out = root;
  *name_out = name;
  return 1;
}

static char *cupidasm_working_directory(void) {
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
    if (errno != ERANGE) {
      free(directory);
      return (char *)0;
    }
#endif
    free(directory);
    capacity *= 2u;
  }
  return (char *)0;
}

static char cupidasm_native_fold(char character) {
  if (character == '\\') {
    return '/';
  }
#if defined(_WIN32)
  if (character >= 'A' && character <= 'Z') {
    return (char)(character + ('a' - 'A'));
  }
#endif
  return character;
}

static int cupidasm_absolute_from_working_root(const char *path,
                                               char **root_out,
                                               char **name_out) {
  char *directory = cupidasm_working_directory();
  size_t index = 0u;
  const char *relative;
  int result;
  if (directory == (char *)0) {
    return 0;
  }
  while (directory[index] != '\0' && path[index] != '\0' &&
         cupidasm_native_fold(directory[index]) ==
             cupidasm_native_fold(path[index])) {
    index++;
  }
  if (directory[index] != '\0' ||
      (path[index] != '/' && path[index] != '\\')) {
    free(directory);
    return 0;
  }
  relative = path + index + 1u;
  result = relative[0] != '\0'
               ? cupidasm_use_working_root(relative, root_out, name_out)
               : 0;
  free(directory);
  return result;
}

static FILE *cupidasm_open_output(const char *path) {
#if defined(_WIN32)
  FILE *file = (FILE *)0;
  return fopen_s(&file, path, "wb") == 0 ? file : (FILE *)0;
#else
  return fopen(path, "wb");
#endif
}

static ctool_status_t cupidasm_write_output(const char *path,
                                            ctool_bytes_t bytes) {
  FILE *file = cupidasm_open_output(path);
  ctool_u32 written = 0u;
  if (file == (FILE *)0) {
    return CTOOL_ERR_IO;
  }
  while (written < bytes.size) {
    size_t count = fwrite(bytes.data + written, 1u,
                          (size_t)(bytes.size - written), file);
    if (count == 0u) {
      (void)fclose(file);
      return CTOOL_ERR_IO;
    }
    written += (ctool_u32)count;
  }
  return fclose(file) == 0 ? CTOOL_OK : CTOOL_ERR_IO;
}

int main(int argc, char **argv) {
  cupidasm_cli_t cli;
  char *native_root = (char *)0;
  char *logical_name = (char *)0;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_path_t root;
  ctool_path_t input_path;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_status_t status;
  int parsed = cupidasm_parse_cli(argc, argv, &cli);
  int exit_code = 1;
  if (parsed < 0) {
    cupidasm_usage(stdout);
    return 0;
  }
  if (parsed == 0) {
    cupidasm_usage(stderr);
    return 2;
  }
  if (!(cupidasm_path_is_absolute(cli.input)
            ? (cupidasm_absolute_from_working_root(
                   cli.input, &native_root, &logical_name) ||
               cupidasm_split_path(cli.input, &native_root, &logical_name))
            : cupidasm_use_working_root(cli.input, &native_root,
                                        &logical_name))) {
    (void)fprintf(stderr, "cupidasm: invalid input path\n");
    return 1;
  }
  limits.source_bytes = CUPIDASM_HOST_SOURCE_BYTES;
  limits.arena_bytes = CUPIDASM_HOST_ARENA_BYTES;
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
                                ctool_string(logical_name),
                                limits.path_bytes, &input_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &input_path, &source);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 4096u, limits.output_bytes, &output);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidasm: cannot load %s (%s)\n", cli.input,
                  ctool_status_name(status));
    goto done;
  }
  (void)memset(&request, 0, sizeof(request));
  request.artifact = cli.artifact;
  request.initial_mode = CTOOL_X86_MODE_32;
  request.include_roots = &root;
  request.include_root_count = 1u;
  (void)memset(&result, 0, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (status == CTOOL_OK) {
    status = cupidasm_write_output(cli.output, result.bytes);
  }
  if (status != CTOOL_OK) {
    if (ctool_job_diagnostic_count(job) != 0u) {
      (void)ctool_job_render_diagnostics(job);
    } else {
      (void)fprintf(stderr, "cupidasm: assembly failed (%s)\n",
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
  free(logical_name);
  return exit_code;
}
