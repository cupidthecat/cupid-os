#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
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
#if !defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#else
#define CUPIDBUILD_CUSTOM_LINUX 1
int cupid_linux_syscall0(int number);
int cupid_linux_syscall1(int number, unsigned int first);
int cupid_linux_syscall2(int number, unsigned int first,
                         unsigned int second);
int cupid_linux_syscall3(int number, unsigned int first,
                         unsigned int second, unsigned int third);
int cupid_linux_syscall4(int number, unsigned int first,
                         unsigned int second, unsigned int third,
                         unsigned int fourth);
#endif
#endif

#define CUPIDBUILD_HOST_PATH_BYTES 8192u
#define CUPIDBUILD_HOST_ERROR_BYTES 512u
#define CUPIDBUILD_HOST_INPUTS 16u
#define CUPIDBUILD_HOST_FILE_LIMIT 67108864u
#define CUPIDBUILD_HOST_PRIVATE_ATTEMPTS 4096u
#define CUPIDBUILD_HOST_DIRECTORY_BYTES 4096u

typedef struct {
  char live_path[CUPIDBUILD_HOST_PATH_BYTES];
  char frozen_path[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t snapshot;
} cupidbuild_host_input_t;

struct cupidbuild_host_transaction {
  char repository_root[CUPIDBUILD_HOST_PATH_BYTES];
  char source_path[CUPIDBUILD_HOST_PATH_BYTES];
  char frozen_source[CUPIDBUILD_HOST_PATH_BYTES];
  char output_path[CUPIDBUILD_HOST_PATH_BYTES];
  char output_parent[CUPIDBUILD_HOST_PATH_BYTES];
  char output_name[CUPIDBUILD_HOST_PATH_BYTES];
  char private_root[CUPIDBUILD_HOST_PATH_BYTES];
  char candidate[CUPIDBUILD_HOST_PATH_BYTES];
  char private_output[CUPIDBUILD_HOST_PATH_BYTES];
  char tool_stdout[CUPIDBUILD_HOST_PATH_BYTES];
  char tool_stderr[CUPIDBUILD_HOST_PATH_BYTES];
  char lock_path[CUPIDBUILD_HOST_PATH_BYTES];
  char error[CUPIDBUILD_HOST_ERROR_BYTES];
  cupidbuild_host_input_t inputs[CUPIDBUILD_HOST_INPUTS];
  unsigned int input_count;
  cupidbuild_host_snapshot_t output_parent_snapshot;
  cupidbuild_host_snapshot_t initial_output_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  cupidbuild_host_snapshot_t private_output_snapshot;
  cupidbuild_host_snapshot_t lock_snapshot;
  int candidate_captured;
  int private_output_captured;
  int lock_held;
  int private_created;
#if defined(_WIN32)
  HANDLE output_parent_handle;
#else
  int output_parent_descriptor;
  int private_descriptor;
#endif
};

typedef unsigned int cupidbuild_sha_word_t;

static const cupidbuild_sha_word_t cupidbuild_sha_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static cupidbuild_sha_word_t cupidbuild_sha_rotate(
    cupidbuild_sha_word_t value, unsigned int count) {
  return (value >> count) | (value << (32u - count));
}

static cupidbuild_sha_word_t cupidbuild_sha_load(
    const unsigned char *bytes) {
  return ((cupidbuild_sha_word_t)bytes[0] << 24u) |
         ((cupidbuild_sha_word_t)bytes[1] << 16u) |
         ((cupidbuild_sha_word_t)bytes[2] << 8u) |
         (cupidbuild_sha_word_t)bytes[3];
}

static void cupidbuild_sha_block(cupidbuild_sha_word_t state[8],
                                 const unsigned char block[64]) {
  cupidbuild_sha_word_t words[64];
  cupidbuild_sha_word_t first = state[0];
  cupidbuild_sha_word_t second = state[1];
  cupidbuild_sha_word_t third = state[2];
  cupidbuild_sha_word_t fourth = state[3];
  cupidbuild_sha_word_t fifth = state[4];
  cupidbuild_sha_word_t sixth = state[5];
  cupidbuild_sha_word_t seventh = state[6];
  cupidbuild_sha_word_t eighth = state[7];
  unsigned int index;
  for (index = 0u; index < 16u; index++) {
    words[index] = cupidbuild_sha_load(block + index * 4u);
  }
  for (index = 16u; index < 64u; index++) {
    cupidbuild_sha_word_t left =
        cupidbuild_sha_rotate(words[index - 15u], 7u) ^
        cupidbuild_sha_rotate(words[index - 15u], 18u) ^
        (words[index - 15u] >> 3u);
    cupidbuild_sha_word_t right =
        cupidbuild_sha_rotate(words[index - 2u], 17u) ^
        cupidbuild_sha_rotate(words[index - 2u], 19u) ^
        (words[index - 2u] >> 10u);
    words[index] = words[index - 16u] + left + words[index - 7u] + right;
  }
  for (index = 0u; index < 64u; index++) {
    cupidbuild_sha_word_t choose = (fifth & sixth) ^ ((~fifth) & seventh);
    cupidbuild_sha_word_t majority =
        (first & second) ^ (first & third) ^ (second & third);
    cupidbuild_sha_word_t high = cupidbuild_sha_rotate(first, 2u) ^
                                  cupidbuild_sha_rotate(first, 13u) ^
                                  cupidbuild_sha_rotate(first, 22u);
    cupidbuild_sha_word_t low = cupidbuild_sha_rotate(fifth, 6u) ^
                                 cupidbuild_sha_rotate(fifth, 11u) ^
                                 cupidbuild_sha_rotate(fifth, 25u);
    cupidbuild_sha_word_t temporary_first =
        eighth + low + choose + cupidbuild_sha_constants[index] + words[index];
    cupidbuild_sha_word_t temporary_second = high + majority;
    eighth = seventh;
    seventh = sixth;
    sixth = fifth;
    fifth = fourth + temporary_first;
    fourth = third;
    third = second;
    second = first;
    first = temporary_first + temporary_second;
  }
  state[0] += first;
  state[1] += second;
  state[2] += third;
  state[3] += fourth;
  state[4] += fifth;
  state[5] += sixth;
  state[6] += seventh;
  state[7] += eighth;
}

static void cupidbuild_sha256(const unsigned char *contents, size_t size,
                              unsigned char digest[32]) {
  cupidbuild_sha_word_t state[8] = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  unsigned char tail[128];
  size_t complete = size & ~(size_t)63u;
  size_t remaining = size - complete;
  unsigned long long bit_count = (unsigned long long)size * 8u;
  size_t index;
  for (index = 0u; index < complete; index += 64u) {
    cupidbuild_sha_block(state, contents + index);
  }
  for (index = 0u; index < remaining; index++) {
    tail[index] = contents[complete + index];
  }
  tail[remaining++] = 0x80u;
  while ((remaining & 63u) != 56u) {
    tail[remaining++] = 0u;
  }
  for (index = 0u; index < 8u; index++) {
    tail[remaining + index] =
        (unsigned char)(bit_count >> ((7u - index) * 8u));
  }
  remaining += 8u;
  cupidbuild_sha_block(state, tail);
  if (remaining == 128u) {
    cupidbuild_sha_block(state, tail + 64u);
  }
  for (index = 0u; index < 8u; index++) {
    digest[index * 4u] = (unsigned char)(state[index] >> 24u);
    digest[index * 4u + 1u] = (unsigned char)(state[index] >> 16u);
    digest[index * 4u + 2u] = (unsigned char)(state[index] >> 8u);
    digest[index * 4u + 3u] = (unsigned char)state[index];
  }
}

static int cupidbuild_host_copy_text(char *destination, size_t capacity,
                                     const char *source) {
  size_t size;
  if (source == (const char *)0) {
    return 0;
  }
  size = strlen(source);
  if (size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, source, size + 1u);
  return 1;
}

static int cupidbuild_host_join(char *destination, size_t capacity,
                                const char *left, const char *right) {
  size_t left_size = strlen(left);
  size_t right_size = strlen(right);
  int separator = left_size != 0u && left[left_size - 1u] != '/' &&
                          left[left_size - 1u] != '\\'
                      ? 1
                      : 0;
  if (left_size + (size_t)separator + right_size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, left, left_size);
  if (separator != 0) {
    destination[left_size++] = '/';
  }
  (void)memcpy(destination + left_size, right, right_size + 1u);
  return 1;
}

static void cupidbuild_host_set_error(cupidbuild_host_transaction_t *transaction,
                                      const char *message) {
  if (transaction != (cupidbuild_host_transaction_t *)0) {
    (void)cupidbuild_host_copy_text(transaction->error,
                                    sizeof(transaction->error), message);
  }
}

static int cupidbuild_host_snapshot_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right) {
  return left->present == right->present && left->size == right->size &&
         memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
         memcmp(left->identity, right->identity, sizeof(left->identity)) == 0 &&
         memcmp(left->modified, right->modified, sizeof(left->modified)) == 0;
}

