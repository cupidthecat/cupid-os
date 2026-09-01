#include "ctool.h"
#include "ctool_host.h"
#include "cupidld.h"

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
#if !defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#include <fcntl.h>
#endif
#endif

#define CUPIDLD_HOST_SOURCE_BYTES 67108864u
#define CUPIDLD_HOST_OUTPUT_BYTES 67108864u
#define CUPIDLD_HOST_ARENA_BYTES 268435456u
#define CUPIDLD_HOST_IMAGE_SPAN 67108864u
#define CUPIDLD_PUBLICATION_ATTEMPTS 4096u
#define CUPIDLD_PUBLICATION_VERIFY_BYTES 4096u

#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#define CUPIDLD_LINUX_SYS_WRITE 4
#define CUPIDLD_LINUX_SYS_OPEN 5
#define CUPIDLD_LINUX_SYS_CLOSE 6
#define CUPIDLD_LINUX_SYS_UNLINK 10
#define CUPIDLD_LINUX_SYS_RENAME 38
#define CUPIDLD_LINUX_O_WRONLY 1
#define CUPIDLD_LINUX_O_CREAT 64
#define CUPIDLD_LINUX_O_EXCL 128

int cupid_linux_syscall1(int number, unsigned int first);
int cupid_linux_syscall2(int number, unsigned int first,
                         unsigned int second);
int cupid_linux_syscall3(int number, unsigned int first,
                         unsigned int second, unsigned int third);
#endif

typedef struct {
  const char *machine;
  const char *script;
  const char *output;
  const char *entry;
  ctool_u32 text_address;
  ctool_bool have_text_address;
  const char **objects;
  ctool_u32 object_count;
  const char **imports;
  ctool_u32 import_count;
} cupidld_cli_t;

typedef struct {
#if defined(_WIN32)
  HANDLE handle;
#else
  int descriptor;
#endif
} cupidld_publication_file_t;

typedef struct {
  ctool_status_t (*open_exclusive)(const char *path,
                                   cupidld_publication_file_t *file_out);
  ctool_status_t (*write_all)(cupidld_publication_file_t *file,
                              ctool_bytes_t contents);
  ctool_status_t (*close)(cupidld_publication_file_t *file);
  ctool_status_t (*verify)(const char *candidate, ctool_bytes_t contents);
  ctool_status_t (*replace)(const char *candidate,
                            const char *destination);
  void (*discard)(const char *candidate);
} cupidld_publication_ops_t;

#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
static ctool_bool cupidld_linux_syscall_failed(int result) {
  return result < 0 && result >= -4095 ? CTOOL_TRUE : CTOOL_FALSE;
}
#endif

