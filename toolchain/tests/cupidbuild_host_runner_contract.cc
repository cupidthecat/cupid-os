#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#endif
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cupidbuild_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if !defined(CUPIDBUILD_HOST_STREAM_LIMIT)
#error "the host runner contract requires an explicit stream limit"
#endif
#if CUPIDBUILD_HOST_STREAM_LIMIT != 4096
#error "the host runner contract requires its 4096-byte test boundary"
#endif

#define CONTRACT_PATH_BYTES 8192u
#define CONTRACT_ERROR_BYTES 512u
#define CONTRACT_PRIVATE_PREFIX ".cupidbuild-run-"

#if defined(CUPIDBUILD_HOST_NATIVE_EINTR_TEST) && !defined(_WIN32)
#define CONTRACT_EINTR_READ 1u
#define CONTRACT_EINTR_WRITE 2u
#define CONTRACT_EINTR_WAIT_NOHANG 3u
#define CONTRACT_EINTR_WAIT_BLOCKING 4u

void cupidbuild_host_native_eintr_test_arm(unsigned int operation);
int cupidbuild_host_native_eintr_test_retry_observed(
    unsigned int operation);
#endif

static int contract_copy_text(char *destination, size_t capacity,
                              const char *source) {
  size_t size = strlen(source);
  if (size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, source, size + 1u);
  return 1;
}

static int contract_join(char *destination, size_t capacity,
                         const char *directory, const char *name) {
  int written = snprintf(destination, capacity, "%s/%s", directory, name);
  return written >= 0 && (size_t)written < capacity;
}

static unsigned int contract_process_id(void) {
#if defined(_WIN32)
  return (unsigned int)GetCurrentProcessId();
#else
  return (unsigned int)getpid();
#endif
}

static int contract_current_directory(char *path, size_t capacity) {
#if defined(_WIN32)
  DWORD size = GetCurrentDirectoryA((DWORD)capacity, path);
  return size != 0u && (size_t)size < capacity;
#else
  return getcwd(path, capacity) != (char *)0;
#endif
}

static int contract_self_path(const char *argument_zero, char *path,
                              size_t capacity) {
#if defined(_WIN32)
  DWORD size = GetModuleFileNameA((HMODULE)0, path, (DWORD)capacity);
  (void)argument_zero;
  return size != 0u && (size_t)size < capacity;
#else
  (void)capacity;
  return realpath(argument_zero, path) != (char *)0;
#endif
}

static int contract_make_directory(const char *path) {
#if defined(_WIN32)
  return _mkdir(path) == 0;
#else
  return mkdir(path, 0700) == 0;
#endif
}

static int contract_remove_directory(const char *path) {
#if defined(_WIN32)
  return _rmdir(path) == 0;
#else
  return rmdir(path) == 0;
#endif
}

static int contract_remove_file(const char *path) {
#if defined(_WIN32)
  return DeleteFileA(path) != 0;
#else
  return unlink(path) == 0;
#endif
}

static int contract_rename(const char *source, const char *destination) {
#if defined(_WIN32)
  return MoveFileA(source, destination) != 0;
#else
  return rename(source, destination) == 0;
#endif
}

static int contract_file_exists(const char *path) {
#if defined(_WIN32)
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
#else
  struct stat information;
  return stat(path, &information) == 0 && S_ISREG(information.st_mode);
#endif
}

static int contract_write_file(const char *path, const char *bytes,
                               size_t size) {
  FILE *stream = fopen(path, "wb");
  int success;
  if (stream == (FILE *)0) {
    return 0;
  }
  success = fwrite(bytes, 1u, size, stream) == size && fflush(stream) == 0 &&
            ferror(stream) == 0;
  if (fclose(stream) != 0) {
    success = 0;
  }
  return success;
}