static int cupidbuild_host_lock_snapshot_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right) {
  return left->present == right->present && left->size == right->size &&
         memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
         memcmp(left->identity, right->identity, sizeof(left->identity)) == 0;
}

static int cupidbuild_host_path_is_relative_safe(const char *path) {
  const char *cursor = path;
  if (path == (const char *)0 || path[0] == '\0' || path[0] == '/' ||
      path[0] == '\\' || path[1] == ':') {
    return 0;
  }
  while (*cursor != '\0') {
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      cursor++;
    }
    if ((cursor - start == 1 && start[0] == '.') ||
        (cursor - start == 2 && start[0] == '.' && start[1] == '.')) {
      return 0;
    }
    if (*cursor != '\0') {
      cursor++;
    }
  }
  return 1;
}

static int cupidbuild_host_parent(char *destination, size_t capacity,
                                  const char *path) {
  size_t size = strlen(path);
  while (size != 0u && path[size - 1u] != '/' && path[size - 1u] != '\\') {
    size--;
  }
  if (size <= 1u || size > capacity) {
    return 0;
  }
  size--;
  (void)memcpy(destination, path, size);
  destination[size] = '\0';
  return 1;
}

static int cupidbuild_host_basename(char *destination, size_t capacity,
                                    const char *path) {
  const char *cursor = path;
  const char *name = path;
  while (*cursor != '\0') {
    if (*cursor == '/' || *cursor == '\\') {
      name = cursor + 1;
    }
    cursor++;
  }
  return cupidbuild_host_copy_text(destination, capacity, name);
}

static char cupidbuild_host_ascii_fold(char value) {
  return value >= 'A' && value <= 'Z' ? (char)(value - 'A' + 'a') : value;
}

