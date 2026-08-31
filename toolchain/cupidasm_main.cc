#include "ctool.h"
#include "ctool_host.h"
#include "cupidasm.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CUPIDASM_HOST_SOURCE_BYTES 67108864u
#define CUPIDASM_HOST_ARENA_BYTES 134217728u
#define CUPIDASM_PUBLICATION_RECORD_BYTES 1048576u

static const char cupidasm_candidate_suffix[] = ".cupid-as-new";
static const char cupidasm_backup_suffix[] = ".cupid-as-old";
static const char cupidasm_absent_suffix[] = ".cupid-as-absent";
static const char cupidasm_commit_suffix[] = ".cupid-as-done";
static const ctool_u8 cupidasm_absent_record[] = {
    'C', 'U', 'P', 'I', 'D', '-', 'A', 'S', '-', 'A', 'B', 'S', 'E', 'N',
    'T', 1u};

#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#define CUPIDASM_LINUX_SYS_UNLINK 10
#define CUPIDASM_LINUX_SYS_RENAME 38

int cupid_linux_syscall1(int number, unsigned int first);
int cupid_linux_syscall2(int number, unsigned int first,
                         unsigned int second);

static int cupidasm_linux_syscall_failed(int result) {
  return result < 0 && result >= -4095 ? 1 : 0;
}
#endif

typedef struct {
  ctool_asm_artifact_kind_t artifact;
  const char *input;
  const char *output;
  const char *range_map;
} cupidasm_cli_t;

typedef struct {
  char *target;
  char *candidate;
  char *backup;
  char *absent;
  char *commit;
} cupidasm_publication_path_t;

typedef struct {
  char *backups[2];
  char *commits[2];
  ctool_u32 count;
  ctool_u32 storage_size;
  int committed;
  int linked;
  ctool_u8 *storage;
} cupidasm_publication_record_t;

typedef enum {
  CUPIDASM_OUTPUT_PATH_ORDINARY = 0,
  CUPIDASM_OUTPUT_PATH_INHERITED_FD,
  CUPIDASM_OUTPUT_PATH_INVALID_FD
} cupidasm_output_path_kind_t;

static void cupidasm_usage(FILE *stream) {
  (void)fprintf(stream,
                "usage: cupidasm -f bin [--map MAP] INPUT -o OUTPUT\n"
                "       cupidasm -f elf32 INPUT -o OUTPUT\n");
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
    taken = cupidasm_take_value(argc, argv, &index, argument, "--map", &value);
    if (taken != 0) {
      if (taken < 0 || cli->range_map != (const char *)0 ||
          value[0] == '\0') {
        return 0;
      }
      cli->range_map = value;
      continue;
    }
    if (argument[0] == '-' || cli->input != (const char *)0) {
      return 0;
    }
    cli->input = argument;
  }
  return have_format == CTOOL_TRUE && cli->input != (const char *)0 &&
                 cli->output != (const char *)0 &&
                 (cli->range_map == (const char *)0 ||
                  (cli->artifact == CTOOL_ASM_ARTIFACT_RAW &&
                   strcmp(cli->range_map, cli->output) != 0))
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
  return path[0] == '/' || path[0] == '\\' ||
                 (path[1] != '\0' && path[1] == ':')
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

static char *cupidasm_absolute_path_copy(const char *path) {
  char *logical = cupidasm_logical_path_copy(path);
  char *combined = logical;
  char *directory = (char *)0;
  char *logical_directory = (char *)0;
  char *result = (char *)0;
  size_t combined_size;
  size_t read_index;
  size_t write_index;
  size_t root_size;
  if (logical == (char *)0) {
    return (char *)0;
  }
  if (cupidasm_path_is_absolute(logical) == 0) {
    size_t directory_size;
    size_t path_size = strlen(logical);
    directory = cupidasm_working_directory();
    if (directory == (char *)0) {
      free(logical);
      return (char *)0;
    }
    logical_directory = cupidasm_logical_path_copy(directory);
    free(directory);
    if (logical_directory == (char *)0) {
      free(logical);
      return (char *)0;
    }
    directory_size = strlen(logical_directory);
    if (directory_size > (size_t)-1 - path_size - 2u) {
      free(logical_directory);
      free(logical);
      return (char *)0;
    }
    combined = (char *)malloc(directory_size + path_size + 2u);
    if (combined == (char *)0) {
      free(logical_directory);
      free(logical);
      return (char *)0;
    }
    (void)memcpy(combined, logical_directory, directory_size);
    combined[directory_size] = '/';
    (void)memcpy(combined + directory_size + 1u, logical, path_size + 1u);
    free(logical_directory);
    free(logical);
  }
  combined_size = strlen(combined);
  result = (char *)malloc(combined_size + 1u);
  if (result == (char *)0) {
    free(combined);
    return (char *)0;
  }
  if (combined_size >= 3u && combined[1] == ':' && combined[2] == '/') {
    result[0] = combined[0];
    result[1] = ':';
    result[2] = '/';
    read_index = 3u;
    write_index = 3u;
    root_size = 3u;
  } else if (combined_size >= 5u && combined[0] == '/' &&
             combined[1] == '/' && combined[2] != '/') {
    size_t server_end = 2u;
    size_t share_end;
    while (server_end < combined_size && combined[server_end] != '/') {
      server_end++;
    }
    share_end = server_end + 1u;
    while (share_end < combined_size && combined[share_end] != '/') {
      share_end++;
    }
    if (server_end == 2u || server_end + 1u >= combined_size ||
        share_end == server_end + 1u) {
      free(result);
      free(combined);
      return (char *)0;
    }
    (void)memcpy(result, combined, share_end);
    result[share_end] = '/';
    read_index = share_end;
    write_index = share_end + 1u;
    root_size = write_index;
  } else if (combined_size >= 1u && combined[0] == '/' &&
             (combined_size == 1u || combined[1] != '/')) {
    result[0] = '/';
    read_index = 1u;
    write_index = 1u;
    root_size = 1u;
  } else {
    free(result);
    free(combined);
    return (char *)0;
  }
  while (read_index < combined_size) {
    size_t component_start;
    size_t component_size;
    while (read_index < combined_size && combined[read_index] == '/') {
      read_index++;
    }
    component_start = read_index;
    while (read_index < combined_size && combined[read_index] != '/') {
      read_index++;
    }
    component_size = read_index - component_start;
    if (component_size == 0u ||
        (component_size == 1u && combined[component_start] == '.')) {
      continue;
    }
    if (component_size == 2u && combined[component_start] == '.' &&
        combined[component_start + 1u] == '.') {
      if (write_index == root_size) {
        free(result);
        free(combined);
        return (char *)0;
      }
      while (write_index > root_size && result[write_index - 1u] != '/') {
        write_index--;
      }
      if (write_index > root_size) {
        write_index--;
      }
      continue;
    }
    if (write_index != root_size && result[write_index - 1u] != '/') {
      result[write_index++] = '/';
    }
    (void)memcpy(result + write_index, combined + component_start,
                 component_size);
    write_index += component_size;
  }
  free(combined);
  if (write_index == root_size) {
    free(result);
    return (char *)0;
  }
  result[write_index] = '\0';
  return result;
}