static int contract_file_equals(const char *path, const char *expected) {
  unsigned char bytes[256];
  size_t expected_size = strlen(expected);
  FILE *stream = fopen(path, "rb");
  size_t size;
  int trailing;
  if (stream == (FILE *)0 || expected_size > sizeof(bytes)) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return 0;
  }
  size = fread(bytes, 1u, sizeof(bytes), stream);
  trailing = fgetc(stream);
  if (fclose(stream) != 0 || size != expected_size || trailing != EOF) {
    return 0;
  }
  return memcmp(bytes, expected, expected_size) == 0;
}

static int contract_create_root(char *path, size_t capacity) {
  char current[CONTRACT_PATH_BYTES];
  unsigned int attempt;
  if (!contract_current_directory(current, sizeof(current))) {
    return 0;
  }
  for (attempt = 0u; attempt < 4096u; attempt++) {
    int written = snprintf(
        path, capacity, "%s/.cupidbuild-host-runner-contract-%08x-%08x",
        current, contract_process_id(), attempt);
    if (written < 0 || (size_t)written >= capacity) {
      return 0;
    }
    if (contract_make_directory(path)) {
      return 1;
    }
  }
  return 0;
}

static int contract_private_root_count(const char *directory) {
  int count = 0;
#if defined(_WIN32)
  char pattern[CONTRACT_PATH_BYTES];
  WIN32_FIND_DATAA found;
  HANDLE search;
  if (!contract_join(pattern, sizeof(pattern), directory,
                     CONTRACT_PRIVATE_PREFIX "*")) {
    return -1;
  }
  search = FindFirstFileA(pattern, &found);
  if (search == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND ? 0 : -1;
  }
  do {
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
        strncmp(found.cFileName, CONTRACT_PRIVATE_PREFIX,
                strlen(CONTRACT_PRIVATE_PREFIX)) == 0) {
      count++;
    }
  } while (FindNextFileA(search, &found) != 0);
  (void)FindClose(search);
#else
  DIR *stream = opendir(directory);
  struct dirent *entry;
  if (stream == (DIR *)0) {
    return -1;
  }
  while ((entry = readdir(stream)) != (struct dirent *)0) {
    if (strncmp(entry->d_name, CONTRACT_PRIVATE_PREFIX,
                strlen(CONTRACT_PRIVATE_PREFIX)) == 0) {
      count++;
    }
  }
  if (closedir(stream) != 0) {
    return -1;
  }
#endif
  return count;
}

static void contract_capture_error(
    const cupidbuild_host_transaction_t *transaction, char *error,
    size_t capacity) {
  const char *message = cupidbuild_host_error(transaction);
  if (message == (const char *)0 ||
      !contract_copy_text(error, capacity, message)) {
    if (capacity != 0u) {
      error[0] = '\0';
    }
  }
}

static int contract_open_self_runner(
    const char *root, const char *self,
    cupidbuild_host_transaction_t **transaction_out,
    const char **frozen_self_out) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
#if defined(_WIN32)
  const char *private_name = "runner-self.exe";
#else
  const char *private_name = "runner-self";
#endif
  if (!cupidbuild_host_runner_open(root, &transaction) ||
      !cupidbuild_host_freeze_input(
          transaction, self, private_name, frozen_self_out,
          (cupidbuild_host_snapshot_t *)0) ||
      !cupidbuild_host_make_input_executable(transaction, *frozen_self_out)) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      (void)fprintf(stderr, "runner setup: %s\n",
                    cupidbuild_host_error(transaction));
    }
    (void)cupidbuild_host_transaction_close(transaction);
    return 0;
  }
  *transaction_out = transaction;
  return 1;
}

static int contract_run_self(const char *root, const char *self,
                             const char *const *arguments,
                             int *result_out, int *forwarded_out,
                             int *closed_out, char *error,
                             size_t error_capacity) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen_self = (const char *)0;
  if (!contract_open_self_runner(root, self, &transaction, &frozen_self)) {
    return 0;
  }
  *result_out = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, 30000u);
  contract_capture_error(transaction, error, error_capacity);
  *forwarded_out = *result_out >= 0
                       ? cupidbuild_host_forward_captured(transaction)
                       : 0;
  *closed_out = cupidbuild_host_transaction_close(transaction);
  return 1;
}