static int cupidbuild_host_name_has_suffix(const char *name,
                                           const char *suffix) {
  size_t name_size = strlen(name);
  size_t suffix_size = strlen(suffix);
  size_t index;
  if (suffix_size > name_size) {
    return 0;
  }
  for (index = 0u; index < suffix_size; index++) {
    if (cupidbuild_host_ascii_fold(name[name_size - suffix_size + index]) !=
        cupidbuild_host_ascii_fold(suffix[index])) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_host_name_is_expected(const char *name,
                                            const char *const *expected,
                                            size_t expected_count) {
  size_t index;
  for (index = 0u; index < expected_count; index++) {
    if (strcmp(name, expected[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

#if defined(_WIN32)
static int cupidbuild_host_path_has_link(const char *path) {
  char prefix[CUPIDBUILD_HOST_PATH_BYTES];
  size_t index;
  size_t size = strlen(path);
  if (size + 1u > sizeof(prefix)) {
    return 1;
  }
  (void)memcpy(prefix, path, size + 1u);
  for (index = 0u; index <= size; index++) {
    if ((prefix[index] == '/' || prefix[index] == '\\' ||
         prefix[index] == '\0') &&
        index > 2u) {
      DWORD attributes;
      char saved = prefix[index];
      prefix[index] = '\0';
      attributes = GetFileAttributesA(prefix);
      prefix[index] = saved;
      if (attributes == INVALID_FILE_ATTRIBUTES ||
          (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        return 1;
      }
    }
  }
  return 0;
}

static int cupidbuild_host_read_regular(
    const char *path, int optional, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  HANDLE handle;
  BY_HANDLE_FILE_INFORMATION information;
  unsigned char *bytes;
  size_t size;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (optional != 0 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
      (GetLastError() == ERROR_FILE_NOT_FOUND ||
       GetLastError() == ERROR_PATH_NOT_FOUND)) {
    snapshot->present = 0;
    if (bytes_out != (unsigned char **)0) {
      *bytes_out = (unsigned char *)0;
    }
    return 1;
  }
  if (cupidbuild_host_path_has_link(path)) {
    return 0;
  }
  handle = CreateFileA(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                       (HANDLE)0);
  if (handle == INVALID_HANDLE_VALUE) {
    if (optional != 0 && (GetLastError() == ERROR_FILE_NOT_FOUND ||
                          GetLastError() == ERROR_PATH_NOT_FOUND)) {
      snapshot->present = 0;
      if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
      }
      return 1;
    }
    return 0;
  }
  if (!GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
      information.nFileSizeHigh != 0u ||
      information.nFileSizeLow > CUPIDBUILD_HOST_FILE_LIMIT) {
    (void)CloseHandle(handle);
    return 0;
  }
  size = (size_t)information.nFileSizeLow;
  bytes = (unsigned char *)malloc(size + 1u);
  if (bytes == (unsigned char *)0) {
    (void)CloseHandle(handle);
    return 0;
  }
  while (offset < size) {
    DWORD read_bytes = 0u;
    DWORD chunk = (DWORD)(size - offset);
    if (!ReadFile(handle, bytes + offset, chunk, &read_bytes,
                  (LPOVERLAPPED)0) ||
        read_bytes == 0u) {
      free(bytes);
      (void)CloseHandle(handle);
      return 0;
    }
    offset += (size_t)read_bytes;
  }
  bytes[size] = 0u;
  snapshot->present = 1;
  snapshot->size = size;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  cupidbuild_sha256(bytes, size, snapshot->sha256);
  (void)CloseHandle(handle);
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_directory_snapshot(
    const char *path, cupidbuild_host_snapshot_t *snapshot) {
  HANDLE handle;
  BY_HANDLE_FILE_INFORMATION information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (cupidbuild_host_path_has_link(path)) {
    return 0;
  }
  handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                       (HANDLE)0);
  if (handle == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
    if (handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle);
    }
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  (void)CloseHandle(handle);
  return 1;
}

static int cupidbuild_host_seed_members_platform(
    const char *directory, const char *suffix, const char *const *expected,
    size_t expected_count) {
  char pattern[CUPIDBUILD_HOST_PATH_BYTES];
  WIN32_FIND_DATAA entry;
  HANDLE search;
  int valid = 1;
  if (!cupidbuild_host_join(pattern, sizeof(pattern), directory, "*")) {
    return 0;
  }
  search = FindFirstFileA(pattern, &entry);
  if (search == INVALID_HANDLE_VALUE) {
    return 0;
  }
  do {
    if (cupidbuild_host_name_has_suffix(entry.cFileName, suffix) &&
        !cupidbuild_host_name_is_expected(entry.cFileName, expected,
                                          expected_count)) {
      valid = 0;
      break;
    }
  } while (FindNextFileA(search, &entry));
  if (valid != 0 && GetLastError() != ERROR_NO_MORE_FILES) {
    valid = 0;
  }
  if (!FindClose(search)) {
    valid = 0;
  }
  return valid;
}

static int cupidbuild_host_write_exclusive(const char *path,
                                           const unsigned char *bytes,
                                           size_t size,
                                           cupidbuild_host_snapshot_t *snapshot) {
  HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0u,
                              (LPSECURITY_ATTRIBUTES)0, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
  BY_HANDLE_FILE_INFORMATION information;
  size_t offset = 0u;
  if (handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  while (offset < size) {
    DWORD written = 0u;
    DWORD chunk = (DWORD)(size - offset);
    if (!WriteFile(handle, bytes + offset, chunk, &written,
                   (LPOVERLAPPED)0) ||
        written == 0u) {
      (void)CloseHandle(handle);
      return 0;
    }
    offset += (size_t)written;
  }
  if (!FlushFileBuffers(handle) ||
      (snapshot != (cupidbuild_host_snapshot_t *)0 &&
       !GetFileInformationByHandle(handle, &information))) {
    (void)CloseHandle(handle);
    return 0;
  }
  if (snapshot != (cupidbuild_host_snapshot_t *)0) {
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    cupidbuild_sha256(bytes, size, snapshot->sha256);
  }
  if (!CloseHandle(handle)) {
    return 0;
  }
  return 1;
}

static int cupidbuild_host_make_directory(const char *path) {
  return CreateDirectoryA(path, (LPSECURITY_ATTRIBUTES)0) != 0;
}

static int cupidbuild_host_make_executable(const char *path) {
  return path != (const char *)0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)DeleteFileA(path);
}

static int cupidbuild_host_quarantine_file(const char *source,
                                           const char *destination) {
  return MoveFileExA(source, destination, MOVEFILE_WRITE_THROUGH) != 0u;
}

static int cupidbuild_host_commit_quarantined_file(
    const char *source, const char *quarantine,
    const cupidbuild_host_snapshot_t *expected) {
  (void)source;
  (void)quarantine;
  (void)expected;
  return 1;
}

static int cupidbuild_host_restore_quarantined_file(const char *source,
                                                    const char *destination) {
  return MoveFileExA(source, destination, MOVEFILE_WRITE_THROUGH) != 0u;
}

static void cupidbuild_host_remove_directory(const char *path) {
  (void)RemoveDirectoryA(path);
}

static unsigned int cupidbuild_host_process_id(void) {
  return GetCurrentProcessId();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0,
                               (DWORD)process_id);
  DWORD status = 0u;
  if (process == (HANDLE)0) {
    return 0;
  }
  if (!GetExitCodeProcess(process, &status)) {
    (void)CloseHandle(process);
    return 1;
  }
  (void)CloseHandle(process);
  return status == STILL_ACTIVE;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       unsigned int timeout_milliseconds) {
  STARTUPINFOA startup;
  PROCESS_INFORMATION process;
  SECURITY_ATTRIBUTES security;
  HANDLE standard_input = INVALID_HANDLE_VALUE;
  HANDLE standard_output = INVALID_HANDLE_VALUE;
  HANDLE standard_error = INVALID_HANDLE_VALUE;
  char command[CUPIDBUILD_HOST_PATH_BYTES * 3u];
  size_t used = 0u;
  unsigned int index;
  DWORD wait_status;
  DWORD exit_code = 125u;
  (void)memset(&startup, 0, sizeof(startup));
  (void)memset(&process, 0, sizeof(process));
  (void)memset(&security, 0, sizeof(security));
  security.nLength = (DWORD)sizeof(security);
  security.bInheritHandle = 1;
  startup.cb = (DWORD)sizeof(startup);
  standard_input = CreateFileA("NUL", GENERIC_READ, 0u, &security,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               (HANDLE)0);
  standard_output = CreateFileA(stdout_path, GENERIC_WRITE, 0u, &security,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                (HANDLE)0);
  standard_error = CreateFileA(stderr_path, GENERIC_WRITE, 0u, &security,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                               (HANDLE)0);
  if (standard_input == INVALID_HANDLE_VALUE ||
      standard_output == INVALID_HANDLE_VALUE ||
      standard_error == INVALID_HANDLE_VALUE) {
    if (standard_input != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_input);
    }
    if (standard_output != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_output);
    }
    if (standard_error != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_error);
    }
    return -1;
  }
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = standard_input;
  startup.hStdOutput = standard_output;
  startup.hStdError = standard_error;
  if (snprintf(command, sizeof(command), "\"%s\"", tool) < 0) {
    (void)CloseHandle(standard_input);
    (void)CloseHandle(standard_output);
    (void)CloseHandle(standard_error);
    return -1;
  }
  used = strlen(command);
  for (index = 0u; arguments[index] != (const char *)0; index++) {
    int written = snprintf(command + used, sizeof(command) - used,
                           " \"%s\"", arguments[index]);
    if (written < 0 || (size_t)written >= sizeof(command) - used) {
      (void)CloseHandle(standard_input);
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
      return -1;
    }
    used += (size_t)written;
  }
  if (!CreateProcessA(tool, command, (LPSECURITY_ATTRIBUTES)0,
                      (LPSECURITY_ATTRIBUTES)0, 1, 0u, (void *)0,
                      (const char *)0, &startup, &process)) {
    (void)CloseHandle(standard_input);
    (void)CloseHandle(standard_output);
    (void)CloseHandle(standard_error);
    return -1;
  }
  (void)CloseHandle(standard_input);
  (void)CloseHandle(standard_output);
  (void)CloseHandle(standard_error);
  wait_status = WaitForSingleObject(process.hProcess,
                                    (DWORD)timeout_milliseconds);
  if (wait_status == WAIT_TIMEOUT) {
    (void)TerminateProcess(process.hProcess, 124u);
    (void)WaitForSingleObject(process.hProcess, INFINITE);
    exit_code = 124u;
  } else if (wait_status != WAIT_OBJECT_0 ||
             !GetExitCodeProcess(process.hProcess, &exit_code)) {
    exit_code = 125u;
  }
  (void)CloseHandle(process.hThread);
  (void)CloseHandle(process.hProcess);
  return (int)exit_code;
}

typedef struct {
  unsigned int status;
#if defined(_WIN64)
  unsigned int status_padding;
  unsigned long long information;
#else
  unsigned int information;
#endif
} cupidbuild_windows_io_status_t;

typedef struct {
  unsigned char replace_if_exists;
#if defined(_WIN64)
  unsigned char padding[7];
#else
  unsigned char padding[3];
#endif
  HANDLE root_directory;
  DWORD file_name_length;
  unsigned short file_name[1];
} cupidbuild_windows_rename_t;

#if !defined(CUPID_HOSTED_I386_LINUX_ABI_H)
__declspec(dllimport) long __stdcall NtSetInformationFile(
    HANDLE file, cupidbuild_windows_io_status_t *status,
    void *information, unsigned long length,
    unsigned int information_class);
#define cupid_windows_nt_set_information_file NtSetInformationFile
#endif

static HANDLE cupidbuild_host_open_output_parent(const char *path) {
  return CreateFileA(
      path,
      FILE_READ_ATTRIBUTES | FILE_TRAVERSE | FILE_ADD_FILE |
          FILE_DELETE_CHILD | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  HANDLE candidate;
  BY_HANDLE_FILE_INFORMATION candidate_information;
  cupidbuild_windows_rename_t *rename_information;
  cupidbuild_windows_io_status_t status;
  size_t name_size = strlen(transaction->output_name);
  size_t allocation_size;
  size_t index;
  long result;
  candidate = CreateFileA(
      transaction->candidate,
      DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
  if (candidate == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(candidate, &candidate_information) ||
      candidate_information.dwVolumeSerialNumber !=
          transaction->candidate_snapshot.identity[0] ||
      candidate_information.nFileIndexHigh !=
          transaction->candidate_snapshot.identity[1] ||
      candidate_information.nFileIndexLow !=
          transaction->candidate_snapshot.identity[2] ||
      name_size > 4096u) {
    if (candidate != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(candidate);
    }
    return 0;
  }
  allocation_size = sizeof(*rename_information) - sizeof(unsigned short) +
                    name_size * sizeof(unsigned short);
  rename_information =
      (cupidbuild_windows_rename_t *)calloc(1u, allocation_size);
  if (rename_information == (cupidbuild_windows_rename_t *)0) {
    (void)CloseHandle(candidate);
    return 0;
  }
  rename_information->replace_if_exists = 1u;
  rename_information->root_directory = transaction->output_parent_handle;
  rename_information->file_name_length = (DWORD)(name_size * 2u);
  for (index = 0u; index < name_size; index++) {
    unsigned char character =
        (unsigned char)transaction->output_name[index];
    if (character >= 128u) {
      free(rename_information);
      (void)CloseHandle(candidate);
      return 0;
    }
    rename_information->file_name[index] = (unsigned short)character;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_set_information_file(
      candidate, &status, rename_information, (unsigned long)allocation_size,
      10u);
  free(rename_information);
  (void)CloseHandle(candidate);
  return result >= 0;
}
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
#define CUPIDBUILD_LINUX_SYS_EXIT 1
#define CUPIDBUILD_LINUX_SYS_FORK 2
#define CUPIDBUILD_LINUX_SYS_READ 3
#define CUPIDBUILD_LINUX_SYS_WRITE 4
#define CUPIDBUILD_LINUX_SYS_OPEN 5
#define CUPIDBUILD_LINUX_SYS_CLOSE 6
#define CUPIDBUILD_LINUX_SYS_WAITPID 7
#define CUPIDBUILD_LINUX_SYS_LINK 9
#define CUPIDBUILD_LINUX_SYS_DUP2 63
#define CUPIDBUILD_LINUX_SYS_UNLINK 10
#define CUPIDBUILD_LINUX_SYS_EXECVE 11
#define CUPIDBUILD_LINUX_SYS_CHMOD 15
#define CUPIDBUILD_LINUX_SYS_GETPID 20
#define CUPIDBUILD_LINUX_SYS_KILL 37
#define CUPIDBUILD_LINUX_SYS_RENAME 38
#define CUPIDBUILD_LINUX_SYS_MKDIR 39
#define CUPIDBUILD_LINUX_SYS_RMDIR 40
#define CUPIDBUILD_LINUX_SYS_FSYNC 118
#define CUPIDBUILD_LINUX_SYS_NANOSLEEP 162
#define CUPIDBUILD_LINUX_SYS_LSTAT64 196
#define CUPIDBUILD_LINUX_SYS_FSTAT64 197
#define CUPIDBUILD_LINUX_SYS_GETDENTS64 220
#define CUPIDBUILD_LINUX_SYS_RENAMEAT 302
#define CUPIDBUILD_LINUX_O_WRONLY 1u
#define CUPIDBUILD_LINUX_O_CREAT 64u
#define CUPIDBUILD_LINUX_O_EXCL 128u
#define CUPIDBUILD_LINUX_O_DIRECTORY 65536u
#define CUPIDBUILD_LINUX_O_NOFOLLOW 131072u
#define CUPIDBUILD_LINUX_WNOHANG 1u
#define CUPIDBUILD_LINUX_SIGKILL 9u
#define CUPIDBUILD_LINUX_ENOENT 2
#define CUPIDBUILD_LINUX_EPERM 1
#define CUPIDBUILD_LINUX_S_IFMT 0170000u
#define CUPIDBUILD_LINUX_S_IFDIR 0040000u
#define CUPIDBUILD_LINUX_S_IFREG 0100000u
#define CUPIDBUILD_LINUX_S_IFLNK 0120000u

typedef struct {
  int seconds;
  int nanoseconds;
} cupidbuild_linux_time_t;

static unsigned int cupidbuild_linux_u32(const unsigned char *bytes) {
  return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u) |
         ((unsigned int)bytes[2] << 16u) |
         ((unsigned int)bytes[3] << 24u);
}

static unsigned int cupidbuild_linux_u16(const unsigned char *bytes) {
  return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u);
}

static int cupidbuild_linux_stat(const char *path, unsigned char result[96]) {
  return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_LSTAT64,
                              (unsigned int)path,
                              (unsigned int)result);
}

static unsigned int cupidbuild_linux_mode(const unsigned char stat_bytes[96]) {
  return cupidbuild_linux_u32(stat_bytes + 16u);
}

static int cupidbuild_host_path_has_link(const char *path) {
  char prefix[CUPIDBUILD_HOST_PATH_BYTES];
  unsigned char information[96];
  size_t index;
  size_t size = strlen(path);
  if (size + 1u > sizeof(prefix)) {
    return 1;
  }
  (void)memcpy(prefix, path, size + 1u);
  for (index = 1u; index <= size; index++) {
    if (prefix[index] == '/' || prefix[index] == '\0') {
      char saved = prefix[index];
      prefix[index] = '\0';
      if (cupidbuild_linux_stat(prefix, information) < 0 ||
          (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) ==
              CUPIDBUILD_LINUX_S_IFLNK) {
        prefix[index] = saved;
        return 1;
      }
      prefix[index] = saved;
    }
  }
  return 0;
}

static int cupidbuild_host_read_regular(
    const char *path, int optional, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  unsigned char information[96];
  int descriptor;
  unsigned int size;
  unsigned char *bytes;
  unsigned int offset = 0u;
  int stat_result = cupidbuild_linux_stat(path, information);
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (optional != 0 && stat_result == -CUPIDBUILD_LINUX_ENOENT) {
    snapshot->present = 0;
    if (bytes_out != (unsigned char **)0) {
      *bytes_out = (unsigned char *)0;
    }
    return 1;
  }
  if (stat_result < 0 || cupidbuild_host_path_has_link(path)) {
    return 0;
  }
  descriptor = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
      CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
  if (descriptor < 0 ||
      cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFREG ||
      cupidbuild_linux_u32(information + 48u) != 0u) {
    if (descriptor >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)descriptor);
    }
    return 0;
  }
  size = cupidbuild_linux_u32(information + 44u);
  if (size > CUPIDBUILD_HOST_FILE_LIMIT) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
  bytes = (unsigned char *)malloc((size_t)size + 1u);
  if (bytes == (unsigned char *)0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
  while (offset < size) {
    int count = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_READ, (unsigned int)descriptor,
        (unsigned int)(bytes + offset), size - offset);
    if (count <= 0) {
      free(bytes);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)descriptor);
      return 0;
    }
    offset += (unsigned int)count;
  }
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)descriptor);
  bytes[size] = 0u;
  snapshot->present = 1;
  snapshot->size = size;
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  cupidbuild_sha256(bytes, size, snapshot->sha256);
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_directory_snapshot(
    const char *path, cupidbuild_host_snapshot_t *snapshot) {
  unsigned char information[96];
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (cupidbuild_host_path_has_link(path) ||
      cupidbuild_linux_stat(path, information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFDIR) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  return 1;
}

static int cupidbuild_host_seed_members_platform(
    const char *directory, const char *suffix, const char *const *expected,
    size_t expected_count) {
  unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
  int descriptor = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)directory,
      CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
  int valid = descriptor >= 0;
  while (valid != 0) {
    int count = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_GETDENTS64, (unsigned int)descriptor,
        (unsigned int)entries, (unsigned int)sizeof(entries));
    size_t offset = 0u;
    if (count == 0) {
      break;
    }
    if (count < 0) {
      valid = 0;
      break;
    }
    while (offset < (size_t)count) {
      unsigned int record_size;
      const char *name;
      size_t name_capacity;
      size_t name_size = 0u;
      if ((size_t)count - offset < 20u) {
        valid = 0;
        break;
      }
      record_size = cupidbuild_linux_u16(entries + offset + 16u);
      if (record_size < 20u || (size_t)record_size > (size_t)count - offset) {
        valid = 0;
        break;
      }
      name = (const char *)(entries + offset + 19u);
      name_capacity = (size_t)record_size - 19u;
      while (name_size < name_capacity && name[name_size] != '\0') {
        name_size++;
      }
      if (name_size == name_capacity) {
        valid = 0;
        break;
      }
      if (cupidbuild_host_name_has_suffix(name, suffix) &&
          !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
        valid = 0;
        break;
      }
      offset += (size_t)record_size;
    }
  }
  if (descriptor >= 0 &&
      cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                           (unsigned int)descriptor) < 0) {
    valid = 0;
  }
  return valid;
}