static char *cupidasm_append_suffix(const char *path, const char *suffix) {
  size_t path_size = strlen(path);
  size_t suffix_size = strlen(suffix);
  char *result;
  if (path_size > (size_t)-1 - suffix_size - 1u) {
    return (char *)0;
  }
  result = (char *)malloc(path_size + suffix_size + 1u);
  if (result == (char *)0) {
    return (char *)0;
  }
  (void)memcpy(result, path, path_size);
  (void)memcpy(result + path_size, suffix, suffix_size + 1u);
  return result;
}

static void cupidasm_publication_path_close(
    cupidasm_publication_path_t *path) {
  free(path->target);
  free(path->candidate);
  free(path->backup);
  free(path->absent);
  free(path->commit);
  (void)memset(path, 0, sizeof(*path));
}

static ctool_status_t cupidasm_publication_path_open(
    const char *target, cupidasm_publication_path_t *path_out) {
  cupidasm_publication_path_t path;
  (void)memset(&path, 0, sizeof(path));
  path.target = cupidasm_absolute_path_copy(target);
  if (path.target != (char *)0) {
    path.candidate =
        cupidasm_append_suffix(path.target, cupidasm_candidate_suffix);
    path.backup = cupidasm_append_suffix(path.target, cupidasm_backup_suffix);
    path.absent = cupidasm_append_suffix(path.target, cupidasm_absent_suffix);
    path.commit = cupidasm_append_suffix(path.target, cupidasm_commit_suffix);
  }
  if (path.target == (char *)0 || path.candidate == (char *)0 ||
      path.backup == (char *)0 || path.absent == (char *)0 ||
      path.commit == (char *)0) {
    cupidasm_publication_path_close(&path);
    return CTOOL_ERR_NO_MEMORY;
  }
  *path_out = path;
  return CTOOL_OK;
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

static ctool_status_t cupidasm_finish_output(FILE *file,
                                             ctool_status_t status) {
  if (fflush(file) != 0) {
    status = CTOOL_ERR_IO;
  }
  if (fclose(file) != 0) {
    status = CTOOL_ERR_IO;
  }
  return status;
}

static cupidasm_output_path_kind_t cupidasm_output_path_kind(
    const char *path) {
#if defined(_WIN32)
  (void)path;
  return CUPIDASM_OUTPUT_PATH_ORDINARY;
#else
  static const char prefix[] = "/proc/self/fd/";
  const size_t prefix_size = sizeof(prefix) - 1u;
  ctool_u32 value = 0u;
  size_t index;
  if (strncmp(path, prefix, prefix_size) != 0) {
    return CUPIDASM_OUTPUT_PATH_ORDINARY;
  }
  if (path[prefix_size] == '\0' ||
      (path[prefix_size] == '0' && path[prefix_size + 1u] != '\0')) {
    return CUPIDASM_OUTPUT_PATH_INVALID_FD;
  }
  for (index = prefix_size; path[index] != '\0'; index++) {
    ctool_u32 digit;
    if (path[index] < '0' || path[index] > '9') {
      return CUPIDASM_OUTPUT_PATH_INVALID_FD;
    }
    digit = (ctool_u32)(path[index] - '0');
    if (value > (2147483647u - digit) / 10u) {
      return CUPIDASM_OUTPUT_PATH_INVALID_FD;
    }
    value = value * 10u + digit;
  }
  return CUPIDASM_OUTPUT_PATH_INHERITED_FD;
#endif
}

static cupidasm_output_path_kind_t cupidasm_publication_kind(
    const cupidasm_cli_t *cli) {
  cupidasm_output_path_kind_t output_kind =
      cupidasm_output_path_kind(cli->output);
  cupidasm_output_path_kind_t map_kind = CUPIDASM_OUTPUT_PATH_ORDINARY;
  if (cli->range_map != (const char *)0) {
    map_kind = cupidasm_output_path_kind(cli->range_map);
  }
  if (output_kind == CUPIDASM_OUTPUT_PATH_INVALID_FD ||
      map_kind == CUPIDASM_OUTPUT_PATH_INVALID_FD ||
      (cli->range_map != (const char *)0 && output_kind != map_kind)) {
    return CUPIDASM_OUTPUT_PATH_INVALID_FD;
  }
  return output_kind;
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
      return cupidasm_finish_output(file, CTOOL_ERR_IO);
    }
    written += (ctool_u32)count;
  }
  return cupidasm_finish_output(file, CTOOL_OK);
}