static int contract_expect_no_private_roots(const char *root,
                                            const char *test_name) {
  int count = contract_private_root_count(root);
  if (count != 0) {
    (void)fprintf(stderr, "%s: %d private runner roots remain\n", test_name,
                  count);
    return 0;
  }
  return 1;
}

static int contract_test_capture_without_forwarding(const char *root,
                                                    const char *self) {
  static const char *const arguments[] = {
      "--child-exit", "0", (const char *)0};
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen_self = (const char *)0;
  char error[CONTRACT_ERROR_BYTES];
  int result;
  int closed;
  int success = 1;
  error[0] = '\0';
  if (!contract_open_self_runner(root, self, &transaction, &frozen_self)) {
    return 0;
  }
  result = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, 30000u);
  contract_capture_error(transaction, error, sizeof(error));
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != 0 || closed == 0) {
    (void)fprintf(stderr,
                  "capture without forwarding: status=%d close=%d error=%s\n",
                  result, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root,
                                        "capture without forwarding")) {
    success = 0;
  }
  return success;
}

static int contract_test_exact_exits(const char *root, const char *self) {
  static const char *const arguments_124[] = {
      "--child-exit", "124", (const char *)0};
  static const char *const arguments_125[] = {
      "--child-exit", "125", (const char *)0};
  const char *const *cases[] = {arguments_124, arguments_125};
  const int expected[] = {124, 125};
  size_t index;
  int success = 1;
  for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
    int result = -1;
    int forwarded = 0;
    int closed = 0;
    char error[CONTRACT_ERROR_BYTES];
    error[0] = '\0';
    if (!contract_run_self(root, self, cases[index], &result, &forwarded,
                           &closed, error, sizeof(error))) {
      success = 0;
      continue;
    }
    if (result != expected[index] || forwarded == 0 || closed == 0) {
      (void)fprintf(stderr,
                    "exact child exit %d: status=%d forwarded=%d close=%d "
                    "error=%s\n",
                    expected[index], result, forwarded, closed, error);
      success = 0;
    }
    if (!contract_expect_no_private_roots(root, "exact child exit")) {
      success = 0;
    }
  }
  return success;
}

static int contract_test_non_executable(const char *root) {
  char source[CONTRACT_PATH_BYTES];
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen = (const char *)0;
  static const char *const no_arguments[] = {(const char *)0};
  char error[CONTRACT_ERROR_BYTES];
  int result = 0;
  int closed = 0;
  int success = 1;
  if (!contract_join(source, sizeof(source), root, "not-a-program.txt") ||
      !contract_write_file(source, "plain text\n", 11u) ||
      !cupidbuild_host_runner_open(root, &transaction) ||
      !cupidbuild_host_freeze_input(
          transaction, source, "not-a-program", &frozen,
          (cupidbuild_host_snapshot_t *)0)) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      (void)fprintf(stderr, "non-executable setup: %s\n",
                    cupidbuild_host_error(transaction));
    }
    (void)cupidbuild_host_transaction_close(transaction);
    (void)contract_remove_file(source);
    return 0;
  }
  result = cupidbuild_host_run_captured(
      transaction, frozen, no_arguments, 30000u);
  contract_capture_error(transaction, error, sizeof(error));
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != -1 || error[0] == '\0' || closed == 0) {
    (void)fprintf(stderr,
                  "non-executable launch: status=%d close=%d error=%s\n",
                  result, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root, "non-executable launch")) {
    success = 0;
  }
  if (!contract_remove_file(source)) {
    (void)fprintf(stderr, "non-executable launch: source cleanup failed\n");
    success = 0;
  }
  return success;
}