static int cupidbuild_host_write_exclusive(const char *path,
                                           const unsigned char *bytes,
                                           size_t size,
                                           cupidbuild_host_snapshot_t *snapshot) {
  int descriptor = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
      CUPIDBUILD_LINUX_O_WRONLY | CUPIDBUILD_LINUX_O_CREAT |
          CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW,
      0600u);
  unsigned char information[96];
  size_t offset = 0u;
  if (descriptor < 0) {
    return 0;
  }
  while (offset < size) {
    int count = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
        (unsigned int)(bytes + offset), (unsigned int)(size - offset));
    if (count <= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC,
                           (unsigned int)descriptor) < 0 ||
      (snapshot != (cupidbuild_host_snapshot_t *)0 &&
       cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                            (unsigned int)descriptor,
                            (unsigned int)information) < 0)) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
  if (snapshot != (cupidbuild_host_snapshot_t *)0) {
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    cupidbuild_sha256(bytes, size, snapshot->sha256);
  }
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                           (unsigned int)descriptor) < 0) {
    return 0;
  }
  return 1;
}

static int cupidbuild_host_make_directory(const char *path) {
  return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_MKDIR,
                              (unsigned int)path, 0700u) == 0;
}

static int cupidbuild_host_make_executable(const char *path) {
  return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_CHMOD,
                              (unsigned int)path, 0700u) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_UNLINK,
                             (unsigned int)path);
}

static int cupidbuild_host_quarantine_file(const char *source,
                                           const char *destination) {
  return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_LINK,
                              (unsigned int)source,
                              (unsigned int)destination) == 0;
}

static int cupidbuild_host_commit_quarantined_file(
    const char *source, const char *quarantine,
    const cupidbuild_host_snapshot_t *expected) {
  char committed[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t quarantine_snapshot;
  cupidbuild_host_snapshot_t committed_snapshot;
  cupidbuild_host_snapshot_t existing_snapshot;
  int written = snprintf(committed, sizeof(committed), "%s.commit",
                         quarantine);
  if (written <= 0 || (size_t)written >= sizeof(committed) ||
      !cupidbuild_host_read_regular(
          committed, 1, &existing_snapshot, (unsigned char **)0) ||
      existing_snapshot.present != 0 ||
      cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_RENAME,
                           (unsigned int)source,
                           (unsigned int)committed) != 0) {
    return 0;
  }
  if (cupidbuild_host_read_regular(
          quarantine, 0, &quarantine_snapshot, (unsigned char **)0) &&
      cupidbuild_host_read_regular(
          committed, 0, &committed_snapshot, (unsigned char **)0) &&
      cupidbuild_host_lock_snapshot_equal(&quarantine_snapshot, expected) &&
      cupidbuild_host_lock_snapshot_equal(&committed_snapshot,
                                           &quarantine_snapshot)) {
    cupidbuild_host_delete_file(committed);
    return 1;
  }
  if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_LINK,
                           (unsigned int)committed,
                           (unsigned int)source) == 0) {
    cupidbuild_host_delete_file(committed);
  }
  return 0;
}

static int cupidbuild_host_restore_quarantined_file(const char *source,
                                                    const char *destination) {
  (void)destination;
  return cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_UNLINK,
                              (unsigned int)source) == 0;
}