static ctool_status_t cupidld_publication_open(
    const char *path, cupidld_publication_file_t *file_out) {
  if (path == (const char *)0 || file_out == (cupidld_publication_file_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
#if defined(_WIN32)
  file_out->handle = CreateFileA(path, GENERIC_WRITE, 0, (LPSECURITY_ATTRIBUTES)0,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
  return file_out->handle == INVALID_HANDLE_VALUE ? CTOOL_ERR_IO : CTOOL_OK;
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
  file_out->descriptor = cupid_linux_syscall3(
      CUPIDLD_LINUX_SYS_OPEN, (unsigned int)path,
      CUPIDLD_LINUX_O_WRONLY | CUPIDLD_LINUX_O_CREAT |
          CUPIDLD_LINUX_O_EXCL,
      511u);
  return cupidld_linux_syscall_failed(file_out->descriptor) == CTOOL_TRUE
             ? CTOOL_ERR_IO
             : CTOOL_OK;
#else
  file_out->descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0777);
  return file_out->descriptor < 0 ? CTOOL_ERR_IO : CTOOL_OK;
#endif
}

static ctool_status_t cupidld_publication_verify(
    const char *candidate, ctool_bytes_t contents) {
  ctool_u8 buffer[CUPIDLD_PUBLICATION_VERIFY_BYTES];
  FILE *file;
  long file_size;
  ctool_u32 total = 0u;
  ctool_status_t status = CTOOL_OK;
  if (candidate == (const char *)0 ||
      (contents.data == (const ctool_u8 *)0 && contents.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
#if defined(_WIN32)
  file = (FILE *)0;
  if (fopen_s(&file, candidate, "rb") != 0) {
    file = (FILE *)0;
  }
#else
  file = fopen(candidate, "rb");
#endif
  if (file == (FILE *)0) {
    return CTOOL_ERR_IO;
  }
  if (fseek(file, 0l, SEEK_END) != 0) {
    status = CTOOL_ERR_IO;
  }
  file_size = status == CTOOL_OK ? ftell(file) : -1l;
  if (file_size < 0l || (unsigned long)file_size != contents.size) {
    status = CTOOL_ERR_IO;
  }
  if (status == CTOOL_OK && fseek(file, 0l, 0) != 0) {
    status = CTOOL_ERR_IO;
  }
  while (status == CTOOL_OK && total < contents.size) {
    ctool_u32 remaining = contents.size - total;
    ctool_u32 request =
        remaining < CUPIDLD_PUBLICATION_VERIFY_BYTES
            ? remaining
            : CUPIDLD_PUBLICATION_VERIFY_BYTES;
    size_t count = fread(buffer, 1u, (size_t)request, file);
    if (count != (size_t)request ||
        memcmp(buffer, contents.data + total, count) != 0) {
      status = CTOOL_ERR_IO;
    } else {
      total += request;
    }
  }
  if (fclose(file) != 0) {
    status = CTOOL_ERR_IO;
  }
  return status;
}

static ctool_status_t cupidld_publication_write_all(
    cupidld_publication_file_t *file, ctool_bytes_t contents) {
  ctool_u32 total = 0u;
  if (file == (cupidld_publication_file_t *)0 ||
      (contents.data == (const ctool_u8 *)0 && contents.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (total < contents.size) {
    ctool_u32 remaining = contents.size - total;
#if defined(_WIN32)
    DWORD written = 0u;
    if (WriteFile(file->handle, contents.data + total, (DWORD)remaining,
                  &written, (LPOVERLAPPED)0) == 0 ||
        written == 0u || written > (DWORD)remaining) {
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)written;
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
    int written = cupid_linux_syscall3(
        CUPIDLD_LINUX_SYS_WRITE, (unsigned int)file->descriptor,
        (unsigned int)(contents.data + total), (unsigned int)remaining);
    if (cupidld_linux_syscall_failed(written) == CTOOL_TRUE || written == 0 ||
        (unsigned int)written > remaining) {
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)(unsigned int)written;
#else
    ssize_t written = write(file->descriptor, contents.data + total,
                            (size_t)remaining);
    if (written <= (ssize_t)0 || (size_t)written > (size_t)remaining) {
      return CTOOL_ERR_IO;
    }
    total += (ctool_u32)(size_t)written;
#endif
  }
  return CTOOL_OK;
}

static ctool_status_t cupidld_publication_close(
    cupidld_publication_file_t *file) {
  if (file == (cupidld_publication_file_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
#if defined(_WIN32)
  {
    BOOL flushed = FlushFileBuffers(file->handle);
    BOOL closed = CloseHandle(file->handle);
    file->handle = INVALID_HANDLE_VALUE;
    return flushed != 0 && closed != 0 ? CTOOL_OK : CTOOL_ERR_IO;
  }
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
  {
    int result = cupid_linux_syscall1(
        CUPIDLD_LINUX_SYS_CLOSE, (unsigned int)file->descriptor);
    file->descriptor = -1;
    return cupidld_linux_syscall_failed(result) == CTOOL_TRUE
               ? CTOOL_ERR_IO
               : CTOOL_OK;
  }
#else
  {
    int result = close(file->descriptor);
    file->descriptor = -1;
    return result == 0 ? CTOOL_OK : CTOOL_ERR_IO;
  }
#endif
}

static ctool_status_t cupidld_publication_replace(
    const char *candidate, const char *destination) {
#if defined(_WIN32)
  return MoveFileExA(candidate, destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0
             ? CTOOL_OK
             : CTOOL_ERR_IO;
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
  int result = cupid_linux_syscall2(
      CUPIDLD_LINUX_SYS_RENAME, (unsigned int)candidate,
      (unsigned int)destination);
  return cupidld_linux_syscall_failed(result) == CTOOL_TRUE ? CTOOL_ERR_IO
                                                            : CTOOL_OK;
#else
  return rename(candidate, destination) == 0 ? CTOOL_OK : CTOOL_ERR_IO;
#endif
}

static void cupidld_publication_discard(const char *candidate) {
#if defined(_WIN32)
  (void)DeleteFileA(candidate);
#elif defined(CUPID_HOSTED_I386_LINUX_ABI_H)
  (void)cupid_linux_syscall1(CUPIDLD_LINUX_SYS_UNLINK,
                             (unsigned int)candidate);
#else
  (void)unlink(candidate);
#endif
}

static ctool_status_t cupidld_publish_output_with_ops(
    const char *destination, ctool_bytes_t contents,
    const cupidld_publication_ops_t *ops) {
  static const char suffix[] = ".cupid-tmp-00000000";
  static const char digits[] = "0123456789abcdef";
  size_t destination_size;
  size_t candidate_size;
  size_t digit_start;
  char *candidate;
  cupidld_publication_file_t file;
  ctool_u32 attempt;
  ctool_bool opened = CTOOL_FALSE;
  ctool_status_t status = CTOOL_ERR_IO;
  if (destination == (const char *)0 || destination[0] == '\0' ||
      (contents.data == (const ctool_u8 *)0 && contents.size != 0u) ||
      ops == (const cupidld_publication_ops_t *)0 ||
      ops->open_exclusive == (ctool_status_t (*)(
                                   const char *,
                                   cupidld_publication_file_t *))0 ||
      ops->write_all == (ctool_status_t (*)(cupidld_publication_file_t *,
                                            ctool_bytes_t))0 ||
      ops->close == (ctool_status_t (*)(cupidld_publication_file_t *))0 ||
      ops->verify ==
          (ctool_status_t (*)(const char *, ctool_bytes_t))0 ||
      ops->replace ==
          (ctool_status_t (*)(const char *, const char *))0 ||
      ops->discard == (void (*)(const char *))0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  destination_size = strlen(destination);
  if (destination_size > (size_t)-1 - sizeof(suffix)) {
    return CTOOL_ERR_OVERFLOW;
  }
  candidate_size = destination_size + sizeof(suffix) - 1u;
  candidate = (char *)malloc(candidate_size + 1u);
  if (candidate == (char *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  (void)memcpy(candidate, destination, destination_size);
  (void)memcpy(candidate + destination_size, suffix, sizeof(suffix));
  digit_start = candidate_size - 8u;
  for (attempt = 0u; attempt < CUPIDLD_PUBLICATION_ATTEMPTS; attempt++) {
    ctool_u32 value = attempt;
    ctool_u32 digit;
    for (digit = 0u; digit < 8u; digit++) {
      candidate[digit_start + 7u - digit] = digits[value & 15u];
      value >>= 4u;
    }
    status = ops->open_exclusive(candidate, &file);
    if (status == CTOOL_OK) {
      opened = CTOOL_TRUE;
      break;
    }
  }
  if (opened == CTOOL_FALSE) {
    free(candidate);
    return status;
  }
  status = ops->write_all(&file, contents);
  {
    ctool_status_t close_status = ops->close(&file);
    if (status == CTOOL_OK) {
      status = close_status;
    }
  }
  if (status == CTOOL_OK) {
    status = ops->verify(candidate, contents);
  }
  if (status == CTOOL_OK) {
    status = ops->replace(candidate, destination);
  }
  if (status != CTOOL_OK) {
    ops->discard(candidate);
  }
  free(candidate);
  return status;
}

static ctool_status_t cupidld_publish_output(const char *destination,
                                             ctool_bytes_t contents) {
  static const cupidld_publication_ops_t ops = {
      cupidld_publication_open, cupidld_publication_write_all,
      cupidld_publication_close, cupidld_publication_verify,
      cupidld_publication_replace, cupidld_publication_discard};
  return cupidld_publish_output_with_ops(destination, contents, &ops);
}

static void cupidld_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidld -m elf_i386 -T SCRIPT -o OUTPUT OBJECT...\n"
      "       cupidld -m elf_i386 --text-address ADDRESS --entry SYMBOL "
      "-o OUTPUT OBJECT...\n"
      "       cupidld -m i386pe --text-address 0x00401000 --entry SYMBOL "
      "[--import IAT_SYMBOL=LIBRARY:PROCEDURE]... -o OUTPUT OBJECT...\n");
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
  const char **imports = cli->imports;
  int index;
  (void)memset(cli, 0, sizeof(*cli));
  cli->objects = objects;
  cli->imports = imports;
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
    taken = cupidld_take_value(argc, argv, &index, argument, "--import",
                               &value);
    if (taken != 0) {
      if (taken < 0 || value[0] == '\0') {
        return 0;
      }
      cli->imports[cli->import_count] = value;
      cli->import_count++;
      continue;
    }
    if (argument[0] == '-') {
      return 0;
    }
    cli->objects[cli->object_count] = argument;
    cli->object_count++;
  }
  if (cli->machine == (const char *)0 ||
      (strcmp(cli->machine, "elf_i386") != 0 &&
       strcmp(cli->machine, "i386pe") != 0) ||
      cli->output == (const char *)0 || cli->object_count == 0u) {
    return 0;
  }
  if (cli->script != (const char *)0) {
    if (strcmp(cli->machine, "i386pe") == 0 ||
        cli->have_text_address == CTOOL_TRUE ||
        cli->entry != (const char *)0) {
      return 0;
    }
  } else if (cli->have_text_address == CTOOL_FALSE ||
             cli->entry == (const char *)0) {
    return 0;
  }
  if (cli->import_count != 0u && strcmp(cli->machine, "i386pe") != 0) {
    return 0;
  }
  return 1;
}

static int cupidld_parse_import(const char *text,
                                ctool_ld_pe32_import_t *import_out) {
  const char *equals;
  const char *colon;
  size_t symbol_size;
  size_t library_size;
  size_t procedure_size;
  if (text == (const char *)0 ||
      import_out == (ctool_ld_pe32_import_t *)0) {
    return 0;
  }
  equals = strchr(text, '=');
  colon = equals == (const char *)0 ? (const char *)0
                                    : strchr(equals + 1, ':');
  if (equals == (const char *)0 || colon == (const char *)0 ||
      equals == text || colon == equals + 1 || colon[1] == '\0') {
    return 0;
  }
  symbol_size = (size_t)(equals - text);
  library_size = (size_t)(colon - equals - 1);
  procedure_size = strlen(colon + 1);
  if (symbol_size > 4294967295u || library_size > 4294967295u ||
      procedure_size > 4294967295u) {
    return 0;
  }
  import_out->symbol_name.data = text;
  import_out->symbol_name.size = (ctool_u32)symbol_size;
  import_out->library_name.data = equals + 1;
  import_out->library_name.size = (ctool_u32)library_size;
  import_out->procedure_name.data = colon + 1;
  import_out->procedure_name.size = (ctool_u32)procedure_size;
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
  const char **cli_imports;
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
  ctool_ld_pe32_import_t *imports = (ctool_ld_pe32_import_t *)0;
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
  cli_imports = (const char **)calloc((size_t)(argc > 0 ? argc : 1),
                                      sizeof(const char *));
  if (cli_objects == (const char **)0 ||
      cli_imports == (const char **)0) {
    (void)fprintf(stderr, "cupidld: argument allocation failed\n");
    free(cli_imports);
    free(cli_objects);
    return 1;
  }
  (void)memset(&cli, 0, sizeof(cli));
  cli.objects = cli_objects;
  cli.imports = cli_imports;
  parsed = cupidld_parse_cli(argc, argv, &cli);
  if (parsed < 0) {
    cupidld_usage(stdout);
    free(cli_imports);
    free(cli_objects);
    return 0;
  }
  if (parsed == 0) {
    cupidld_usage(stderr);
    free(cli_imports);
    free(cli_objects);
    return 2;
  }
  if (cli.import_count != 0u) {
    imports = (ctool_ld_pe32_import_t *)calloc(
        (size_t)cli.import_count, sizeof(ctool_ld_pe32_import_t));
    if (imports == (ctool_ld_pe32_import_t *)0) {
      (void)fprintf(stderr, "cupidld: import allocation failed\n");
      free(cli_imports);
      free(cli_objects);
      return 1;
    }
    for (index = 0u; index < cli.import_count; index++) {
      if (cupidld_parse_import(cli.imports[index], &imports[index]) == 0) {
        cupidld_usage(stderr);
        free(imports);
        free(cli_imports);
        free(cli_objects);
        return 2;
      }
    }
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
  request.image_kind = strcmp(cli.machine, "i386pe") == 0
                           ? CTOOL_LD_IMAGE_PE32_FIXED
                           : CTOOL_LD_IMAGE_ELF32;
  request.pe32_imports = imports;
  request.pe32_import_count = cli.import_count;
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
    status = cupidld_publish_output(native_paths[output_native_index],
                                    ctool_buffer_view(output));
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
  free(imports);
  free(cli_imports);
  free(cli_objects);
  return exit_code;
}