static int contract_test_stream_limit(const char *root, const char *self) {
  static const char *const arguments_4096[] = {
      "--child-stream", "4096", (const char *)0};
  static const char *const arguments_4097[] = {
      "--child-stream", "4097", (const char *)0};
  static const char *const stderr_arguments_4097[] = {
      "--child-stderr-stream", "4097", (const char *)0};
  const char *const *cases[] = {
      arguments_4096, arguments_4097, stderr_arguments_4097};
  const int expected[] = {0, -1, -1};
  size_t index;
  int success = 1;
  for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
    cupidbuild_host_transaction_t *transaction =
        (cupidbuild_host_transaction_t *)0;
    const char *frozen_self = (const char *)0;
    int result;
    int closed;
    char error[CONTRACT_ERROR_BYTES];
    error[0] = '\0';
    if (!contract_open_self_runner(root, self, &transaction, &frozen_self)) {
      success = 0;
      continue;
    }
    result = cupidbuild_host_run_captured(
        transaction, frozen_self, cases[index], 30000u);
    contract_capture_error(transaction, error, sizeof(error));
    closed = cupidbuild_host_transaction_close(transaction);
    if (result != expected[index] || closed == 0 ||
        (expected[index] < 0 && error[0] == '\0')) {
      (void)fprintf(stderr,
                    "%s size %s: status=%d close=%d error=%s\n",
                    cases[index][0], cases[index][1], result, closed, error);
      success = 0;
    }
    if (!contract_expect_no_private_roots(root, "stream limit")) {
      success = 0;
    }
  }
  return success;
}

static int contract_test_windows_command_limit(const char *root,
                                               const char *self) {
#if defined(_WIN32)
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen_self = (const char *)0;
  char *long_argument = (char *)malloc(32769u);
  const char *arguments[2];
  char error[CONTRACT_ERROR_BYTES];
  int result;
  int closed;
  int success = 1;
  if (long_argument == (char *)0) {
    return 0;
  }
  (void)memset(long_argument, 'a', 32768u);
  long_argument[32768] = '\0';
  arguments[0] = long_argument;
  arguments[1] = (const char *)0;
  error[0] = '\0';
  if (!contract_open_self_runner(root, self, &transaction, &frozen_self)) {
    free(long_argument);
    return 0;
  }
  result = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, 30000u);
  contract_capture_error(transaction, error, sizeof(error));
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != -1 || closed == 0 || error[0] == '\0') {
    (void)fprintf(stderr,
                  "oversized Windows command: status=%d close=%d error=%s\n",
                  result, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root,
                                        "oversized Windows command")) {
    success = 0;
  }
  free(long_argument);
  return success;
#else
  (void)root;
  (void)self;
  return 1;
#endif
}

static int contract_test_arguments(const char *root, const char *self) {
  static const char *const arguments[] = {
      "--child-argv",
      "plain",
      "",
      "two words",
      "embedded\"quote",
      "trailing" "\\\\",
      "slashes" "\\\\" "\"" "quote",
      (const char *)0};
  static const char expected[] =
      "5:plain\n"
      "0:\n"
      "9:two words\n"
      "14:embedded\"quote\n"
      "10:trailing" "\\\\" "\n"
      "15:slashes" "\\\\" "\"" "quote\n";
  char marker[CONTRACT_PATH_BYTES];
  int result = -1;
  int forwarded = 0;
  int closed = 0;
  char error[CONTRACT_ERROR_BYTES];
  int success = 1;
  error[0] = '\0';
  if (!contract_join(marker, sizeof(marker), root, "argv.marker") ||
      !contract_run_self(root, self, arguments, &result, &forwarded, &closed,
                         error, sizeof(error))) {
    return 0;
  }
  if (result != 0 || forwarded == 0 || closed == 0 ||
      !contract_file_equals(marker, expected)) {
    (void)fprintf(stderr,
                  "argument roundtrip: status=%d forwarded=%d close=%d "
                  "error=%s\n",
                  result, forwarded, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root, "argument roundtrip")) {
    success = 0;
  }
  if (!contract_remove_file(marker)) {
    (void)fprintf(stderr, "argument roundtrip: marker cleanup failed\n");
    success = 0;
  }
  return success;
}