static void cupidbuild_host_remove_directory(const char *path) {
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_RMDIR,
                             (unsigned int)path);
}

static unsigned int cupidbuild_host_process_id(void) {
  return (unsigned int)cupid_linux_syscall0(CUPIDBUILD_LINUX_SYS_GETPID);
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
  int result = cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_KILL,
                                    process_id, 0u);
  return result == 0 || result == -CUPIDBUILD_LINUX_EPERM;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       unsigned int timeout_milliseconds) {
  char *argv[32];
  unsigned int count = 0u;
  int child;
  int stdout_descriptor;
  int stderr_descriptor;
  unsigned int elapsed = 0u;
  int status = 0;
  cupidbuild_linux_time_t pause_time;
  argv[count++] = (char *)tool;
  while (arguments[count - 1u] != (const char *)0 && count < 31u) {
    argv[count] = (char *)arguments[count - 1u];
    count++;
  }
  if (count == 31u && arguments[count - 1u] != (const char *)0) {
    return -1;
  }
  argv[count] = (char *)0;
  stdout_descriptor = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)stdout_path,
      CUPIDBUILD_LINUX_O_WRONLY | CUPIDBUILD_LINUX_O_CREAT |
          CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW,
      0600u);
  stderr_descriptor = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)stderr_path,
      CUPIDBUILD_LINUX_O_WRONLY | CUPIDBUILD_LINUX_O_CREAT |
          CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW,
      0600u);
  if (stdout_descriptor < 0 || stderr_descriptor < 0) {
    if (stdout_descriptor >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stdout_descriptor);
    }
    if (stderr_descriptor >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stderr_descriptor);
    }
    return -1;
  }
  child = cupid_linux_syscall0(CUPIDBUILD_LINUX_SYS_FORK);
  if (child == 0) {
    (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_DUP2,
                               (unsigned int)stdout_descriptor, 1u);
    (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_DUP2,
                               (unsigned int)stderr_descriptor, 2u);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)stdout_descriptor);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)stderr_descriptor);
    (void)cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_EXECVE,
                               (unsigned int)tool, (unsigned int)argv, 0u);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
    return 125;
  }
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)stdout_descriptor);
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)stderr_descriptor);
  if (child < 0) {
    return -1;
  }
  pause_time.seconds = 0;
  pause_time.nanoseconds = 10000000;
  while (cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WAITPID,
                              (unsigned int)child, (unsigned int)&status,
                              CUPIDBUILD_LINUX_WNOHANG) == 0) {
    if (elapsed >= timeout_milliseconds) {
      (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_KILL,
                                 (unsigned int)child,
                                 CUPIDBUILD_LINUX_SIGKILL);
      (void)cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WAITPID,
                                 (unsigned int)child, (unsigned int)&status,
                                 0u);
      return 124;
    }
    (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_NANOSLEEP,
                               (unsigned int)&pause_time, 0u);
    elapsed += 10u;
  }
  if ((status & 0x7fu) != 0) {
    return 125;
  }
  return (status >> 8) & 0xff;
}

static int cupidbuild_host_open_directory(const char *path) {
  return cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
      CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
}

static void cupidbuild_host_close_directory(int descriptor) {
  if (descriptor >= 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
  }
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  return cupid_linux_syscall4(
             CUPIDBUILD_LINUX_SYS_RENAMEAT,
             (unsigned int)transaction->private_descriptor,
             (unsigned int)"candidate.o",
             (unsigned int)transaction->output_parent_descriptor,
             (unsigned int)transaction->output_name) == 0;
}
#else
static int cupidbuild_host_path_has_link(const char *path) {
  char prefix[CUPIDBUILD_HOST_PATH_BYTES];
  size_t index;
  size_t size = strlen(path);
  if (size + 1u > sizeof(prefix)) {
    return 1;
  }
  (void)memcpy(prefix, path, size + 1u);
  for (index = 1u; index <= size; index++) {
    if (prefix[index] == '/' || prefix[index] == '\0') {
      struct stat information;
      char saved = prefix[index];
      prefix[index] = '\0';
      if (lstat(prefix, &information) != 0 || S_ISLNK(information.st_mode)) {
        prefix[index] = saved;
        return 1;
      }
      prefix[index] = saved;
    }
  }
  return 0;
}