static ctool_status_t cupidasm_publication_inspect(const char *path,
                                                   int *exists_out) {
  FILE *file;
  ctool_u8 first;
  long size;
  if (exists_out == (int *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *exists_out = 0;
  errno = 0;
#if defined(_WIN32)
  {
    FILE *read_file = (FILE *)0;
    if (fopen_s(&read_file, path, "rb") != 0) {
      return errno == ENOENT ? CTOOL_OK : CTOOL_ERR_IO;
    }
    file = read_file;
  }
#else
  file = fopen(path, "rb");
  if (file == (FILE *)0) {
    return errno == ENOENT ? CTOOL_OK : CTOOL_ERR_IO;
  }
#endif
  errno = 0;
  (void)fread(&first, 1u, 1u, file);
  if (ferror(file) != 0) {
    (void)fclose(file);
    return CTOOL_ERR_IO;
  }
  if (fseek(file, 0L, SEEK_END) != 0) {
    (void)fclose(file);
    return CTOOL_ERR_IO;
  }
  size = ftell(file);
  if (size < 0L || fclose(file) != 0) {
    return CTOOL_ERR_IO;
  }
  *exists_out = 1;
  return CTOOL_OK;
}

static const char *cupidasm_range_kind_name(ctool_asm_raw_range_kind_t kind) {
  if (kind == CTOOL_ASM_RAW_RANGE_CODE16) {
    return "code16";
  }
  if (kind == CTOOL_ASM_RAW_RANGE_CODE32) {
    return "code32";
  }
  return "data";
}

static const char *cupidasm_edge_kind_name(ctool_asm_raw_edge_kind_t kind) {
  if (kind == CTOOL_ASM_RAW_EDGE_RELATIVE) {
    return "relative";
  }
  if (kind == CTOOL_ASM_RAW_EDGE_FAR) {
    return "far";
  }
  return "indirect";
}

static const char *cupidasm_edge_class_name(
    ctool_asm_raw_edge_class_t class_id) {
  if (class_id == CTOOL_ASM_RAW_EDGE_LOCAL) {
    return "local";
  }
  if (class_id == CTOOL_ASM_RAW_EDGE_EXTERNAL) {
    return "external";
  }
  return "unprovable";
}

static ctool_status_t cupidasm_write_range_map(
    const char *path, const ctool_asm_result_t *result,
    ctool_u32 base_address) {
  FILE *file = cupidasm_open_output(path);
  ctool_u32 index;
  if (file == (FILE *)0) {
    return CTOOL_ERR_IO;
  }
  if (fprintf(file,
              "cupid.raw-map.v2\nsize %u\nbase 0x%08x\nedges %u\n",
              (unsigned int)result->bytes.size,
              (unsigned int)base_address,
              (unsigned int)result->raw_edge_count) < 0) {
    return cupidasm_finish_output(file, CTOOL_ERR_IO);
  }
  for (index = 0u; index < result->raw_range_count; index++) {
    const ctool_asm_raw_range_t *range = &result->raw_ranges[index];
    if (fprintf(file, "range 0x%08x %s\n",
                (unsigned int)range->offset,
                cupidasm_range_kind_name(range->kind)) < 0) {
      return cupidasm_finish_output(file, CTOOL_ERR_IO);
    }
  }
  for (index = 0u; index < result->raw_edge_count; index++) {
    const ctool_asm_raw_edge_t *edge = &result->raw_edges[index];
    if (edge->class_id == CTOOL_ASM_RAW_EDGE_UNPROVABLE) {
      if (fprintf(file, "edge 0x%08x %s %s - - unknown -\n",
                  (unsigned int)edge->source_offset,
                  cupidasm_edge_kind_name(edge->kind),
                  cupidasm_edge_class_name(edge->class_id)) < 0) {
        return cupidasm_finish_output(file, CTOOL_ERR_IO);
      }
    } else if (edge->class_id == CTOOL_ASM_RAW_EDGE_EXTERNAL) {
      if (fprintf(file, "edge 0x%08x %s %s - 0x%08x %u 0x%08x\n",
                  (unsigned int)edge->source_offset,
                  cupidasm_edge_kind_name(edge->kind),
                  cupidasm_edge_class_name(edge->class_id),
                  (unsigned int)edge->target_address,
                  (unsigned int)edge->target_mode,
                  (unsigned int)edge->target_segment) < 0) {
        return cupidasm_finish_output(file, CTOOL_ERR_IO);
      }
    } else if (fprintf(
                   file,
                   "edge 0x%08x %s %s 0x%08x 0x%08x %u 0x%08x\n",
                   (unsigned int)edge->source_offset,
                   cupidasm_edge_kind_name(edge->kind),
                   cupidasm_edge_class_name(edge->class_id),
                   (unsigned int)edge->target_offset,
                   (unsigned int)edge->target_address,
                   (unsigned int)edge->target_mode,
                   (unsigned int)edge->target_segment) < 0) {
      return cupidasm_finish_output(file, CTOOL_ERR_IO);
    }
  }
  return cupidasm_finish_output(file, CTOOL_OK);
}

static ctool_status_t cupidasm_publish_inherited(
    const cupidasm_cli_t *cli, const ctool_asm_result_t *result) {
  ctool_status_t status = cupidasm_write_output(cli->output, result->bytes);
  if (status == CTOOL_OK && cli->range_map != (const char *)0) {
    status = cupidasm_write_range_map(cli->range_map, result,
                                      result->raw_origin);
  }
  return status;
}

static int cupidasm_publication_path_equal(const char *left,
                                           const char *right) {
  size_t index = 0u;
  while (left[index] != '\0' && right[index] != '\0') {
    if (cupidasm_native_fold(left[index]) !=
        cupidasm_native_fold(right[index])) {
      return 0;
    }
    index++;
  }
  return left[index] == right[index] ? 1 : 0;
}

static ctool_status_t cupidasm_publication_remove_if_present(
    const char *path) {
  int exists = 0;
  ctool_status_t status = cupidasm_publication_inspect(path, &exists);
  if (status == CTOOL_OK && exists != 0) {
#if defined(_WIN32)
    status = DeleteFileA(path) != 0 ? CTOOL_OK : CTOOL_ERR_IO;
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
    status = cupidasm_linux_syscall_failed(cupid_linux_syscall1(
                 CUPIDASM_LINUX_SYS_UNLINK, (unsigned int)path)) == 0
                 ? CTOOL_OK
                 : CTOOL_ERR_IO;
#else
    status = unlink(path) == 0 ? CTOOL_OK : CTOOL_ERR_IO;
#endif
  }
  return status;
}

static ctool_status_t cupidasm_publication_replace(const char *source,
                                                   const char *destination) {
#if defined(_WIN32)
  return MoveFileExA(source, destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0
             ? CTOOL_OK
             : CTOOL_ERR_IO;
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
  return cupidasm_linux_syscall_failed(cupid_linux_syscall2(
             CUPIDASM_LINUX_SYS_RENAME, (unsigned int)source,
             (unsigned int)destination)) == 0
             ? CTOOL_OK
             : CTOOL_ERR_IO;
#else
  return rename(source, destination) == 0 ? CTOOL_OK : CTOOL_ERR_IO;
#endif
}

static ctool_status_t cupidasm_publication_read(
    const char *path, cupidasm_publication_record_t *record_out) {
  FILE *file;
  long length;
  ctool_u8 *storage;
  size_t read_size;
  (void)memset(record_out, 0, sizeof(*record_out));
#if defined(_WIN32)
  file = (FILE *)0;
  if (fopen_s(&file, path, "rb") != 0) {
    return CTOOL_ERR_IO;
  }
#else
  file = fopen(path, "rb");
  if (file == (FILE *)0) {
    return CTOOL_ERR_IO;
  }
#endif
  if (fseek(file, 0L, SEEK_END) != 0) {
    (void)fclose(file);
    return CTOOL_ERR_IO;
  }
  length = ftell(file);
  if (length < 0L ||
      (unsigned long)length >
          (unsigned long)CUPIDASM_PUBLICATION_RECORD_BYTES ||
      fseek(file, 0L, 0) != 0) {
    (void)fclose(file);
    return CTOOL_ERR_LIMIT;
  }
  read_size = (size_t)(unsigned long)length;
  storage = (ctool_u8 *)malloc(read_size == 0u ? 1u : read_size);
  if (storage == (ctool_u8 *)0) {
    (void)fclose(file);
    return CTOOL_ERR_NO_MEMORY;
  }
  if (read_size != 0u && fread(storage, 1u, read_size, file) != read_size) {
    free(storage);
    (void)fclose(file);
    return CTOOL_ERR_IO;
  }
  if (fclose(file) != 0) {
    free(storage);
    return CTOOL_ERR_IO;
  }
  record_out->storage = storage;
  record_out->count = (ctool_u32)read_size;
  record_out->storage_size = (ctool_u32)read_size;
  return CTOOL_OK;
}

static void cupidasm_publication_record_close(
    cupidasm_publication_record_t *record) {
  free(record->storage);
  (void)memset(record, 0, sizeof(*record));
}

static ctool_status_t cupidasm_publication_require_absent_record(
    const char *path) {
  cupidasm_publication_record_t record;
  ctool_status_t status = cupidasm_publication_read(path, &record);
  if (status == CTOOL_OK &&
      (record.count != (ctool_u32)sizeof(cupidasm_absent_record) ||
       memcmp(record.storage, cupidasm_absent_record,
              sizeof(cupidasm_absent_record)) != 0)) {
    status = CTOOL_ERR_INPUT;
  }
  cupidasm_publication_record_close(&record);
  return status;
}

static void cupidasm_publication_store_u32(ctool_u8 *destination,
                                           ctool_u32 value) {
  destination[0] = (ctool_u8)(value & 0xffu);
  destination[1] = (ctool_u8)((value >> 8u) & 0xffu);
  destination[2] = (ctool_u8)((value >> 16u) & 0xffu);
  destination[3] = (ctool_u8)((value >> 24u) & 0xffu);
}

static ctool_u32 cupidasm_publication_load_u32(const ctool_u8 *source) {
  return (ctool_u32)source[0] | ((ctool_u32)source[1] << 8u) |
         ((ctool_u32)source[2] << 16u) | ((ctool_u32)source[3] << 24u);
}

static int cupidasm_publication_private_path_valid(const char *path,
                                                   size_t path_size,
                                                   const char *suffix) {
  size_t suffix_size = strlen(suffix);
  size_t component;
  size_t index;
  if (path == (const char *)0 || path_size <= suffix_size) {
    return 0;
  }
  if (path_size >= 3u && path[1] == ':' && path[2] == '/') {
    component = 3u;
  } else if (path_size >= 3u && path[0] == '/' && path[1] == '/' &&
             path[2] != '/') {
    component = 2u;
  } else if (path[0] == '/' && (path_size == 1u || path[1] != '/')) {
    component = 1u;
  } else {
    return 0;
  }
  if (component >= path_size) {
    return 0;
  }
  for (index = component; index < path_size; index++) {
    char character = path[index];
    if (character == '\0' || character == '\\') {
      return 0;
    }
    if (character == '/') {
      size_t component_size = index - component;
      if (component_size == 0u ||
          (component_size == 1u && path[component] == '.') ||
          (component_size == 2u && path[component] == '.' &&
           path[component + 1u] == '.')) {
        return 0;
      }
      component = index + 1u;
    }
  }
  if (component == path_size ||
      (path_size - component == 1u && path[component] == '.') ||
      (path_size - component == 2u && path[component] == '.' &&
       path[component + 1u] == '.')) {
    return 0;
  }
  return path_size >= suffix_size &&
                 memcmp(path + path_size - suffix_size, suffix,
                        suffix_size) == 0
             ? 1
             : 0;
}

static int cupidasm_publication_private_pair_matches(const char *backup,
                                                      const char *commit) {
  size_t backup_size = strlen(backup);
  size_t commit_size = strlen(commit);
  size_t backup_suffix_size = strlen(cupidasm_backup_suffix);
  size_t commit_suffix_size = strlen(cupidasm_commit_suffix);
  size_t backup_stem = backup_size - backup_suffix_size;
  size_t commit_stem = commit_size - commit_suffix_size;
  return backup_stem == commit_stem &&
                 memcmp(backup, commit, backup_stem) == 0
             ? 1
             : 0;
}

static ctool_status_t cupidasm_publication_parse_record(
    cupidasm_publication_record_t *record) {
  static const ctool_u8 magic[7] = {'C', 'U', 'P', 'I', 'D', 'A', 'S'};
  size_t size = (size_t)record->count;
  size_t cursor = 12u;
  ctool_u32 count;
  ctool_u32 index;
  if (record->storage == (ctool_u8 *)0 || size < cursor ||
      memcmp(record->storage, magic, sizeof(magic)) != 0 ||
      (record->storage[7] != 1u && record->storage[7] != 2u &&
       record->storage[7] != 3u)) {
    return CTOOL_ERR_INPUT;
  }
  record->committed = record->storage[7] == 1u || record->storage[7] == 3u;
  record->linked = record->storage[7] == 2u || record->storage[7] == 3u;
  count = cupidasm_publication_load_u32(record->storage + 8u);
  if (count == 0u || count > 2u) {
    return CTOOL_ERR_INPUT;
  }
  for (index = 0u; index < count; index++) {
    ctool_u32 field;
    for (field = 0u; field < 2u; field++) {
      ctool_u32 field_size;
      char *value;
      if (cursor > size || size - cursor < 4u) {
        return CTOOL_ERR_INPUT;
      }
      field_size =
          cupidasm_publication_load_u32(record->storage + cursor);
      cursor += 4u;
      if (field_size == 0u || cursor > size ||
          (size_t)field_size > size - cursor ||
          size - cursor - (size_t)field_size < 1u ||
          record->storage[cursor + (size_t)field_size] != 0u) {
        return CTOOL_ERR_INPUT;
      }
      value = (char *)(record->storage + cursor);
      if (cupidasm_publication_private_path_valid(
              value, (size_t)field_size,
              field == 0u ? cupidasm_backup_suffix
                          : cupidasm_commit_suffix) == 0) {
        return CTOOL_ERR_INPUT;
      }
      if (field == 0u) {
        record->backups[index] = value;
      } else {
        record->commits[index] = value;
      }
      cursor += (size_t)field_size + 1u;
    }
    if (cupidasm_publication_private_pair_matches(record->backups[index],
                                                   record->commits[index]) ==
        0) {
      return CTOOL_ERR_INPUT;
    }
  }
  if (cursor != size ||
      (count == 2u &&
       (cupidasm_publication_path_equal(record->backups[0],
                                        record->backups[1]) != 0 ||
        cupidasm_publication_path_equal(record->commits[0],
                                        record->commits[1]) != 0))) {
    return CTOOL_ERR_INPUT;
  }
  record->count = count;
  return CTOOL_OK;
}

static ctool_status_t cupidasm_publication_render_record(
    const cupidasm_publication_path_t *paths, ctool_u32 count,
    ctool_bytes_t *bytes_out, ctool_u8 **storage_out) {
  static const ctool_u8 magic[8] = {'C', 'U', 'P', 'I', 'D', 'A', 'S', 2u};
  size_t required = 12u;
  size_t cursor;
  ctool_u32 index;
  ctool_u8 *storage;
  for (index = 0u; index < count; index++) {
    size_t backup_size = strlen(paths[index].backup);
    size_t commit_size = strlen(paths[index].commit);
    if (backup_size > 0xffffffffu || commit_size > 0xffffffffu ||
        required > (size_t)CUPIDASM_PUBLICATION_RECORD_BYTES - 10u -
                       backup_size - commit_size) {
      return CTOOL_ERR_LIMIT;
    }
    required += backup_size + commit_size + 10u;
  }
  storage = (ctool_u8 *)malloc(required);
  if (storage == (ctool_u8 *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  (void)memcpy(storage, magic, sizeof(magic));
  cupidasm_publication_store_u32(storage + 8u, count);
  cursor = 12u;
  for (index = 0u; index < count; index++) {
    ctool_u32 field;
    for (field = 0u; field < 2u; field++) {
      const char *value =
          field == 0u ? paths[index].backup : paths[index].commit;
      size_t value_size = strlen(value);
      cupidasm_publication_store_u32(storage + cursor,
                                     (ctool_u32)value_size);
      cursor += 4u;
      (void)memcpy(storage + cursor, value, value_size + 1u);
      cursor += value_size + 1u;
    }
  }
  bytes_out->data = storage;
  bytes_out->size = (ctool_u32)required;
  *storage_out = storage;
  return CTOOL_OK;
}

#if defined(CUPIDASM_PUBLICATION_TESTING)
static int cupidasm_publication_test_fails(const char *name,
                                           ctool_u32 index) {
  const char *value = getenv(name);
  char expected[16];
  int written;
  if (value == (const char *)0) {
    return 0;
  }
  written = snprintf(expected, sizeof(expected), "%u", index);
  if (written <= 0 || (size_t)written >= sizeof(expected)) {
    return 0;
  }
  return strcmp(value, expected) == 0 ? 1 : 0;
}
#endif

static int cupidasm_publication_record_matches(
    const cupidasm_publication_record_t *record,
    const cupidasm_publication_path_t *paths, ctool_u32 count);

static int cupidasm_publication_records_equal(
    const cupidasm_publication_record_t *left,
    const cupidasm_publication_record_t *right) {
  ctool_u32 index;
  if (left->count != right->count) {
    return 0;
  }
  for (index = 0u; index < left->count; index++) {
    if (cupidasm_publication_path_equal(left->backups[index],
                                        right->backups[index]) == 0 ||
        cupidasm_publication_path_equal(left->commits[index],
                                        right->commits[index]) == 0) {
      return 0;
    }
  }
  return 1;
}

static ctool_status_t cupidasm_publication_find_commit(
    const cupidasm_publication_record_t *record, int inspect_linked_peers,
    int *committed_out) {
  ctool_u32 index;
  int linked_committed = record->linked != 0 && record->committed != 0;
  int pending = record->linked != 0 && record->committed == 0;
  *committed_out = record->committed;
  if ((record->linked == 0 && inspect_linked_peers == 0) ||
      (record->linked != 0 && record->committed != 0)) {
    return CTOOL_OK;
  }
  for (index = 0u; index < record->count; index++) {
    int exists = 0;
    ctool_status_t status =
        cupidasm_publication_inspect(record->commits[index], &exists);
    if (status != CTOOL_OK) {
      return status;
    }
    if (exists != 0) {
      cupidasm_publication_record_t peer;
      status = cupidasm_publication_read(record->commits[index], &peer);
      if (status == CTOOL_OK) {
        status = cupidasm_publication_parse_record(&peer);
      }
      if (status == CTOOL_OK &&
          cupidasm_publication_records_equal(record, &peer) != 0) {
        if (peer.committed != 0 && peer.linked != 0) {
          linked_committed = 1;
          *committed_out = 1;
        } else if (peer.committed == 0 && peer.linked != 0) {
          pending = 1;
        }
      }
      cupidasm_publication_record_close(&peer);
      if (status != CTOOL_OK) {
        return status;
      }
      if (linked_committed != 0) {
        return CTOOL_OK;
      }
    }
  }
  if (pending != 0) {
    *committed_out = 0;
  }
  return CTOOL_OK;
}

static ctool_status_t cupidasm_publication_remove_commit_records(
    const cupidasm_publication_path_t *paths, ctool_u32 count) {
  ctool_u32 index;
  ctool_u32 witness = count;
  int exists[2] = {0, 0};
  for (index = 0u; index < count; index++) {
    ctool_status_t status =
        cupidasm_publication_inspect(paths[index].commit, &exists[index]);
    if (status != CTOOL_OK) {
      return status;
    }
    if (exists[index] != 0) {
      cupidasm_publication_record_t record;
      status = cupidasm_publication_read(paths[index].commit, &record);
      if (status == CTOOL_OK) {
        status = cupidasm_publication_parse_record(&record);
      }
      if (status == CTOOL_OK && record.committed != 0 &&
          cupidasm_publication_record_matches(&record, paths, count) != 0 &&
          witness == count) {
        witness = index;
      }
      cupidasm_publication_record_close(&record);
    }
  }
  if (witness == count) {
    return CTOOL_ERR_INPUT;
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t status = CTOOL_OK;
    if (index == witness || exists[index] == 0) {
      continue;
    }
#if defined(CUPIDASM_PUBLICATION_TESTING)
    if (cupidasm_publication_test_fails(
            "CUPIDASM_TEST_FAIL_COMMIT_REMOVE_INDEX", index) != 0) {
      status = CTOOL_ERR_IO;
    }
#endif
    if (status == CTOOL_OK) {
      status =
          cupidasm_publication_remove_if_present(paths[index].commit);
    }
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return cupidasm_publication_remove_if_present(paths[witness].commit);
}

static ctool_status_t cupidasm_publication_prepare(
    const cupidasm_publication_path_t *paths, ctool_u32 count) {
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    int commit_exists = 0;
    ctool_status_t status =
        cupidasm_publication_inspect(paths[index].commit, &commit_exists);
    if (status != CTOOL_OK) {
      return status;
    }
    if (commit_exists != 0) {
      cupidasm_publication_record_t record;
      ctool_u32 cleanup;
      ctool_u32 matched_entry;
      int transaction_committed = 0;
      int exact_scope;
      status = cupidasm_publication_read(paths[index].commit, &record);
      if (status == CTOOL_OK) {
        status = cupidasm_publication_parse_record(&record);
      }
      if (status != CTOOL_OK) {
        cupidasm_publication_record_close(&record);
        return status;
      }
      matched_entry = record.count;
      for (cleanup = 0u; cleanup < record.count; cleanup++) {
        if (cupidasm_publication_path_equal(record.commits[cleanup],
                                            paths[index].commit) != 0) {
          matched_entry = cleanup;
          break;
        }
      }
      if (matched_entry == record.count) {
        cupidasm_publication_record_close(&record);
        return CTOOL_ERR_INPUT;
      }
      exact_scope =
          cupidasm_publication_record_matches(&record, paths, count);
      status = cupidasm_publication_find_commit(
          &record, exact_scope, &transaction_committed);
      if (status != CTOOL_OK) {
        cupidasm_publication_record_close(&record);
        return status;
      }
      if (transaction_committed != 0 && exact_scope != 0) {
        for (cleanup = 0u; cleanup < count && status == CTOOL_OK; cleanup++) {
          status = cupidasm_publication_remove_if_present(
              paths[cleanup].backup);
          if (status == CTOOL_OK) {
            status = cupidasm_publication_remove_if_present(
                paths[cleanup].absent);
          }
        }
        if (status == CTOOL_OK) {
          status =
              cupidasm_publication_remove_commit_records(paths, count);
        }
      } else if (transaction_committed != 0 && record.linked != 0) {
        status = cupidasm_publication_remove_if_present(
            paths[index].backup);
        if (status == CTOOL_OK) {
          status = cupidasm_publication_remove_if_present(
              paths[index].absent);
        }
        if (status == CTOOL_OK && record.committed == 0) {
          record.storage[7] = 3u;
          status = cupidasm_write_output(
              paths[index].commit,
              ctool_bytes(record.storage, record.storage_size));
        }
        if (status == CTOOL_OK) {
          status = CTOOL_ERR_IO;
        }
      } else if (transaction_committed != 0) {
        status = cupidasm_publication_remove_if_present(
            paths[index].backup);
        if (status == CTOOL_OK) {
          status = cupidasm_publication_remove_if_present(
              paths[index].absent);
        }
        if (status == CTOOL_OK) {
          status = cupidasm_publication_remove_if_present(
              paths[index].commit);
        }
      } else if (exact_scope != 0) {
        for (cleanup = 0u; cleanup < count && status == CTOOL_OK; cleanup++) {
          status = cupidasm_publication_remove_if_present(
              paths[cleanup].commit);
        }
      } else {
        status = cupidasm_publication_remove_if_present(
            paths[index].commit);
      }
      cupidasm_publication_record_close(&record);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  {
    int backup_exists[2] = {0, 0};
    int absent_exists[2] = {0, 0};
    for (index = 0u; index < count; index++) {
      ctool_status_t status = cupidasm_publication_inspect(
          paths[index].backup, &backup_exists[index]);
      if (status == CTOOL_OK) {
        status = cupidasm_publication_inspect(paths[index].absent,
                                              &absent_exists[index]);
      }
      if (status == CTOOL_OK && backup_exists[index] != 0 &&
          absent_exists[index] != 0) {
        status = CTOOL_ERR_INPUT;
      }
      if (status == CTOOL_OK && absent_exists[index] != 0) {
        status =
            cupidasm_publication_require_absent_record(paths[index].absent);
      }
      if (status != CTOOL_OK) {
        return status;
      }
    }
    for (index = 0u; index < count; index++) {
      ctool_status_t status;
      if (backup_exists[index] == 0 && absent_exists[index] == 0) {
        continue;
      }
      if (backup_exists[index] != 0) {
        status = cupidasm_publication_replace(paths[index].backup,
                                              paths[index].target);
      } else {
        int target_exists = 0;
        status =
            cupidasm_publication_inspect(paths[index].target, &target_exists);
        if (status == CTOOL_OK && target_exists != 0) {
          status = cupidasm_publication_remove_if_present(paths[index].target);
        }
        if (status == CTOOL_OK) {
          status =
              cupidasm_publication_remove_if_present(paths[index].absent);
        }
      }
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t status =
        cupidasm_publication_remove_if_present(paths[index].candidate);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static int cupidasm_publication_record_matches(
    const cupidasm_publication_record_t *record,
    const cupidasm_publication_path_t *paths, ctool_u32 count) {
  ctool_u32 index;
  if (record->count != count) {
    return 0;
  }
  for (index = 0u; index < count; index++) {
    if (cupidasm_publication_path_equal(record->backups[index],
                                        paths[index].backup) == 0 ||
        cupidasm_publication_path_equal(record->commits[index],
                                        paths[index].commit) == 0) {
      return 0;
    }
  }
  return 1;
}

static ctool_status_t cupidasm_publish(
    const cupidasm_cli_t *cli, const ctool_asm_result_t *result) {
  cupidasm_publication_path_t paths[2];
  int existed[2] = {0, 0};
  int backed_up[2] = {0, 0};
  int tombstoned[2] = {0, 0};
  int published[2] = {0, 0};
  int committed = 0;
  const char *all_paths[10];
  ctool_u32 count = cli->range_map == (const char *)0 ? 1u : 2u;
  ctool_u32 path_count = count * 5u;
  ctool_u32 index;
  ctool_u32 other;
  ctool_status_t status;
  ctool_status_t rollback_status = CTOOL_OK;
  ctool_status_t cleanup_status = CTOOL_OK;
  ctool_bytes_t commit_record = ctool_bytes((const ctool_u8 *)0, 0u);
  ctool_u8 *commit_storage = (ctool_u8 *)0;
  (void)memset(paths, 0, sizeof(paths));
  status = cupidasm_publication_path_open(cli->output, &paths[0]);
  if (status == CTOOL_OK && count == 2u) {
    status = cupidasm_publication_path_open(cli->range_map, &paths[1]);
  }
  if (status != CTOOL_OK) {
    goto done;
  }
  for (index = 0u; index < count; index++) {
    all_paths[index * 5u] = paths[index].target;
    all_paths[index * 5u + 1u] = paths[index].candidate;
    all_paths[index * 5u + 2u] = paths[index].backup;
    all_paths[index * 5u + 3u] = paths[index].absent;
    all_paths[index * 5u + 4u] = paths[index].commit;
  }
  for (index = 0u; index < path_count; index++) {
    for (other = index + 1u; other < path_count; other++) {
      if (cupidasm_publication_path_equal(all_paths[index],
                                          all_paths[other]) != 0) {
        status = CTOOL_ERR_INVALID_ARGUMENT;
        goto done;
      }
    }
  }
  status = cupidasm_publication_prepare(paths, count);
  if (status != CTOOL_OK) {
    goto done;
  }
  status = cupidasm_publication_render_record(paths, count, &commit_record,
                                               &commit_storage);
  if (status != CTOOL_OK) {
    goto done;
  }
  status = cupidasm_write_output(paths[0].candidate, result->bytes);
  if (status != CTOOL_OK) {
    goto rollback;
  }
  if (count == 2u) {
    status = cupidasm_write_range_map(paths[1].candidate, result,
                                      result->raw_origin);
    if (status != CTOOL_OK) {
      goto rollback;
    }
  }
  for (index = 0u; index < count; index++) {
    status = cupidasm_write_output(paths[index].commit, commit_record);
    if (status != CTOOL_OK) {
      goto rollback;
    }
  }
  for (index = 0u; index < count; index++) {
    status = cupidasm_publication_inspect(paths[index].target,
                                          &existed[index]);
    if (status != CTOOL_OK) {
      goto rollback;
    }
  }
  for (index = 0u; index < count; index++) {
    if (existed[index] == 0) {
      status = cupidasm_write_output(
          paths[index].absent,
          ctool_bytes(cupidasm_absent_record,
                      (ctool_u32)sizeof(cupidasm_absent_record)));
      if (status != CTOOL_OK) {
        goto rollback;
      }
      tombstoned[index] = 1;
    }
  }
  for (index = 0u; index < count; index++) {
    if (existed[index] != 0) {
      status = cupidasm_publication_replace(paths[index].target,
                                            paths[index].backup);
      if (status != CTOOL_OK) {
        goto rollback;
      }
      backed_up[index] = 1;
    }
  }
  for (index = 0u; index < count; index++) {
    status = CTOOL_OK;
#if defined(CUPIDASM_PUBLICATION_TESTING)
    if (cupidasm_publication_test_fails(
            "CUPIDASM_TEST_FAIL_PUBLISH_INDEX", index) != 0) {
      status = CTOOL_ERR_IO;
    }
#endif
    if (status == CTOOL_OK) {
      status = cupidasm_publication_replace(paths[index].candidate,
                                            paths[index].target);
    }
    if (status != CTOOL_OK) {
      goto rollback;
    }
    published[index] = 1;
  }
  commit_storage[7] = 3u;
  for (index = 0u; index < count; index++) {
    status = CTOOL_OK;
#if defined(CUPIDASM_PUBLICATION_TESTING)
    if (cupidasm_publication_test_fails(
            "CUPIDASM_TEST_FAIL_COMMIT_INDEX", index) != 0) {
      status = CTOOL_ERR_IO;
    }
    if (cupidasm_publication_test_fails(
            "CUPIDASM_TEST_CORRUPT_COMMIT_INDEX", index) != 0) {
      static const ctool_u8 corrupt_record[4] = {'C', 'U', 'P', 'I'};
      status = cupidasm_write_output(
          paths[index].commit, ctool_bytes(corrupt_record, 4u));
      if (status == CTOOL_OK) {
        status = CTOOL_ERR_IO;
      }
    }
#endif
    if (status == CTOOL_OK) {
      status = cupidasm_write_output(paths[index].commit, commit_record);
    }
    if (status != CTOOL_OK) {
      int commit_exists = 0;
      ctool_status_t write_status = status;
      ctool_status_t inspect_status = cupidasm_publication_inspect(
          paths[index].commit, &commit_exists);
      if (inspect_status != CTOOL_OK) {
        status = inspect_status;
        if (committed != 0) {
          goto committed_cleanup;
        }
        goto rollback;
      }
      if (commit_exists == 0) {
        status = write_status;
        if (committed != 0) {
          goto committed_cleanup;
        }
        goto rollback;
      }
      {
        cupidasm_publication_record_t observed;
        ctool_status_t read_status =
            cupidasm_publication_read(paths[index].commit, &observed);
        if (read_status == CTOOL_OK) {
          read_status = cupidasm_publication_parse_record(&observed);
        }
        if (read_status == CTOOL_OK && observed.committed != 0 &&
            cupidasm_publication_record_matches(&observed, paths, count) !=
                0) {
          committed = 1;
        }
        cupidasm_publication_record_close(&observed);
      }
      status = write_status;
      if (committed != 0) {
        goto committed_cleanup;
      }
      goto rollback;
    }
    committed = 1;
  }
committed_cleanup:
  for (index = 0u; index < count; index++) {
    if (backed_up[index] != 0) {
      ctool_status_t cleanup = CTOOL_OK;
#if defined(CUPIDASM_PUBLICATION_TESTING)
      if (cupidasm_publication_test_fails(
              "CUPIDASM_TEST_FAIL_BACKUP_CLEANUP_INDEX", index) != 0) {
        cleanup = CTOOL_ERR_IO;
      }
#endif
      if (cleanup == CTOOL_OK) {
        cleanup =
            cupidasm_publication_remove_if_present(paths[index].backup);
        if (cleanup == CTOOL_OK) {
          backed_up[index] = 0;
        }
      }
      if (cleanup_status == CTOOL_OK && cleanup != CTOOL_OK) {
        cleanup_status = cleanup;
      }
    }
    if (tombstoned[index] != 0) {
      ctool_status_t cleanup =
          cupidasm_publication_remove_if_present(paths[index].absent);
      if (cleanup == CTOOL_OK) {
        tombstoned[index] = 0;
      }
      if (cleanup_status == CTOOL_OK && cleanup != CTOOL_OK) {
        cleanup_status = cleanup;
      }
    }
  }
  if (cleanup_status == CTOOL_OK) {
    cleanup_status =
        cupidasm_publication_remove_commit_records(paths, count);
  }
  status = cleanup_status;
  goto done;

rollback:
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup;
    if (backed_up[index] != 0) {
      cleanup = CTOOL_OK;
#if defined(CUPIDASM_PUBLICATION_TESTING)
      if (cupidasm_publication_test_fails(
              "CUPIDASM_TEST_FAIL_ROLLBACK_RESTORE_INDEX", index) != 0) {
        cleanup = CTOOL_ERR_IO;
      }
#endif
      if (cleanup == CTOOL_OK) {
        cleanup = cupidasm_publication_replace(paths[index].backup,
                                               paths[index].target);
      }
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
      if (cleanup == CTOOL_OK) {
        backed_up[index] = 0;
        published[index] = 0;
      }
    } else if (published[index] != 0 && tombstoned[index] != 0) {
      cleanup =
          cupidasm_publication_remove_if_present(paths[index].target);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
      if (cleanup == CTOOL_OK) {
        published[index] = 0;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup =
        cupidasm_publication_remove_if_present(paths[index].candidate);
    if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
      rollback_status = cleanup;
    }
    if (backed_up[index] == 0) {
      cleanup = cupidasm_publication_remove_if_present(paths[index].backup);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
    }
    if (tombstoned[index] != 0 && published[index] == 0) {
      cleanup =
          cupidasm_publication_remove_if_present(paths[index].absent);
      if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
        rollback_status = cleanup;
      }
    }
  }
  for (index = 0u; index < count; index++) {
    ctool_status_t cleanup =
        cupidasm_publication_remove_if_present(paths[index].commit);
    if (rollback_status == CTOOL_OK && cleanup != CTOOL_OK) {
      rollback_status = cleanup;
    }
  }
  if (rollback_status != CTOOL_OK) {
    status = rollback_status;
  }

done:
  free(commit_storage);
  for (index = 0u; index < count; index++) {
    cupidasm_publication_path_close(&paths[index]);
  }
  return status;
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
  cupidasm_output_path_kind_t publication_kind;
  int exit_code = 1;
  if (parsed < 0) {
    cupidasm_usage(stdout);
    return 0;
  }
  if (parsed == 0) {
    cupidasm_usage(stderr);
    return 2;
  }
  publication_kind = cupidasm_publication_kind(&cli);
  if (publication_kind == CUPIDASM_OUTPUT_PATH_INVALID_FD) {
    (void)fprintf(stderr, "cupidasm: invalid inherited output path\n");
    return 1;
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
    status = publication_kind == CUPIDASM_OUTPUT_PATH_INHERITED_FD
                 ? cupidasm_publish_inherited(&cli, &result)
                 : cupidasm_publish(&cli, &result);
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