static int contract_test_forwarding(const char *root, const char *self,
                                    const char *const *arguments,
                                    const char *test_name) {
  int result = -1;
  int forwarded = 0;
  int closed = 0;
  char error[CONTRACT_ERROR_BYTES];
  int success = 1;
  error[0] = '\0';
  if (!contract_run_self(root, self, arguments, &result, &forwarded, &closed,
                         error, sizeof(error))) {
    return 0;
  }
  if (result != 0 || forwarded == 0 || closed == 0) {
    (void)fprintf(stderr,
                  "%s: status=%d forwarded=%d close=%d error=%s\n",
                  test_name, result, forwarded, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root, test_name)) {
    success = 0;
  }
  return success;
}

static int contract_test_stdout_limit_forward(const char *root,
                                              const char *self) {
  static const char *const arguments[] = {
      "--child-stream", "4096", (const char *)0};
  return contract_test_forwarding(root, self, arguments,
                                  "stdout boundary forwarding");
}

static int contract_test_stream_pair_forward(const char *root,
                                             const char *self) {
  static const char *const arguments[] = {
      "--child-stream-pair", (const char *)0};
  return contract_test_forwarding(root, self, arguments,
                                  "paired stream forwarding");
}

static int contract_test_stream_prefix_forward(const char *root,
                                               const char *self) {
  static char stdout_buffer[256];
  static char stderr_buffer[256];
  if (setvbuf(stdout, stdout_buffer, _IOFBF, sizeof(stdout_buffer)) != 0 ||
      setvbuf(stderr, stderr_buffer, _IOFBF, sizeof(stderr_buffer)) != 0 ||
      fputs("caller stdout prefix\n", stdout) == EOF ||
      fputs("caller stderr prefix\n", stderr) == EOF) {
    return 0;
  }
  return contract_test_stream_pair_forward(root, self);
}

#if defined(CUPIDBUILD_HOST_NATIVE_EINTR_TEST) && !defined(_WIN32)
static int contract_test_eintr_run(const char *root, const char *self,
                                   const char *const *arguments,
                                   unsigned int timeout_milliseconds,
                                   unsigned int operation,
                                   int expected_status,
                                   const char *test_name) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen_self = (const char *)0;
  char error[CONTRACT_ERROR_BYTES];
  int result;
  int retried;
  int closed;
  int success = 1;
  error[0] = '\0';
  if (!contract_open_self_runner(root, self, &transaction, &frozen_self)) {
    return 0;
  }
  cupidbuild_host_native_eintr_test_arm(operation);
  result = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, timeout_milliseconds);
  contract_capture_error(transaction, error, sizeof(error));
  retried = cupidbuild_host_native_eintr_test_retry_observed(operation);
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != expected_status || retried == 0 || closed == 0 ||
      (expected_status < 0 && error[0] == '\0')) {
    (void)fprintf(stderr,
                  "%s: status=%d retry=%d close=%d error=%s\n",
                  test_name, result, retried, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root, test_name)) {
    success = 0;
  }
  return success;
}

static int contract_test_eintr_write(const char *root) {
  char source[CONTRACT_PATH_BYTES];
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen = (const char *)0;
  static const char *const no_arguments[] = {(const char *)0};
  char error[CONTRACT_ERROR_BYTES];
  int result;
  int retried;
  int closed;
  int success = 1;
  error[0] = '\0';
  if (!contract_join(source, sizeof(source), root,
                     "eintr-not-a-program.txt") ||
      !contract_write_file(source, "plain text\n", 11u) ||
      !cupidbuild_host_runner_open(root, &transaction) ||
      !cupidbuild_host_freeze_input(
          transaction, source, "eintr-not-a-program", &frozen,
          (cupidbuild_host_snapshot_t *)0)) {
    (void)cupidbuild_host_transaction_close(transaction);
    (void)contract_remove_file(source);
    return 0;
  }
  cupidbuild_host_native_eintr_test_arm(CONTRACT_EINTR_WRITE);
  result = cupidbuild_host_run_captured(
      transaction, frozen, no_arguments, 30000u);
  contract_capture_error(transaction, error, sizeof(error));
  retried = cupidbuild_host_native_eintr_test_retry_observed(
      CONTRACT_EINTR_WRITE);
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != -1 || retried == 0 || closed == 0 || error[0] == '\0') {
    (void)fprintf(stderr,
                  "launch-status write EINTR: status=%d retry=%d close=%d "
                  "error=%s\n",
                  result, retried, closed, error);
    success = 0;
  }
  if (!contract_expect_no_private_roots(root,
                                        "launch-status write EINTR")) {
    success = 0;
  }
  if (!contract_remove_file(source)) {
    success = 0;
  }
  return success;
}