static int cupidbuild_host_read_regular(
    const char *path, int optional, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  int descriptor;
  struct stat information;
  unsigned char *bytes;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (optional != 0 && lstat(path, &information) != 0 && errno == ENOENT) {
    snapshot->present = 0;
    if (bytes_out != (unsigned char **)0) {
      *bytes_out = (unsigned char *)0;
    }
    return 1;
  }
  if (cupidbuild_host_path_has_link(path)) {
    return 0;
  }
  descriptor = open(path, O_RDONLY | O_NOFOLLOW);
  if (descriptor < 0) {
    if (optional != 0 && errno == ENOENT) {
      snapshot->present = 0;
      if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
      }
      return 1;
    }
    return 0;
  }
  if (fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0 ||
      (unsigned long long)information.st_size > CUPIDBUILD_HOST_FILE_LIMIT) {
    (void)close(descriptor);
    return 0;
  }
  bytes = (unsigned char *)malloc((size_t)information.st_size + 1u);
  if (bytes == (unsigned char *)0) {
    (void)close(descriptor);
    return 0;
  }
  while (offset < (size_t)information.st_size) {
    ssize_t count = read(descriptor, bytes + offset,
                         (size_t)information.st_size - offset);
    if (count <= 0) {
      free(bytes);
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
  bytes[offset] = 0u;
  snapshot->present = 1;
  snapshot->size = offset;
  snapshot->identity[0] =
      (unsigned int)(unsigned long long)information.st_dev;
  snapshot->identity[1] =
      (unsigned int)((unsigned long long)information.st_dev >> 32u);
  snapshot->identity[2] =
      (unsigned int)(unsigned long long)information.st_ino;
  snapshot->identity[3] =
      (unsigned int)((unsigned long long)information.st_ino >> 32u);
  snapshot->modified[0] = (unsigned int)information.st_mtime;
  cupidbuild_sha256(bytes, offset, snapshot->sha256);
  (void)close(descriptor);
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_directory_snapshot(
    const char *path, cupidbuild_host_snapshot_t *snapshot) {
  struct stat information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (cupidbuild_host_path_has_link(path) ||
      stat(path, &information) != 0 || !S_ISDIR(information.st_mode)) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] =
      (unsigned int)(unsigned long long)information.st_dev;
  snapshot->identity[1] =
      (unsigned int)((unsigned long long)information.st_dev >> 32u);
  snapshot->identity[2] =
      (unsigned int)(unsigned long long)information.st_ino;
  snapshot->identity[3] =
      (unsigned int)((unsigned long long)information.st_ino >> 32u);
  return 1;
}

static int cupidbuild_host_seed_members_platform(
    const char *directory, const char *suffix, const char *const *expected,
    size_t expected_count) {
  DIR *stream = opendir(directory);
  struct dirent *entry;
  int valid = stream != (DIR *)0;
  if (stream == (DIR *)0) {
    return 0;
  }
  errno = 0;
  while ((entry = readdir(stream)) != (struct dirent *)0) {
    if (cupidbuild_host_name_has_suffix(entry->d_name, suffix) &&
        !cupidbuild_host_name_is_expected(entry->d_name, expected,
                                          expected_count)) {
      valid = 0;
      break;
    }
    errno = 0;
  }
  if (entry == (struct dirent *)0 && errno != 0) {
    valid = 0;
  }
  if (closedir(stream) != 0) {
    valid = 0;
  }
  return valid;
}

static int cupidbuild_host_write_exclusive(const char *path,
                                           const unsigned char *bytes,
                                           size_t size,
                                           cupidbuild_host_snapshot_t *snapshot) {
  int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  struct stat information;
  size_t offset = 0u;
  if (descriptor < 0) {
    return 0;
  }
  while (offset < size) {
    ssize_t count = write(descriptor, bytes + offset, size - offset);
    if (count <= 0) {
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
  if (fsync(descriptor) != 0 ||
      (snapshot != (cupidbuild_host_snapshot_t *)0 &&
       fstat(descriptor, &information) != 0)) {
    (void)close(descriptor);
    return 0;
  }
  if (snapshot != (cupidbuild_host_snapshot_t *)0) {
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] =
        (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] =
        (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] =
        (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] =
        (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    cupidbuild_sha256(bytes, size, snapshot->sha256);
  }
  if (close(descriptor) != 0) {
    return 0;
  }
  return 1;
}

static int cupidbuild_host_make_directory(const char *path) {
  return mkdir(path, 0700) == 0;
}

static int cupidbuild_host_make_executable(const char *path) {
  return chmod(path, 0700) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)unlink(path);
}

static int cupidbuild_host_quarantine_file(const char *source,
                                           const char *destination) {
  return link(source, destination) == 0;
}

static int cupidbuild_host_commit_quarantined_file(
    const char *source, const char *quarantine,
    const cupidbuild_host_snapshot_t *expected) {
  char committed[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t quarantine_snapshot;
  cupidbuild_host_snapshot_t committed_snapshot;
  cupidbuild_host_snapshot_t existing_snapshot;
  int written = snprintf(committed, sizeof(committed), "%s.commit",
                         quarantine);
  if (written <= 0 || (size_t)written >= sizeof(committed) ||
      !cupidbuild_host_read_regular(
          committed, 1, &existing_snapshot, (unsigned char **)0) ||
      existing_snapshot.present != 0 ||
      rename(source, committed) != 0) {
    return 0;
  }
  if (cupidbuild_host_read_regular(
          quarantine, 0, &quarantine_snapshot, (unsigned char **)0) &&
      cupidbuild_host_read_regular(
          committed, 0, &committed_snapshot, (unsigned char **)0) &&
      cupidbuild_host_lock_snapshot_equal(&quarantine_snapshot, expected) &&
      cupidbuild_host_lock_snapshot_equal(&committed_snapshot,
                                           &quarantine_snapshot)) {
    cupidbuild_host_delete_file(committed);
    return 1;
  }
  if (link(committed, source) == 0) {
    cupidbuild_host_delete_file(committed);
  }
  return 0;
}

static int cupidbuild_host_restore_quarantined_file(const char *source,
                                                    const char *destination) {
  (void)destination;
  return unlink(source) == 0;
}

static void cupidbuild_host_remove_directory(const char *path) {
  (void)rmdir(path);
}

static unsigned int cupidbuild_host_process_id(void) {
  return (unsigned int)getpid();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
  return kill((pid_t)process_id, 0) == 0 || errno == EPERM;
}

static char *cupidbuild_host_exec_argument(const char *argument) {
  char *mutable_argument;
  (void)memcpy(&mutable_argument, &argument, sizeof(mutable_argument));
  return mutable_argument;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       unsigned int timeout_milliseconds) {
  char *argv[32];
  unsigned int count = 0u;
  pid_t child;
  int stdout_descriptor;
  int stderr_descriptor;
  unsigned int elapsed = 0u;
  int status = 0;
  struct timespec pause_time;
  argv[count++] = cupidbuild_host_exec_argument(tool);
  while (arguments[count - 1u] != (const char *)0 && count < 31u) {
    argv[count] = cupidbuild_host_exec_argument(arguments[count - 1u]);
    count++;
  }
  if (count == 31u && arguments[count - 1u] != (const char *)0) {
    return -1;
  }
  argv[count] = (char *)0;
  stdout_descriptor = open(stdout_path,
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  stderr_descriptor = open(stderr_path,
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (stdout_descriptor < 0 || stderr_descriptor < 0) {
    if (stdout_descriptor >= 0) {
      (void)close(stdout_descriptor);
    }
    if (stderr_descriptor >= 0) {
      (void)close(stderr_descriptor);
    }
    return -1;
  }
  child = fork();
  if (child == 0) {
    (void)dup2(stdout_descriptor, STDOUT_FILENO);
    (void)dup2(stderr_descriptor, STDERR_FILENO);
    (void)close(stdout_descriptor);
    (void)close(stderr_descriptor);
    execv(tool, argv);
    _exit(125);
  }
  (void)close(stdout_descriptor);
  (void)close(stderr_descriptor);
  if (child < 0) {
    return -1;
  }
  pause_time.tv_sec = 0;
  pause_time.tv_nsec = 10000000L;
  while (waitpid(child, &status, WNOHANG) == 0) {
    if (elapsed >= timeout_milliseconds) {
      (void)kill(child, SIGKILL);
      (void)waitpid(child, &status, 0);
      return 124;
    }
    (void)nanosleep(&pause_time, (struct timespec *)0);
    elapsed += 10u;
  }
  if (!WIFEXITED(status)) {
    return 125;
  }
  return WEXITSTATUS(status);
}

static int cupidbuild_host_open_directory(const char *path) {
  return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

static void cupidbuild_host_close_directory(int descriptor) {
  if (descriptor >= 0) {
    (void)close(descriptor);
  }
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  return renameat(transaction->private_descriptor, "candidate.o",
                  transaction->output_parent_descriptor,
                  transaction->output_name) == 0;
}
#endif
#endif

int cupidbuild_host_seed_members_exact(const char *directory,
                                       const char *suffix,
                                       const char *const *expected,
                                       size_t expected_count) {
  if (directory == (const char *)0 || directory[0] == '\0' ||
      suffix == (const char *)0 || suffix[0] == '\0' ||
      expected == (const char *const *)0 || expected_count == 0u) {
    return 0;
  }
  return cupidbuild_host_seed_members_platform(directory, suffix, expected,
                                                expected_count);
}

static int cupidbuild_host_read_owner(const char *path,
                                      unsigned int *owner_out,
                                      cupidbuild_host_snapshot_t *snapshot_out) {
  cupidbuild_host_snapshot_t snapshot;
  unsigned char *bytes = (unsigned char *)0;
  unsigned int owner = 0u;
  size_t index;
  if (!cupidbuild_host_read_regular(path, 0, &snapshot, &bytes) ||
      snapshot.size < 2u || snapshot.size > 16u ||
      bytes[snapshot.size - 1u] != '\n') {
    free(bytes);
    return 0;
  }
  for (index = 0u; index + 1u < snapshot.size; index++) {
    unsigned int digit;
    if (bytes[index] < '0' || bytes[index] > '9') {
      free(bytes);
      return 0;
    }
    digit = (unsigned int)(bytes[index] - '0');
    if (owner > (4294967295u - digit) / 10u) {
      free(bytes);
      return 0;
    }
    owner = owner * 10u + digit;
  }
  free(bytes);
  if (owner == 0u) {
    return 0;
  }
  *owner_out = owner;
  if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
    *snapshot_out = snapshot;
  }
  return 1;
}

static int cupidbuild_host_acquire_lock(
    cupidbuild_host_transaction_t *transaction) {
  char owner_text[32];
  char recovery_path[CUPIDBUILD_HOST_PATH_BYTES];
  unsigned int attempt;
  unsigned int process_id = cupidbuild_host_process_id();
  int owner_written = snprintf(owner_text, sizeof(owner_text), "%u\n",
                               process_id);
  if (owner_written <= 0 ||
      (size_t)owner_written >= sizeof(owner_text)) {
    return 0;
  }
  for (attempt = 0u; attempt < 4u; attempt++) {
    if (cupidbuild_host_write_exclusive(
            transaction->lock_path, (const unsigned char *)owner_text,
            (size_t)owner_written, &transaction->lock_snapshot)) {
      transaction->lock_held = 1;
      return 1;
    }
    {
      unsigned int owner = 0u;
      cupidbuild_host_snapshot_t stale_snapshot;
      cupidbuild_host_snapshot_t quarantined_snapshot;
      if (!cupidbuild_host_read_owner(transaction->lock_path, &owner,
                                      &stale_snapshot)) {
        cupidbuild_host_set_error(transaction,
                                  "publication lock is not a regular owner file");
        return 0;
      }
      if (cupidbuild_host_process_alive(owner)) {
        cupidbuild_host_set_error(transaction,
                                  "publication lock is held by a live process");
        return 0;
      }
      int recovery_written = snprintf(
          recovery_path, sizeof(recovery_path), "%s.reclaim-%08x",
          transaction->lock_path, owner);
      if (recovery_written <= 0 ||
          (size_t)recovery_written >= sizeof(recovery_path) ||
          !cupidbuild_host_quarantine_file(transaction->lock_path,
                                            recovery_path)) {
        cupidbuild_host_set_error(
            transaction, "publication lock could not enter stale recovery");
        return 0;
      }
      if (!cupidbuild_host_read_regular(
              recovery_path, 0, &quarantined_snapshot,
              (unsigned char **)0) ||
          !cupidbuild_host_lock_snapshot_equal(&quarantined_snapshot,
                                                &stale_snapshot) ||
          !cupidbuild_host_commit_quarantined_file(
              transaction->lock_path, recovery_path, &stale_snapshot)) {
        (void)cupidbuild_host_restore_quarantined_file(
            recovery_path, transaction->lock_path);
        cupidbuild_host_set_error(
            transaction, "publication lock changed during stale recovery");
        return 0;
      }
      cupidbuild_host_delete_file(recovery_path);
    }
  }
  cupidbuild_host_set_error(transaction,
                            "stale publication lock could not be reclaimed");
  return 0;
}

static int cupidbuild_host_lock_is_unchanged(
    const cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t current;
  return transaction != (const cupidbuild_host_transaction_t *)0 &&
         transaction->lock_held != 0 &&
         cupidbuild_host_read_regular(transaction->lock_path, 0, &current,
                                      (unsigned char **)0) &&
         cupidbuild_host_lock_snapshot_equal(&current,
                                              &transaction->lock_snapshot);
}

static int cupidbuild_host_require_lock(
    cupidbuild_host_transaction_t *transaction) {
  if (!cupidbuild_host_lock_is_unchanged(transaction)) {
    cupidbuild_host_set_error(
        transaction, "publication lock changed while checked tools ran");
    return 0;
  }
  return 1;
}

static void cupidbuild_host_release_lock(
    cupidbuild_host_transaction_t *transaction) {
  char quarantine_path[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t quarantined_snapshot;
  int written;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->lock_held == 0) {
    return;
  }
  if (transaction->private_created != 0) {
    if (!cupidbuild_host_join(quarantine_path, sizeof(quarantine_path),
                              transaction->private_root, "owner.lock")) {
      return;
    }
  } else {
    written = snprintf(quarantine_path, sizeof(quarantine_path),
                       "%s.release-%08x", transaction->lock_path,
                       cupidbuild_host_process_id());
    if (written <= 0 || (size_t)written >= sizeof(quarantine_path)) {
      return;
    }
  }
  if (!cupidbuild_host_quarantine_file(transaction->lock_path,
                                        quarantine_path)) {
    return;
  }
  if (cupidbuild_host_read_regular(
          quarantine_path, 0, &quarantined_snapshot,
          (unsigned char **)0) &&
      cupidbuild_host_lock_snapshot_equal(&quarantined_snapshot,
                                           &transaction->lock_snapshot) &&
      cupidbuild_host_commit_quarantined_file(
          transaction->lock_path, quarantine_path,
          &transaction->lock_snapshot)) {
    cupidbuild_host_delete_file(quarantine_path);
  } else {
    (void)cupidbuild_host_restore_quarantined_file(
        quarantine_path, transaction->lock_path);
  }
  transaction->lock_held = 0;
}

static int cupidbuild_host_register_input(
    cupidbuild_host_transaction_t *transaction, const char *live_path,
    const char *frozen_path, const cupidbuild_host_snapshot_t *snapshot) {
  cupidbuild_host_input_t *input;
  if (transaction->input_count >= CUPIDBUILD_HOST_INPUTS) {
    cupidbuild_host_set_error(transaction, "too many frozen transaction inputs");
    return 0;
  }
  input = &transaction->inputs[transaction->input_count];
  if (!cupidbuild_host_copy_text(input->live_path, sizeof(input->live_path),
                                 live_path) ||
      !cupidbuild_host_copy_text(input->frozen_path,
                                 sizeof(input->frozen_path), frozen_path)) {
    cupidbuild_host_set_error(transaction, "transaction input path is too long");
    return 0;
  }
  input->snapshot = *snapshot;
  transaction->input_count++;
  return 1;
}

int cupidbuild_host_freeze_input(cupidbuild_host_transaction_t *transaction,
                                 const char *live_path,
                                 const char *private_name,
                                 const char **frozen_path_out,
                                 cupidbuild_host_snapshot_t *snapshot_out) {
  char frozen_path[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t snapshot;
  unsigned char *bytes = (unsigned char *)0;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      private_name == (const char *)0 || strchr(private_name, '/') != 0 ||
      strchr(private_name, '\\') != 0 ||
      !cupidbuild_host_join(frozen_path, sizeof(frozen_path),
                            transaction->private_root, private_name) ||
      !cupidbuild_host_read_regular(live_path, 0, &snapshot, &bytes)) {
    cupidbuild_host_set_error(transaction,
                              "transaction input cannot be pinned and read");
    free(bytes);
    return 0;
  }
  if (!cupidbuild_host_write_exclusive(frozen_path, bytes, snapshot.size,
                                       (cupidbuild_host_snapshot_t *)0)) {
    cupidbuild_host_set_error(transaction,
                              "private frozen input cannot be created");
    free(bytes);
    return 0;
  }
  free(bytes);
  if (!cupidbuild_host_register_input(transaction, live_path, frozen_path,
                                      &snapshot)) {
    cupidbuild_host_delete_file(frozen_path);
    return 0;
  }
  if (transaction->initial_output_snapshot.present != 0 &&
      memcmp(transaction->initial_output_snapshot.identity, snapshot.identity,
             sizeof(snapshot.identity)) == 0) {
    cupidbuild_host_set_error(transaction,
                              "code output may not replace an input");
    cupidbuild_host_delete_file(frozen_path);
    transaction->input_count--;
    return 0;
  }
  if (frozen_path_out != (const char **)0) {
    *frozen_path_out =
        transaction->inputs[transaction->input_count - 1u].frozen_path;
  }
  if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
    *snapshot_out = snapshot;
  }
  return 1;
}

int cupidbuild_host_make_input_executable(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path) {
  unsigned int index;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      frozen_path == (const char *)0) {
    return 0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if (strcmp(transaction->inputs[index].frozen_path, frozen_path) == 0) {
      if (cupidbuild_host_make_executable(frozen_path)) {
        return 1;
      }
      cupidbuild_host_set_error(
          transaction, "private checked tool cannot be made executable");
      return 0;
    }
  }
  cupidbuild_host_set_error(transaction,
                            "executable is not a frozen transaction input");
  return 0;
}

int cupidbuild_host_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical,
    cupidbuild_host_transaction_t **transaction_out) {
  cupidbuild_host_transaction_t *transaction;
  unsigned int attempt;
  const char *frozen = (const char *)0;
  if (transaction_out == (cupidbuild_host_transaction_t **)0) {
    return 0;
  }
  *transaction_out = (cupidbuild_host_transaction_t *)0;
  transaction = (cupidbuild_host_transaction_t *)calloc(1u, sizeof(*transaction));
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
#if !defined(_WIN32)
  transaction->output_parent_descriptor = -1;
  transaction->private_descriptor = -1;
#endif
  if (!cupidbuild_host_path_is_relative_safe(source_logical) ||
      !cupidbuild_host_path_is_relative_safe(output_logical) ||
      !cupidbuild_host_copy_text(transaction->repository_root,
                                 sizeof(transaction->repository_root),
                                 repository_root) ||
      !cupidbuild_host_join(transaction->source_path,
                            sizeof(transaction->source_path), repository_root,
                            source_logical) ||
      !cupidbuild_host_join(transaction->output_path,
                            sizeof(transaction->output_path), repository_root,
                            output_logical) ||
      !cupidbuild_host_parent(transaction->output_parent,
                              sizeof(transaction->output_parent),
                              transaction->output_path) ||
      !cupidbuild_host_basename(transaction->output_name,
                                sizeof(transaction->output_name),
                                transaction->output_path) ||
      !cupidbuild_host_directory_snapshot(
          transaction->output_parent, &transaction->output_parent_snapshot) ||
      snprintf(transaction->lock_path, sizeof(transaction->lock_path),
               "%s.cupidbuild.lock", transaction->output_path) < 0) {
    cupidbuild_host_set_error(transaction, "guarded object paths are invalid");
    *transaction_out = transaction;
    return 0;
  }
#if defined(_WIN32)
  transaction->output_parent_handle =
      cupidbuild_host_open_output_parent(transaction->output_parent);
  if (transaction->output_parent_handle == INVALID_HANDLE_VALUE) {
    cupidbuild_host_set_error(transaction, "code output parent cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
#else
  transaction->output_parent_descriptor =
      cupidbuild_host_open_directory(transaction->output_parent);
  if (transaction->output_parent_descriptor < 0) {
    cupidbuild_host_set_error(transaction, "code output parent cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
#endif
  if (!cupidbuild_host_acquire_lock(transaction)) {
    *transaction_out = transaction;
    return 0;
  }
  for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
    int written = snprintf(transaction->private_root,
                           sizeof(transaction->private_root),
                           "%s/.cupidbuild-object-%08x", repository_root,
                           attempt);
    if (written < 0 || (size_t)written >= sizeof(transaction->private_root)) {
      break;
    }
    if (cupidbuild_host_make_directory(transaction->private_root)) {
      transaction->private_created = 1;
      break;
    }
  }
#if !defined(_WIN32)
  if (transaction->private_created != 0) {
    transaction->private_descriptor =
        cupidbuild_host_open_directory(transaction->private_root);
  }
#endif
  if (transaction->private_created == 0 ||
#if !defined(_WIN32)
      transaction->private_descriptor < 0 ||
#endif
      !cupidbuild_host_join(transaction->candidate,
                            sizeof(transaction->candidate),
                            transaction->private_root, "candidate.o") ||
      !cupidbuild_host_join(transaction->private_output,
                            sizeof(transaction->private_output),
                            transaction->private_root, "candidate.map") ||
      !cupidbuild_host_join(transaction->tool_stdout,
                            sizeof(transaction->tool_stdout),
                            transaction->private_root, "tool.stdout") ||
      !cupidbuild_host_join(transaction->tool_stderr,
                            sizeof(transaction->tool_stderr),
                            transaction->private_root, "tool.stderr") ||
      !cupidbuild_host_freeze_input(transaction, transaction->source_path,
                                    "source.asm", &frozen,
                                    (cupidbuild_host_snapshot_t *)0) ||
      !cupidbuild_host_copy_text(transaction->frozen_source,
                                 sizeof(transaction->frozen_source), frozen) ||
      !cupidbuild_host_read_regular(transaction->output_path, 1,
                                    &transaction->initial_output_snapshot,
                                    (unsigned char **)0)) {
    if (transaction->error[0] == '\0') {
      cupidbuild_host_set_error(transaction,
                                "private object transaction cannot be opened");
    }
    *transaction_out = transaction;
    return 0;
  }
  if (transaction->initial_output_snapshot.present != 0 &&
      memcmp(transaction->initial_output_snapshot.identity,
             transaction->inputs[0].snapshot.identity,
             sizeof(transaction->initial_output_snapshot.identity)) == 0) {
    cupidbuild_host_set_error(transaction,
                              "code output may not replace an input");
    *transaction_out = transaction;
    return 0;
  }
  *transaction_out = transaction;
  return 1;
}

void cupidbuild_host_transaction_close(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return;
  }
  cupidbuild_host_delete_file(transaction->candidate);
  cupidbuild_host_delete_file(transaction->private_output);
  cupidbuild_host_delete_file(transaction->tool_stdout);
  cupidbuild_host_delete_file(transaction->tool_stderr);
  for (index = 0u; index < transaction->input_count; index++) {
    cupidbuild_host_delete_file(transaction->inputs[index].frozen_path);
  }
  cupidbuild_host_release_lock(transaction);
#if defined(_WIN32)
  if (transaction->output_parent_handle != (HANDLE)0 &&
      transaction->output_parent_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->output_parent_handle);
  }
#else
  cupidbuild_host_close_directory(transaction->private_descriptor);
  cupidbuild_host_close_directory(transaction->output_parent_descriptor);
#endif
  if (transaction->private_created != 0) {
    cupidbuild_host_remove_directory(transaction->private_root);
  }
  free(transaction);
}

const char *cupidbuild_host_frozen_source(
    const cupidbuild_host_transaction_t *transaction) {
  return transaction == (const cupidbuild_host_transaction_t *)0
             ? (const char *)0
             : transaction->frozen_source;
}

const char *cupidbuild_host_candidate(
    const cupidbuild_host_transaction_t *transaction) {
  return transaction == (const cupidbuild_host_transaction_t *)0
             ? (const char *)0
             : transaction->candidate;
}

const char *cupidbuild_host_private_output(
    const cupidbuild_host_transaction_t *transaction) {
  return transaction == (const cupidbuild_host_transaction_t *)0
             ? (const char *)0
             : transaction->private_output;
}

int cupidbuild_host_run(cupidbuild_host_transaction_t *transaction,
                        const char *tool, const char *const *arguments,
                        unsigned int timeout_milliseconds) {
  cupidbuild_host_snapshot_t stdout_snapshot;
  cupidbuild_host_snapshot_t stderr_snapshot;
  unsigned char *stdout_bytes = (unsigned char *)0;
  unsigned char *stderr_bytes = (unsigned char *)0;
  int result;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      tool == (const char *)0 || arguments == (const char *const *)0) {
    return -1;
  }
  cupidbuild_host_delete_file(transaction->tool_stdout);
  cupidbuild_host_delete_file(transaction->tool_stderr);
  result = cupidbuild_host_run_process(
      tool, arguments, transaction->tool_stdout, transaction->tool_stderr,
      timeout_milliseconds);
  if (result >= 0 &&
      (!cupidbuild_host_read_regular(transaction->tool_stdout, 0,
                                     &stdout_snapshot, &stdout_bytes) ||
       !cupidbuild_host_read_regular(transaction->tool_stderr, 0,
                                     &stderr_snapshot, &stderr_bytes))) {
    result = -1;
  }
  if (result != 0 && stderr_bytes != (unsigned char *)0 &&
      stderr_snapshot.size != 0u) {
    (void)fwrite(stderr_bytes, 1u, stderr_snapshot.size, stderr);
  }
  if (result == 0 && stdout_snapshot.size != 0u) {
    cupidbuild_host_set_error(transaction,
                              "checked tool wrote unexpected standard output");
    result = 125;
  } else if (result == 0 && stderr_snapshot.size != 0u) {
    cupidbuild_host_set_error(transaction,
                              "checked tool wrote unexpected standard error");
    result = 125;
  }
  free(stdout_bytes);
  free(stderr_bytes);
  cupidbuild_host_delete_file(transaction->tool_stdout);
  cupidbuild_host_delete_file(transaction->tool_stderr);
  if (result < 0) {
    cupidbuild_host_set_error(transaction, "checked tool could not be started");
  } else if (result == 124) {
    cupidbuild_host_set_error(transaction, "checked tool timed out");
  }
  return result;
}

int cupidbuild_host_capture_candidate(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out, unsigned char **bytes_out) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      !cupidbuild_host_read_regular(transaction->candidate, 0,
                                    &transaction->candidate_snapshot,
                                    bytes_out)) {
    cupidbuild_host_set_error(transaction,
                              "checked CupidASM output cannot be pinned");
    return 0;
  }
  transaction->candidate_captured = 1;
  if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
    *snapshot_out = transaction->candidate_snapshot;
  }
  return 1;
}