static int contract_test_native_posix_eintr(const char *root,
                                            const char *self) {
  static const char *const exit_125[] = {
      "--child-exit", "125", (const char *)0};
  static const char *const exit_zero[] = {
      "--child-exit", "0", (const char *)0};
  static const char *const linger[] = {
      "--child-linger", (const char *)0};
  int success = 1;
  if (!contract_test_eintr_run(
          root, self, exit_125, 30000u, CONTRACT_EINTR_READ, 125,
          "launch-status read EINTR")) {
    success = 0;
  }
  if (!contract_test_eintr_write(root)) {
    success = 0;
  }
  if (!contract_test_eintr_run(
          root, self, exit_zero, 30000u, CONTRACT_EINTR_WAIT_NOHANG, 0,
          "nonblocking wait EINTR")) {
    success = 0;
  }
  if (!contract_test_eintr_run(
          root, self, linger, 1u, CONTRACT_EINTR_WAIT_BLOCKING, -2,
          "blocking wait EINTR")) {
    success = 0;
  }
  cupidbuild_host_native_eintr_test_arm(0u);
  return success;
}
#endif

static int contract_test_working_directory(const char *base,
                                           const char *self) {
  char original[CONTRACT_PATH_BYTES];
  char renamed[CONTRACT_PATH_BYTES];
  char original_marker[CONTRACT_PATH_BYTES];
  char renamed_marker[CONTRACT_PATH_BYTES];
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  const char *frozen_self = (const char *)0;
  static const char *const arguments[] = {
      "--child-marker", (const char *)0};
  int rename_while_open;
  int result = -1;
  int closed = 0;
  int success = 1;
  if (!contract_join(original, sizeof(original), base, "cwd-original") ||
      !contract_join(renamed, sizeof(renamed), base, "cwd-renamed") ||
      !contract_join(original_marker, sizeof(original_marker), original,
                     "cwd.marker") ||
      !contract_join(renamed_marker, sizeof(renamed_marker), renamed,
                     "cwd.marker") ||
      !contract_make_directory(original) ||
      !contract_open_self_runner(original, self, &transaction,
                                 &frozen_self)) {
    (void)contract_remove_directory(original);
    return 0;
  }
  rename_while_open = contract_rename(original, renamed);
#if defined(_WIN32)
  if (rename_while_open != 0) {
    if (!contract_rename(renamed, original)) {
      closed = cupidbuild_host_transaction_close(transaction);
      (void)contract_rename(renamed, original);
      (void)contract_remove_directory(original);
      (void)fprintf(stderr,
                    "working directory: Windows rename could not be restored "
                    "(close=%d)\n",
                    closed);
      return 0;
    }
  }
  result = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, 30000u);
  closed = cupidbuild_host_transaction_close(transaction);
  if (rename_while_open != 0 || result != 0 || closed == 0 ||
      !contract_file_exists(original_marker) ||
      !contract_expect_no_private_roots(original, "working directory")) {
    (void)fprintf(stderr,
                  "working directory: Windows rename=%d status=%d close=%d\n",
                  rename_while_open, result, closed);
    success = 0;
  }
  if (contract_file_exists(original_marker)) {
    (void)contract_remove_file(original_marker);
  }
  if (!contract_remove_directory(original)) {
    success = 0;
  }
#else
  if (rename_while_open == 0 || !contract_make_directory(original)) {
    closed = cupidbuild_host_transaction_close(transaction);
    if (rename_while_open != 0) {
      (void)contract_rename(renamed, original);
    }
    (void)contract_remove_directory(original);
    (void)fprintf(stderr,
                  "working directory: POSIX rename or replacement failed "
                  "(close=%d)\n",
                  closed);
    return 0;
  }
  result = cupidbuild_host_run_captured(
      transaction, frozen_self, arguments, 30000u);
  closed = cupidbuild_host_transaction_close(transaction);
  if (result != 0 || closed == 0 ||
      !contract_file_exists(renamed_marker) ||
      contract_file_exists(original_marker) ||
      !contract_expect_no_private_roots(original, "working directory") ||
      !contract_expect_no_private_roots(renamed, "working directory")) {
    (void)fprintf(stderr,
                  "working directory: POSIX status=%d close=%d\n", result,
                  closed);
    success = 0;
  }
  if (contract_file_exists(renamed_marker)) {
    (void)contract_remove_file(renamed_marker);
  }
  if (!contract_remove_directory(original) ||
      !contract_remove_directory(renamed)) {
    success = 0;
  }
#endif
  return success;
}

static int contract_child_exit(int argc, char **argv) {
  char *end = (char *)0;
  unsigned long status;
  if (argc != 3) {
    return 250;
  }
  errno = 0;
  status = strtoul(argv[2], &end, 10);
  if (errno != 0 || end == argv[2] || *end != '\0' || status > 255u) {
    return 251;
  }
  return (int)status;
}

static int contract_child_stream(int argc, char **argv, FILE *stream) {
  char bytes[256];
  char *end = (char *)0;
  unsigned long size;
  unsigned long offset = 0u;
  if (argc != 3) {
    return 252;
  }
  errno = 0;
  size = strtoul(argv[2], &end, 10);
  if (errno != 0 || end == argv[2] || *end != '\0' || size > 65536u) {
    return 253;
  }
  (void)memset(bytes, 'x', sizeof(bytes));
  while (offset < size) {
    size_t remaining = (size_t)(size - offset);
    size_t amount = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
    if (fwrite(bytes, 1u, amount, stream) != amount) {
      return 254;
    }
    offset += (unsigned long)amount;
  }
  return fflush(stream) == 0 && ferror(stream) == 0 ? 0 : 255;
}

static int contract_child_stream_pair(int argc) {
  static const char stdout_bytes[] = "runner stdout\n";
  static const char stderr_bytes[] = "runner stderr\n";
  int success;
  if (argc != 2) {
    return 239;
  }
  success = fwrite(stdout_bytes, 1u, sizeof(stdout_bytes) - 1u, stdout) ==
                sizeof(stdout_bytes) - 1u &&
            fwrite(stderr_bytes, 1u, sizeof(stderr_bytes) - 1u, stderr) ==
                sizeof(stderr_bytes) - 1u &&
            fflush(stdout) == 0 && ferror(stdout) == 0 &&
            fflush(stderr) == 0 && ferror(stderr) == 0;
  return success != 0 ? 0 : 238;
}

static int contract_child_arguments(int argc, char **argv) {
  FILE *stream = fopen("argv.marker", "wb");
  int index;
  int success;
  if (stream == (FILE *)0) {
    return 240;
  }
  for (index = 2; index < argc; index++) {
    size_t size = strlen(argv[index]);
    if (fprintf(stream, "%zu:", size) < 0 ||
        fwrite(argv[index], 1u, size, stream) != size ||
        fputc('\n', stream) == EOF) {
      (void)fclose(stream);
      return 241;
    }
  }
  success = fflush(stream) == 0 && ferror(stream) == 0;
  if (fclose(stream) != 0) {
    success = 0;
  }
  return success != 0 ? 0 : 242;
}

static int contract_child_marker(int argc) {
  static const char marker[] = "pinned\n";
  if (argc != 2) {
    return 243;
  }
  return contract_write_file("cwd.marker", marker, sizeof(marker) - 1u)
             ? 0
             : 244;
}