int cupidbuild_host_require_candidate(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      expected == (const cupidbuild_host_snapshot_t *)0 ||
      !cupidbuild_host_read_regular(transaction->candidate, 0, &current,
                                    (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&current, expected)) {
    cupidbuild_host_set_error(transaction,
                              "checked CupidASM output changed while validation ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_capture_private_output(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out, unsigned char **bytes_out) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      !cupidbuild_host_read_regular(transaction->private_output, 0,
                                    &transaction->private_output_snapshot,
                                    bytes_out)) {
    cupidbuild_host_set_error(transaction,
                              "checked CupidASM private output cannot be pinned");
    return 0;
  }
  transaction->private_output_captured = 1;
  if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
    *snapshot_out = transaction->private_output_snapshot;
  }
  return 1;
}

int cupidbuild_host_require_private_output(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->private_output_captured == 0 ||
      expected == (const cupidbuild_host_snapshot_t *)0 ||
      !cupidbuild_host_read_regular(transaction->private_output, 0, &current,
                                    (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&current, expected)) {
    cupidbuild_host_set_error(
        transaction,
        "checked CupidASM private output changed while validation ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_require_inputs(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    cupidbuild_host_snapshot_t current;
    if (!cupidbuild_host_read_regular(transaction->inputs[index].live_path, 0,
                                      &current, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(
            &current, &transaction->inputs[index].snapshot)) {
      cupidbuild_host_set_error(
          transaction,
          index == 0u
              ? "CupidASM source changed while checked tools ran"
              : "checked seed inputs changed while checked tools ran");
      return 0;
    }
  }
  return 1;
}

int cupidbuild_host_require_publication_boundary(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t parent;
  cupidbuild_host_snapshot_t output;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  if (!cupidbuild_host_directory_snapshot(transaction->output_parent, &parent) ||
      memcmp(parent.identity, transaction->output_parent_snapshot.identity,
             sizeof(parent.identity)) != 0) {
    cupidbuild_host_set_error(transaction,
                              "code output parent changed while checked tools ran");
    return 0;
  }
  if (!cupidbuild_host_require_lock(transaction)) {
    return 0;
  }
  if (!cupidbuild_host_read_regular(transaction->output_path, 1, &output,
                                    (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(
          &output, &transaction->initial_output_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "code output changed while checked tools ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_publish(cupidbuild_host_transaction_t *transaction) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->candidate_captured == 0 ||
      !cupidbuild_host_require_inputs(transaction) ||
      !cupidbuild_host_require_candidate(transaction,
                                         &transaction->candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction)) {
    return 0;
  }
  if (!cupidbuild_host_require_lock(transaction)) {
    return 0;
  }
  if (!cupidbuild_host_atomic_replace(transaction)) {
    cupidbuild_host_set_error(transaction,
                              "validated CupidASM output could not be published");
    return 0;
  }
  return 1;
}

const char *cupidbuild_host_error(
    const cupidbuild_host_transaction_t *transaction) {
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      transaction->error[0] == '\0') {
    return "hosted CupidASM transaction failed";
  }
  return transaction->error;
}