static int contract_child_linger(int argc) {
  if (argc != 2) {
    return 237;
  }
#if defined(_WIN32)
  return 237;
#else
  (void)sleep(10u);
  return 0;
#endif
}

int main(int argc, char **argv) {
  char self[CONTRACT_PATH_BYTES];
  char root[CONTRACT_PATH_BYTES];
  int success = 1;
  if (argc >= 2 && strcmp(argv[1], "--child-exit") == 0) {
    return contract_child_exit(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-stream") == 0) {
    return contract_child_stream(argc, argv, stdout);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-stderr-stream") == 0) {
    return contract_child_stream(argc, argv, stderr);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-stream-pair") == 0) {
    return contract_child_stream_pair(argc);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-argv") == 0) {
    return contract_child_arguments(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-marker") == 0) {
    return contract_child_marker(argc);
  }
  if (argc >= 2 && strcmp(argv[1], "--child-linger") == 0) {
    return contract_child_linger(argc);
  }
  if (argc != 2 ||
      (strcmp(argv[1], "all") != 0 &&
       strcmp(argv[1], "capture-no-forward") != 0 &&
       strcmp(argv[1], "stdout-4096-forward") != 0 &&
       strcmp(argv[1], "stream-pair-forward") != 0 &&
       strcmp(argv[1], "stream-prefix-forward") != 0 &&
       strcmp(argv[1], "native-posix-eintr") != 0)) {
    (void)fprintf(stderr,
                  "usage: cupidbuild-host-runner-contract "
                  "{all|capture-no-forward|stdout-4096-forward|"
                  "stream-pair-forward|stream-prefix-forward|"
                  "native-posix-eintr}\n");
    return 2;
  }
  if (!contract_self_path(argv[0], self, sizeof(self)) ||
      !contract_create_root(root, sizeof(root))) {
    (void)fprintf(stderr, "contract setup failed\n");
    return 1;
  }
  if (strcmp(argv[1], "capture-no-forward") == 0) {
    success = contract_test_capture_without_forwarding(root, self);
  } else if (strcmp(argv[1], "stdout-4096-forward") == 0) {
    success = contract_test_stdout_limit_forward(root, self);
  } else if (strcmp(argv[1], "stream-pair-forward") == 0) {
    success = contract_test_stream_pair_forward(root, self);
  } else if (strcmp(argv[1], "stream-prefix-forward") == 0) {
    success = contract_test_stream_prefix_forward(root, self);
  } else if (strcmp(argv[1], "native-posix-eintr") == 0) {
#if defined(CUPIDBUILD_HOST_NATIVE_EINTR_TEST) && !defined(_WIN32)
    success = contract_test_native_posix_eintr(root, self);
#else
    (void)fprintf(stderr,
                  "native POSIX EINTR injection is unavailable\n");
    success = 0;
#endif
  } else {
    if (!contract_test_exact_exits(root, self)) {
      success = 0;
    }
    if (!contract_test_non_executable(root)) {
      success = 0;
    }
    if (!contract_test_stream_limit(root, self)) {
      success = 0;
    }
    if (!contract_test_windows_command_limit(root, self)) {
      success = 0;
    }
    if (!contract_test_arguments(root, self)) {
      success = 0;
    }
    if (!contract_test_working_directory(root, self)) {
      success = 0;
    }
  }
  if (!contract_expect_no_private_roots(root, "final cleanup") ||
      !contract_remove_directory(root)) {
    (void)fprintf(stderr, "contract root cleanup failed\n");
    success = 0;
  }
  if (success == 0) {
    return 1;
  }
  if (strcmp(argv[1], "all") == 0) {
    (void)fputs("cupidbuild host runner contract: ok\n", stdout);
  } else if (strcmp(argv[1], "native-posix-eintr") == 0) {
    (void)fputs("cupidbuild host runner EINTR contract: ok\n", stdout);
  }
  return 0;
}
