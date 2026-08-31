#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(_WIN32) &&                                                \
    (defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) ||               \
     defined(CUPIDBUILD_PUBLICATION_RACE_TEST) ||                     \
     defined(CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST)) &&            \
    !defined(_CRT_SECURE_NO_WARNINGS)
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
#if !defined(FILE_ATTRIBUTE_DEVICE)
#define FILE_ATTRIBUTE_DEVICE 0x00000040u
#endif
#if !defined(FILE_LIST_DIRECTORY)
#define FILE_LIST_DIRECTORY 0x00000001u
#endif
#if !defined(CUPID_HOSTED_I386_WINDOWS_H)
#include <fcntl.h>
#include <io.h>
#endif
#else
#if !defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
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
int cupid_linux_syscall5(int number, unsigned int first,
                         unsigned int second, unsigned int third,
                         unsigned int fourth, unsigned int fifth);
#endif
#endif

#define CUPIDBUILD_HOST_PATH_BYTES 8192u
#define CUPIDBUILD_HOST_ERROR_BYTES 512u
#define CUPIDBUILD_HOST_INPUTS 512u
#if !defined(CUPIDBUILD_HOST_FILE_LIMIT)
#define CUPIDBUILD_HOST_FILE_LIMIT 67108864u
#endif
#if !defined(CUPIDBUILD_HOST_STREAM_LIMIT)
#define CUPIDBUILD_HOST_STREAM_LIMIT CUPIDBUILD_HOST_FILE_LIMIT
#endif
#define CUPIDBUILD_HOST_PRIVATE_ATTEMPTS 4096u
#define CUPIDBUILD_HOST_DIRECTORY_BYTES 4096u
#define CUPIDBUILD_HOST_DISCOVERY_FILES 2048u
#define CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES 512u
#define CUPIDBUILD_HOST_RETAINED_DIRECTORIES 2048u
#define CUPIDBUILD_HOST_CANDIDATE_PUBLISH "candidate.publish"
#if defined(_WIN32)
#define CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES 32767u
#define CUPIDBUILD_HOST_WINDOWS_REAP_MILLISECONDS 1000u
#endif

typedef struct {
  char live_path[CUPIDBUILD_HOST_PATH_BYTES];
  char frozen_path[CUPIDBUILD_HOST_PATH_BYTES];
  char frozen_name[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t snapshot;
  cupidbuild_host_snapshot_t frozen_snapshot;
#if defined(_WIN32)
  HANDLE frozen_handle;
#else
  int frozen_descriptor;
#endif
} cupidbuild_host_input_t;

typedef struct {
  char *logical;
  cupidbuild_host_snapshot_t snapshot;
#if defined(_WIN32)
  HANDLE handle;
#else
  int descriptor;
#endif
} cupidbuild_host_discovery_directory_t;

typedef struct {
  cupidbuild_host_discovery_directory_t *directories;
  size_t count;
  size_t capacity;
} cupidbuild_host_discovery_worklist_t;

struct cupidbuild_host_transaction {
  char repository_root[CUPIDBUILD_HOST_PATH_BYTES];
  char source_path[CUPIDBUILD_HOST_PATH_BYTES];
  char frozen_source[CUPIDBUILD_HOST_PATH_BYTES];
  char output_path[CUPIDBUILD_HOST_PATH_BYTES];
  char output_parent[CUPIDBUILD_HOST_PATH_BYTES];
  char output_name[CUPIDBUILD_HOST_PATH_BYTES];
  char initial_output_backup_name[CUPIDBUILD_HOST_PATH_BYTES];
  char private_name[64];
  char private_prefix[64];
  char candidate_name[128];
  char candidate_publish_name[128];
  char private_output_name[128];
  char tool_stdout_name[128];
  char tool_stderr_name[128];
  char private_root[CUPIDBUILD_HOST_PATH_BYTES];
  char candidate[CUPIDBUILD_HOST_PATH_BYTES];
  char private_output[CUPIDBUILD_HOST_PATH_BYTES];
  char tool_stdout[CUPIDBUILD_HOST_PATH_BYTES];
  char tool_stderr[CUPIDBUILD_HOST_PATH_BYTES];
  char lock_name[CUPIDBUILD_HOST_PATH_BYTES];
  char lock_path[CUPIDBUILD_HOST_PATH_BYTES];
  char error[CUPIDBUILD_HOST_ERROR_BYTES];
  cupidbuild_host_input_t *inputs;
  cupidbuild_host_discovery_directory_t *discovery_directories;
  unsigned int input_count;
  unsigned int input_capacity;
  size_t discovery_directory_count;
  size_t discovery_directory_capacity;
  cupidbuild_host_snapshot_t output_parent_snapshot;
  cupidbuild_host_snapshot_t initial_output_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  cupidbuild_host_snapshot_t candidate_publish_snapshot;
  cupidbuild_host_snapshot_t private_output_snapshot;
  cupidbuild_host_snapshot_t lock_snapshot;
  cupidbuild_host_snapshot_t private_root_snapshot;
  cupidbuild_host_snapshot_t private_reservation_snapshot;
  cupidbuild_host_snapshot_t repository_root_snapshot;
  cupidbuild_host_snapshot_t tool_stdout_snapshot;
  cupidbuild_host_snapshot_t tool_stderr_snapshot;
  int candidate_captured;
  int candidate_sealed;
  int candidate_publish_created;
  int candidate_published;
  int private_output_captured;
  int private_output_sealed;
  int tool_stdout_sealed;
  int tool_stderr_sealed;
  int lock_held;
  int private_created;
  int private_flat;
  int output_parent_is_repository_root;
  int initial_output_parked;
  int runner_transaction;
  int captured;
  int discovery_sealed;
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  unsigned int discovery_boundary_count;
#if defined(_WIN32)
  unsigned int discovery_named_query_count;
#endif
#endif
  int publication_committed;
  int namespace_interfered;
#if defined(_WIN32)
  HANDLE repository_root_handle;
  HANDLE output_parent_handle;
  HANDLE initial_output_handle;
  HANDLE lock_handle;
  HANDLE candidate_handle;
  HANDLE private_output_handle;
  HANDLE tool_stdout_handle;
  HANDLE tool_stderr_handle;
  HANDLE private_handle;
  HANDLE working_directory_handle;
#else
  int repository_root_descriptor;
  int output_parent_descriptor;
  int working_directory_descriptor;
  int private_reservation_descriptor;
  int candidate_descriptor;
  int private_output_descriptor;
  int private_descriptor;
  int tool_stdout_descriptor;
  int tool_stderr_descriptor;
#endif
};

#if defined(_WIN32)
static int cupidbuild_host_windows_read_open_regular(
    HANDLE handle, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out);
static int cupidbuild_host_windows_dispose_retained_at(
    HANDLE parent, const char *name, cupidbuild_host_snapshot_t *expected,
    HANDLE *retained_handle);
#endif
static int cupidbuild_host_candidate_ready_for_publication(
    cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_published_candidate_matches(
    cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_require_public_binding(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected_output,
    int require_discovery);
static int cupidbuild_host_read_output(
    cupidbuild_host_transaction_t *transaction, int optional,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out);
static int cupidbuild_host_private_root_is_owned(
    cupidbuild_host_transaction_t *transaction);
#if !defined(_WIN32)
static int cupidbuild_host_posix_atomic_replace(
    cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_posix_dispose_parked_output(
    cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_directory_entry_missing_at(
    int directory, const char *name);
static int cupidbuild_host_read_open_file(
    int descriptor, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out);
#endif

struct cupidbuild_host_profile_parent {
  char repository_root[CUPIDBUILD_HOST_PATH_BYTES];
  char build_path[CUPIDBUILD_HOST_PATH_BYTES];
  char bootstrap_path[CUPIDBUILD_HOST_PATH_BYTES];
  char error[CUPIDBUILD_HOST_ERROR_BYTES];
  cupidbuild_host_snapshot_t repository_root_snapshot;
  cupidbuild_host_snapshot_t build_snapshot;
  cupidbuild_host_snapshot_t bootstrap_snapshot;
  int build_created;
  int bootstrap_created;
  int committed;
#if defined(_WIN32)
  HANDLE repository_root_handle;
  HANDLE build_handle;
  HANDLE bootstrap_handle;
#else
  int repository_root_descriptor;
  int build_descriptor;
  int bootstrap_descriptor;
#endif
};

static int cupidbuild_host_repository_logical_path(
    const cupidbuild_host_transaction_t *transaction, const char *path,
    const char **logical_out);

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

void cupidbuild_host_sha256_bytes(const unsigned char *contents, size_t size,
                                  unsigned char digest[32]) {
  cupidbuild_sha256(contents, size, digest);
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

int cupidbuild_host_snapshot_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right) {
  return left->present == right->present && left->size == right->size &&
         memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
         memcmp(left->identity, right->identity, sizeof(left->identity)) == 0 &&
         memcmp(left->modified, right->modified, sizeof(left->modified)) == 0 &&
         memcmp(left->changed, right->changed, sizeof(left->changed)) == 0;
}

static int cupidbuild_host_snapshot_identity_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right) {
  return left->present != 0 && right->present != 0 &&
         memcmp(left->identity, right->identity, sizeof(left->identity)) == 0;
}

static int cupidbuild_host_lock_snapshot_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right) {
  return left->present == right->present && left->size == right->size &&
         memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
         memcmp(left->identity, right->identity, sizeof(left->identity)) == 0;
}

#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_publication_test_pause(const char *phase) {
  const char *requested = getenv("CUPIDBUILD_PUBLICATION_TEST_PHASE");
  const char *ready = getenv("CUPIDBUILD_PUBLICATION_TEST_READY");
  const char *resume = getenv("CUPIDBUILD_PUBLICATION_TEST_RESUME");
  FILE *signal;
  unsigned int attempt;
  if (requested == (const char *)0 || strcmp(requested, phase) != 0) {
    return 1;
  }
  if (ready == (const char *)0 || resume == (const char *)0) {
    return 0;
  }
  signal = fopen(ready, "wb");
  if (signal == (FILE *)0 || fclose(signal) != 0) {
    return 0;
  }
  for (attempt = 0u; attempt < 30000u; attempt++) {
    FILE *permission = fopen(resume, "rb");
    if (permission != (FILE *)0) {
      return fclose(permission) == 0;
    }
#if defined(_WIN32)
    Sleep(1u);
#else
    {
      struct timespec pause_time;
      pause_time.tv_sec = 0;
      pause_time.tv_nsec = 1000000L;
      (void)nanosleep(&pause_time, (struct timespec *)0);
    }
#endif
  }
  return 0;
}
#endif

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

static int cupidbuild_host_discovery_suffix(
    const char *name, const char *const *suffixes, size_t suffix_count) {
  size_t index;
  size_t name_size = strlen(name);
  for (index = 0u; index < suffix_count; index++) {
    size_t suffix_size = strlen(suffixes[index]);
    if (suffix_size <= name_size &&
        memcmp(name + name_size - suffix_size, suffixes[index],
               suffix_size) == 0) {
      return 1;
    }
  }
  return 0;
}

static int cupidbuild_host_path_list_add(
    cupidbuild_host_path_list_t *paths, const char *path,
    const cupidbuild_host_snapshot_t *snapshot, size_t limit) {
  char **grown;
  cupidbuild_host_snapshot_t *grown_snapshots;
  char *copy;
  size_t size = strlen(path);
  size_t capacity;
  if (paths->count >= limit) {
    return 0;
  }
  if (paths->count == paths->capacity) {
    capacity = paths->capacity == 0u ? 64u : paths->capacity * 2u;
    if (capacity > limit) {
      capacity = limit;
    }
    grown = (char **)realloc(paths->paths, capacity * sizeof(*grown));
    if (grown == (char **)0) {
      return 0;
    }
    paths->paths = grown;
    grown_snapshots = (cupidbuild_host_snapshot_t *)realloc(
        paths->snapshots, capacity * sizeof(*grown_snapshots));
    if (grown_snapshots == (cupidbuild_host_snapshot_t *)0) {
      return 0;
    }
    paths->snapshots = grown_snapshots;
    paths->capacity = capacity;
  }
  copy = (char *)malloc(size + 1u);
  if (copy == (char *)0) {
    return 0;
  }
  (void)memcpy(copy, path, size + 1u);
  paths->paths[paths->count] = copy;
  paths->snapshots[paths->count] = *snapshot;
  paths->count++;
  return 1;
}

static int cupidbuild_host_discovery_worklist_add(
    cupidbuild_host_discovery_worklist_t *worklist, const char *logical,
    const cupidbuild_host_snapshot_t *snapshot,
#if defined(_WIN32)
    HANDLE handle
#else
    int descriptor
#endif
) {
  cupidbuild_host_discovery_directory_t *grown;
  cupidbuild_host_discovery_directory_t *directory;
  char *copy;
  size_t size = strlen(logical);
  size_t capacity;
  if (worklist->count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES) {
    return 0;
  }
  if (worklist->count == worklist->capacity) {
    capacity = worklist->capacity == 0u ? 16u : worklist->capacity * 2u;
    if (capacity > CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES) {
      capacity = CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES;
    }
    grown = (cupidbuild_host_discovery_directory_t *)realloc(
        worklist->directories, capacity * sizeof(*grown));
    if (grown == (cupidbuild_host_discovery_directory_t *)0) {
      return 0;
    }
    worklist->directories = grown;
    worklist->capacity = capacity;
  }
  copy = (char *)malloc(size + 1u);
  if (copy == (char *)0) {
    return 0;
  }
  (void)memcpy(copy, logical, size + 1u);
  directory = &worklist->directories[worklist->count++];
  directory->logical = copy;
  directory->snapshot = *snapshot;
#if defined(_WIN32)
  directory->handle = handle;
#else
  directory->descriptor = descriptor;
#endif
  return 1;
}

static int cupidbuild_host_bind_discovery_directory(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_discovery_directory_t *directory,
    int *retained_out) {
  size_t index;
  size_t capacity;
  cupidbuild_host_discovery_directory_t *grown;
  *retained_out = 0;
  for (index = 0u; index < transaction->discovery_directory_count; index++) {
    cupidbuild_host_discovery_directory_t *expected =
        &transaction->discovery_directories[index];
    if (strcmp(expected->logical, directory->logical) == 0) {
      return cupidbuild_host_snapshot_equal(&expected->snapshot,
                                            &directory->snapshot);
    }
    if (cupidbuild_host_snapshot_identity_equal(&expected->snapshot,
                                                &directory->snapshot)) {
      return 0;
    }
  }
  if (transaction->discovery_sealed != 0 ||
      transaction->discovery_directory_count >=
          CUPIDBUILD_HOST_RETAINED_DIRECTORIES) {
    return 0;
  }
  if (transaction->discovery_directory_count ==
      transaction->discovery_directory_capacity) {
    capacity = transaction->discovery_directory_capacity == 0u
                   ? 64u
                   : transaction->discovery_directory_capacity * 2u;
    if (capacity > CUPIDBUILD_HOST_RETAINED_DIRECTORIES) {
      capacity = CUPIDBUILD_HOST_RETAINED_DIRECTORIES;
    }
    grown = (cupidbuild_host_discovery_directory_t *)realloc(
        transaction->discovery_directories, capacity * sizeof(*grown));
    if (grown == (cupidbuild_host_discovery_directory_t *)0) {
      return 0;
    }
    transaction->discovery_directories = grown;
    transaction->discovery_directory_capacity = capacity;
  }
  transaction->discovery_directories[
      transaction->discovery_directory_count++] = *directory;
  *retained_out = 1;
  return 1;
}

static int cupidbuild_host_discovery_add(
    cupidbuild_host_path_list_t *paths, const char *path,
    const cupidbuild_host_snapshot_t *snapshot) {
  return cupidbuild_host_path_list_add(
      paths, path, snapshot, CUPIDBUILD_HOST_DISCOVERY_FILES);
}

static void cupidbuild_host_discovery_swap(
    cupidbuild_host_path_list_t *paths, size_t left, size_t right) {
  char *temporary_path = paths->paths[left];
  cupidbuild_host_snapshot_t temporary_snapshot = paths->snapshots[left];
  paths->paths[left] = paths->paths[right];
  paths->snapshots[left] = paths->snapshots[right];
  paths->paths[right] = temporary_path;
  paths->snapshots[right] = temporary_snapshot;
}

static void cupidbuild_host_discovery_sift(
    cupidbuild_host_path_list_t *paths, size_t root, size_t count) {
  for (;;) {
    size_t child;
    size_t selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (strcmp(paths->paths[selected], paths->paths[child]) < 0) {
      selected = child;
    }
    if (child + 1u < count &&
        strcmp(paths->paths[selected], paths->paths[child + 1u]) < 0) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    cupidbuild_host_discovery_swap(paths, root, selected);
    root = selected;
  }
}

static int cupidbuild_host_discovery_sort(
    cupidbuild_host_path_list_t *paths) {
  size_t start = paths->count / 2u;
  size_t end = paths->count;
  size_t read_index;
  size_t write_index = 0u;
  int valid = 1;
  while (start != 0u) {
    start--;
    cupidbuild_host_discovery_sift(paths, start, paths->count);
  }
  while (end > 1u) {
    cupidbuild_host_discovery_swap(paths, 0u, end - 1u);
    end--;
    cupidbuild_host_discovery_sift(paths, 0u, end);
  }
  for (read_index = 0u; read_index < paths->count; read_index++) {
    if (write_index != 0u &&
        strcmp(paths->paths[write_index - 1u], paths->paths[read_index]) == 0) {
      if (!cupidbuild_host_snapshot_equal(
              &paths->snapshots[write_index - 1u],
              &paths->snapshots[read_index])) {
        valid = 0;
      }
      free(paths->paths[read_index]);
      continue;
    }
    paths->paths[write_index] = paths->paths[read_index];
    paths->snapshots[write_index] = paths->snapshots[read_index];
    write_index++;
  }
  paths->count = write_index;
  return valid;
}

void cupidbuild_host_path_list_close(cupidbuild_host_path_list_t *paths) {
  size_t index;
  if (paths == (cupidbuild_host_path_list_t *)0) {
    return;
  }
  for (index = 0u; index < paths->count; index++) {
    free(paths->paths[index]);
  }
  free(paths->paths);
  free(paths->snapshots);
  (void)memset(paths, 0, sizeof(*paths));
}

#if defined(_WIN32)
static int cupidbuild_host_path_missing(const char *path) {
  DWORD attributes = GetFileAttributesA(path);
  DWORD error = GetLastError();
  return attributes == INVALID_FILE_ATTRIBUTES &&
         (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}

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
          (attributes &
           (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u) {
        return 1;
      }
    }
  }
  return 0;
}

static int cupidbuild_host_read_regular_limit(
    const char *path, int optional, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
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
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
      information.nFileSizeHigh != 0u || information.nFileSizeLow > limit) {
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

static int cupidbuild_host_read_regular(
    const char *path, int optional, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  return cupidbuild_host_read_regular_limit(
      path, optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
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
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u) {
    if (handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle);
    }
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  (void)CloseHandle(handle);
  return 1;
}

static int cupidbuild_host_windows_directory_handle_snapshot(
    HANDLE handle, cupidbuild_host_snapshot_t *snapshot) {
  BY_HANDLE_FILE_INFORMATION information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (handle == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  return 1;
}

static HANDLE cupidbuild_host_windows_open_relative(
    HANDLE parent, const char *name, int directory, int read_contents);
static HANDLE cupidbuild_host_windows_open_relative_access(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access);
static HANDLE cupidbuild_host_windows_open_relative_access_status(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access, long *status_out);
static HANDLE cupidbuild_host_windows_open_relative_access_share_status(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access, unsigned long share_access,
    long *status_out);
static HANDLE cupidbuild_host_windows_open_relative_path(
    HANDLE root, const char *logical, int directory, int read_contents);
static HANDLE cupidbuild_host_windows_open_relative_path_access(
    HANDLE root, const char *logical, int directory, int read_contents,
    unsigned long extra_access);
static HANDLE cupidbuild_host_windows_open_repository(const char *path);
static int cupidbuild_host_windows_query_directory(
    HANDLE directory, int restart, char *name, size_t name_capacity,
    DWORD *attributes_out, DWORD *file_id_high_out,
    DWORD *file_id_low_out, DWORD *last_write_high_out,
    DWORD *last_write_low_out, DWORD *change_high_out,
    DWORD *change_low_out, int *complete_out);
static int cupidbuild_host_windows_named_directory_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *logical,
    HANDLE retained, cupidbuild_host_snapshot_t *snapshot,
    int *metadata_unsettled_out);
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_profile_directory_test_pause(
    const char *ready_variable, const char *resume_variable);
#endif

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

static int cupidbuild_host_discover_platform(
    cupidbuild_host_transaction_t *transaction, const char *logical_root,
    const char *const *suffixes, size_t suffix_count, int skip_hidden_files,
    int reject_matching_nonfiles, cupidbuild_host_path_list_t *paths) {
  cupidbuild_host_discovery_worklist_t worklist;
  cupidbuild_host_snapshot_t *visited;
  cupidbuild_host_snapshot_t repository_snapshot;
  cupidbuild_host_snapshot_t root_snapshot;
  HANDLE repository = transaction->repository_root_handle;
  HANDLE root = INVALID_HANDLE_VALUE;
  size_t visited_count = 0u;
  size_t directory_count = 0u;
  int valid = 1;
  (void)memset(&worklist, 0, sizeof(worklist));
  visited = (cupidbuild_host_snapshot_t *)calloc(
      CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES, sizeof(*visited));
  if (visited == (cupidbuild_host_snapshot_t *)0) {
    free(visited);
    return 0;
  }
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          repository, &repository_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &repository_snapshot)) {
    valid = 0;
  } else {
    unsigned int stable_attempt;
    int root_ready = 0;
    for (stable_attempt = 0u; stable_attempt < 4u; stable_attempt++) {
      int metadata_unsettled = 0;
      root = cupidbuild_host_windows_open_relative_path(
          repository, logical_root, 1, 1);
      if (cupidbuild_host_windows_named_directory_snapshot(
              transaction, logical_root, root, &root_snapshot,
              &metadata_unsettled) &&
          !cupidbuild_host_snapshot_identity_equal(&repository_snapshot,
                                                   &root_snapshot) &&
          cupidbuild_host_discovery_worklist_add(
              &worklist, logical_root, &root_snapshot, root)) {
        root_ready = 1;
        root = INVALID_HANDLE_VALUE;
        break;
      }
      if (root != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(root);
        root = INVALID_HANDLE_VALUE;
      }
      if (metadata_unsettled == 0) {
        break;
      }
    }
    if (root_ready == 0) {
      valid = 0;
    } else {
      visited[visited_count++] = root_snapshot;
      directory_count = 1u;
    }
  }
  if (root != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(root);
  }
  while (valid != 0 && worklist.count != 0u) {
    cupidbuild_host_discovery_directory_t current =
        worklist.directories[worklist.count - 1u];
    cupidbuild_host_snapshot_t completed_snapshot;
    int retained = 0;
    int restart = 1;
    int complete = 0;
    worklist.count--;
    while (valid != 0) {
      char name[260];
      char logical[CUPIDBUILD_HOST_PATH_BYTES];
      DWORD attributes = 0u;
      DWORD file_id_high = 0u;
      DWORD file_id_low = 0u;
      DWORD last_write_high = 0u;
      DWORD last_write_low = 0u;
      DWORD change_high = 0u;
      DWORD change_low = 0u;
      int directory_entry;
      int matches;
      if (!cupidbuild_host_windows_query_directory(
              current.handle, restart, name, sizeof(name), &attributes,
              &file_id_high, &file_id_low, &last_write_high,
              &last_write_low, &change_high, &change_low, &complete)) {
        valid = 0;
        break;
      }
      restart = 0;
      if (complete != 0) {
        break;
      }
      if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        directory_entry =
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
        if (!(name[0] == '.' &&
              (directory_entry != 0 || skip_hidden_files != 0))) {
          if ((attributes &
               (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u ||
              !cupidbuild_host_join(logical, sizeof(logical), current.logical,
                                    name)) {
            valid = 0;
            break;
          }
          matches = cupidbuild_host_discovery_suffix(
              name, suffixes, suffix_count);
          if (directory_entry != 0 && matches != 0 &&
              reject_matching_nonfiles != 0) {
            valid = 0;
            break;
          }
          if (directory_entry != 0) {
            cupidbuild_host_snapshot_t child_snapshot;
            HANDLE child = INVALID_HANDLE_VALUE;
            unsigned int stable_attempt;
            size_t prior;
            int alias = 0;
            int child_ready = 0;
            for (stable_attempt = 0u; stable_attempt < 4u;
                 stable_attempt++) {
              int metadata_unsettled = 0;
              child = cupidbuild_host_windows_open_relative(
                  current.handle, name, 1, 1);
              if (cupidbuild_host_windows_named_directory_snapshot(
                      transaction, logical, child, &child_snapshot,
                      &metadata_unsettled) &&
                  (file_id_high != 0u || file_id_low != 0u) &&
                  child_snapshot.identity[0] == current.snapshot.identity[0] &&
                  child_snapshot.identity[1] == file_id_high &&
                  child_snapshot.identity[2] == file_id_low) {
                child_ready = 1;
                break;
              }
              if (child != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(child);
                child = INVALID_HANDLE_VALUE;
              }
              if (metadata_unsettled == 0) {
                break;
              }
            }
            if (child_ready == 0) {
              if (child != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(child);
              }
              valid = 0;
              break;
            }
            (void)last_write_high;
            (void)last_write_low;
            (void)change_high;
            (void)change_low;
            for (prior = 0u; prior < visited_count; prior++) {
              if (cupidbuild_host_snapshot_identity_equal(
                      &visited[prior], &child_snapshot)) {
                alias = 1;
                break;
              }
            }
            if (alias != 0 ||
                directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
                !cupidbuild_host_discovery_worklist_add(
                    &worklist, logical, &child_snapshot, child)) {
              (void)CloseHandle(child);
              valid = 0;
              break;
            }
            visited[visited_count++] = child_snapshot;
            directory_count++;
          } else if (matches != 0) {
            HANDLE child;
            BY_HANDLE_FILE_INFORMATION information;
            cupidbuild_host_snapshot_t child_snapshot;
            child = cupidbuild_host_windows_open_relative(
                current.handle, name, 0, 1);
            if (child == INVALID_HANDLE_VALUE ||
                !GetFileInformationByHandle(child, &information) ||
                (information.dwFileAttributes &
                 (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
                    0u ||
                (file_id_high == 0u && file_id_low == 0u) ||
                information.dwVolumeSerialNumber !=
                    current.snapshot.identity[0] ||
                information.nFileIndexHigh != file_id_high ||
                information.nFileIndexLow != file_id_low) {
              if (child != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(child);
              }
              valid = 0;
              break;
            }
            if ((information.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0u) {
              if (reject_matching_nonfiles != 0) {
                (void)CloseHandle(child);
                valid = 0;
                break;
              }
            } else if (!cupidbuild_host_windows_read_open_regular(
                           child, CUPIDBUILD_HOST_FILE_LIMIT,
                           &child_snapshot, (unsigned char **)0) ||
                       !cupidbuild_host_discovery_add(
                           paths, logical, &child_snapshot)) {
              (void)CloseHandle(child);
              valid = 0;
              break;
            }
            if (!CloseHandle(child)) {
              valid = 0;
              break;
            }
          }
        }
      }
    }
    if (valid != 0 &&
        (!cupidbuild_host_windows_named_directory_snapshot(
             transaction, current.logical, current.handle,
             &completed_snapshot, (int *)0) ||
         !cupidbuild_host_snapshot_equal(&current.snapshot,
                                         &completed_snapshot))) {
      valid = 0;
    }
    if (valid != 0) {
      current.snapshot = completed_snapshot;
      if (!cupidbuild_host_bind_discovery_directory(
              transaction, &current, &retained)) {
        valid = 0;
      }
    }
    if (retained != 0) {
      current.handle = INVALID_HANDLE_VALUE;
      current.logical = (char *)0;
    }
    if (current.handle != INVALID_HANDLE_VALUE &&
        !CloseHandle(current.handle)) {
      valid = 0;
    }
    free(current.logical);
  }
  while (worklist.count != 0u) {
    cupidbuild_host_discovery_directory_t *directory =
        &worklist.directories[--worklist.count];
    (void)CloseHandle(directory->handle);
    free(directory->logical);
  }
  free(worklist.directories);
  free(visited);
  return valid;
}

static int cupidbuild_host_absolute_directory(char *destination,
                                              size_t capacity,
                                              const char *path) {
  return _fullpath(destination, path, capacity) != (char *)0;
}

static int cupidbuild_host_make_executable(const char *path) {
  return path != (const char *)0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)DeleteFileA(path);
}

static unsigned int cupidbuild_host_process_id(void) {
  return GetCurrentProcessId();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0,
                               (DWORD)process_id);
  DWORD status = 0u;
  if (process == (HANDLE)0) {
    return GetLastError() != ERROR_INVALID_PARAMETER;
  }
  if (!GetExitCodeProcess(process, &status)) {
    (void)CloseHandle(process);
    return 1;
  }
  (void)CloseHandle(process);
  return status == STILL_ACTIVE;
}

static int cupidbuild_host_windows_append_character(char *command,
                                                     size_t capacity,
                                                     size_t *used,
                                                     char character) {
  if (*used + 1u >= capacity) {
    return 0;
  }
  command[*used] = character;
  *used = *used + 1u;
  command[*used] = '\0';
  return 1;
}

static int cupidbuild_host_windows_append_argument(char *command,
                                                    size_t capacity,
                                                    size_t *used,
                                                    const char *argument) {
  const char *cursor = argument;
  if (*used != 0u &&
      !cupidbuild_host_windows_append_character(command, capacity, used, ' ')) {
    return 0;
  }
  if (!cupidbuild_host_windows_append_character(command, capacity, used, '"')) {
    return 0;
  }
  while (*cursor != '\0') {
    size_t slash_count = 0u;
    size_t index;
    while (cursor[slash_count] == '\\') {
      slash_count++;
    }
    cursor += slash_count;
    if (*cursor == '"') {
      for (index = 0u; index < slash_count * 2u + 1u; index++) {
        if (!cupidbuild_host_windows_append_character(command, capacity, used,
                                                       '\\')) {
          return 0;
        }
      }
      if (!cupidbuild_host_windows_append_character(command, capacity, used,
                                                     '"')) {
        return 0;
      }
      cursor++;
    } else if (*cursor == '\0') {
      for (index = 0u; index < slash_count * 2u; index++) {
        if (!cupidbuild_host_windows_append_character(command, capacity, used,
                                                       '\\')) {
          return 0;
        }
      }
    } else {
      for (index = 0u; index < slash_count; index++) {
        if (!cupidbuild_host_windows_append_character(command, capacity, used,
                                                       '\\')) {
          return 0;
        }
      }
      if (!cupidbuild_host_windows_append_character(command, capacity, used,
                                                     *cursor)) {
        return 0;
      }
      cursor++;
    }
  }
  return cupidbuild_host_windows_append_character(command, capacity, used,
                                                   '"');
}

static int cupidbuild_host_windows_open_snapshot(
    HANDLE handle, cupidbuild_host_snapshot_t *snapshot) {
  BY_HANDLE_FILE_INFORMATION information;
  if (snapshot == (cupidbuild_host_snapshot_t *)0) {
    return 1;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (!GetFileInformationByHandle(handle, &information) ||
      information.nFileSizeHigh != 0u) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->size = (size_t)information.nFileSizeLow;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  return 1;
}

static void cupidbuild_host_windows_close_attribute_list(
    STARTUPINFOEXA *startup, int initialized) {
  if (startup == (STARTUPINFOEXA *)0) {
    return;
  }
  if (initialized != 0 &&
      startup->lpAttributeList != (LPPROC_THREAD_ATTRIBUTE_LIST)0) {
    DeleteProcThreadAttributeList(startup->lpAttributeList);
  }
  free(startup->lpAttributeList);
  startup->lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)0;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const cupidbuild_host_snapshot_t *expected_tool,
                                       HANDLE tool_handle,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       HANDLE stdout_handle,
                                       HANDLE stderr_handle,
                                       const char *working_directory,
                                       int working_descriptor,
                                       const cupidbuild_host_transaction_t *
                                           inherited_transaction,
                                       cupidbuild_host_snapshot_t *stdout_opened,
                                       cupidbuild_host_snapshot_t *stderr_opened,
                                       unsigned int timeout_milliseconds) {
  STARTUPINFOEXA startup;
  PROCESS_INFORMATION process;
  SECURITY_ATTRIBUTES security;
  HANDLE standard_input = INVALID_HANDLE_VALUE;
  HANDLE standard_output = INVALID_HANDLE_VALUE;
  HANDLE standard_error = INVALID_HANDLE_VALUE;
  HANDLE inherited_handles[3];
  cupidbuild_host_snapshot_t tool_path_snapshot;
  cupidbuild_host_snapshot_t tool_snapshot;
  char *command = (char *)0;
  size_t used = 0u;
  size_t index;
  DWORD wait_status;
  DWORD exit_code = 125u;
  SIZE_T attribute_list_size = 0u;
  int attribute_list_initialized = 0;
  int adapter_failed = 0;
  int streams_inheritable = 0;
  int stdout_inherit_cleared;
  int stderr_inherit_cleared;
  int own_stream_handles =
      stdout_handle == INVALID_HANDLE_VALUE &&
      stderr_handle == INVALID_HANDLE_VALUE;
  (void)working_descriptor;
  (void)inherited_transaction;
  (void)memset(&startup, 0, sizeof(startup));
  (void)memset(&process, 0, sizeof(process));
  (void)memset(&security, 0, sizeof(security));
  security.nLength = (DWORD)sizeof(security);
  security.bInheritHandle = 1;
  startup.StartupInfo.cb = (DWORD)sizeof(startup);
  if ((stdout_handle == INVALID_HANDLE_VALUE) !=
      (stderr_handle == INVALID_HANDLE_VALUE)) {
    return -1;
  }
  if (tool_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_read_open_regular(
          tool_handle, CUPIDBUILD_HOST_FILE_LIMIT, &tool_snapshot,
          (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&tool_snapshot, expected_tool) ||
      !cupidbuild_host_read_regular(tool, 0, &tool_path_snapshot,
                                    (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&tool_path_snapshot, expected_tool)) {
    return -1;
  }
  standard_input = CreateFileA("NUL", GENERIC_READ, 0u, &security,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               (HANDLE)0);
  if (own_stream_handles != 0) {
    standard_output = CreateFileA(stdout_path, GENERIC_WRITE, 0u, &security,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                  (HANDLE)0);
    standard_error = CreateFileA(stderr_path, GENERIC_WRITE, 0u, &security,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                 (HANDLE)0);
  } else {
    standard_output = stdout_handle;
    standard_error = stderr_handle;
  }
  if (standard_input == INVALID_HANDLE_VALUE ||
      standard_output == INVALID_HANDLE_VALUE ||
      standard_error == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_open_snapshot(standard_output,
                                              stdout_opened) ||
      !cupidbuild_host_windows_open_snapshot(standard_error,
                                              stderr_opened)) {
    if (standard_input != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_input);
    }
    if (own_stream_handles != 0 &&
        standard_output != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_output);
    }
    if (own_stream_handles != 0 && standard_error != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(standard_error);
    }
    return -1;
  }
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = standard_input;
  startup.StartupInfo.hStdOutput = standard_output;
  startup.StartupInfo.hStdError = standard_error;
  command = (char *)calloc(CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES, 1u);
  if (command == (char *)0 ||
      !cupidbuild_host_windows_append_argument(
          command, CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES, &used, tool)) {
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
    }
    free(command);
    return -1;
  }
  for (index = 0u; arguments[index] != (const char *)0; index++) {
    if (!cupidbuild_host_windows_append_argument(
            command, CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES, &used,
            arguments[index])) {
      (void)CloseHandle(standard_input);
      if (own_stream_handles != 0) {
        (void)CloseHandle(standard_output);
        (void)CloseHandle(standard_error);
      }
      free(command);
      return -1;
    }
  }
  inherited_handles[0] = standard_input;
  inherited_handles[1] = standard_output;
  inherited_handles[2] = standard_error;
  (void)InitializeProcThreadAttributeList(
      (LPPROC_THREAD_ATTRIBUTE_LIST)0, 1u, 0u, &attribute_list_size);
  if (attribute_list_size == 0u) {
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
    }
    free(command);
    return -1;
  }
  startup.lpAttributeList =
      (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(attribute_list_size, 1u);
  if (startup.lpAttributeList == (LPPROC_THREAD_ATTRIBUTE_LIST)0 ||
      !InitializeProcThreadAttributeList(
          startup.lpAttributeList, 1u, 0u, &attribute_list_size)) {
    cupidbuild_host_windows_close_attribute_list(&startup, 0);
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
    }
    free(command);
    return -1;
  }
  attribute_list_initialized = 1;
  if (own_stream_handles == 0) {
    if (!SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT,
                              HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT,
                              HANDLE_FLAG_INHERIT)) {
      (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
      (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
      cupidbuild_host_windows_close_attribute_list(
          &startup, attribute_list_initialized);
      (void)CloseHandle(standard_input);
      free(command);
      return -1;
    }
    streams_inheritable = 1;
  }
  if (!UpdateProcThreadAttribute(
          startup.lpAttributeList, 0u, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited_handles, (SIZE_T)sizeof(inherited_handles), (void *)0,
          (SIZE_T *)0)) {
    if (streams_inheritable != 0) {
      (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
      (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
    }
    cupidbuild_host_windows_close_attribute_list(
        &startup, attribute_list_initialized);
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
    }
    free(command);
    return -1;
  }
  if (!CreateProcessA(tool, command, (LPSECURITY_ATTRIBUTES)0,
                      (LPSECURITY_ATTRIBUTES)0, 1,
                      EXTENDED_STARTUPINFO_PRESENT, (void *)0,
                      working_directory, &startup.StartupInfo, &process)) {
    if (streams_inheritable != 0) {
      (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
      (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
    }
    cupidbuild_host_windows_close_attribute_list(
        &startup, attribute_list_initialized);
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
      (void)CloseHandle(standard_output);
      (void)CloseHandle(standard_error);
    }
    free(command);
    return -1;
  }
  cupidbuild_host_windows_close_attribute_list(
      &startup, attribute_list_initialized);
  if (streams_inheritable != 0) {
    stdout_inherit_cleared =
        SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u) != 0;
    stderr_inherit_cleared =
        SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u) != 0;
    if (stdout_inherit_cleared == 0 || stderr_inherit_cleared == 0) {
      adapter_failed = 1;
    }
  }
  free(command);
  (void)CloseHandle(standard_input);
  if (own_stream_handles != 0) {
    (void)CloseHandle(standard_output);
    (void)CloseHandle(standard_error);
  }
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
  if (!cupidbuild_host_publication_test_pause("after-tool-launch")) {
    adapter_failed = 1;
  }
#endif
  wait_status = WaitForSingleObject(process.hProcess,
                                    (DWORD)timeout_milliseconds);
  if (wait_status == WAIT_TIMEOUT) {
    int terminated;
#if defined(CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST)
    if (getenv("CUPIDBUILD_WINDOWS_TEST_FORCE_TERMINATION_FAILURE") !=
        (const char *)0) {
      terminated = 0;
    } else
#endif
    {
      terminated = TerminateProcess(process.hProcess, 124u) != 0;
    }
    DWORD reaped = WaitForSingleObject(
        process.hProcess, CUPIDBUILD_HOST_WINDOWS_REAP_MILLISECONDS);
    if (terminated == 0 || reaped != WAIT_OBJECT_0) {
      adapter_failed = 1;
    }
    exit_code = 124u;
  } else if (wait_status != WAIT_OBJECT_0) {
    int terminated = TerminateProcess(process.hProcess, 125u) != 0;
    DWORD reaped = WaitForSingleObject(
        process.hProcess, CUPIDBUILD_HOST_WINDOWS_REAP_MILLISECONDS);
    (void)terminated;
    (void)reaped;
    adapter_failed = 1;
  } else if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
    adapter_failed = 1;
  }
  (void)CloseHandle(process.hThread);
  (void)CloseHandle(process.hProcess);
  if (wait_status == WAIT_TIMEOUT && adapter_failed == 0) {
    return -2;
  }
  if (wait_status != WAIT_OBJECT_0 || adapter_failed != 0) {
    return -1;
  }
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
  unsigned short length;
  unsigned short maximum_length;
  unsigned short *buffer;
} cupidbuild_windows_unicode_string_t;

typedef struct {
  unsigned long length;
  HANDLE root_directory;
  cupidbuild_windows_unicode_string_t *object_name;
  unsigned long attributes;
  void *security_descriptor;
  void *security_quality_of_service;
} cupidbuild_windows_object_attributes_t;

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
__declspec(dllimport) long __stdcall NtCreateFile(
    HANDLE *file, unsigned long access,
    cupidbuild_windows_object_attributes_t *attributes,
    cupidbuild_windows_io_status_t *status, void *allocation_size,
    unsigned long file_attributes, unsigned long sharing,
    unsigned long disposition, unsigned long options, void *extended,
    unsigned long extended_size);
__declspec(dllimport) long __stdcall NtQueryDirectoryFile(
    HANDLE file, HANDLE event, void *apc_routine, void *apc_context,
    cupidbuild_windows_io_status_t *status, void *information,
    unsigned long length, unsigned int information_class,
    unsigned char return_single_entry,
    cupidbuild_windows_unicode_string_t *file_name,
    unsigned char restart_scan);
__declspec(dllimport) long __stdcall NtSetInformationFile(
    HANDLE file, cupidbuild_windows_io_status_t *status,
    void *information, unsigned long length,
    unsigned int information_class);
#define cupid_windows_nt_create_file NtCreateFile
#define cupid_windows_nt_query_directory_file NtQueryDirectoryFile
#define cupid_windows_nt_set_information_file NtSetInformationFile
#endif

#define CUPIDBUILD_WINDOWS_FILE_OPEN 1u
#define CUPIDBUILD_WINDOWS_FILE_CREATE 2u
#define CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE 0x00000001u
#define CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE 0x00000040u
#define CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT 0x00000020u
#define CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT 0x00200000u
#define CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE 0x00000040u
#define CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE 0x00001000u
#define CUPIDBUILD_WINDOWS_OBJECT_INHERIT 0x00000002u

static unsigned int cupidbuild_host_windows_u32(const unsigned char *bytes) {
  return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u) |
         ((unsigned int)bytes[2] << 16u) |
         ((unsigned int)bytes[3] << 24u);
}

static HANDLE cupidbuild_host_windows_open_relative_access_share_status(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access, unsigned long share_access,
    long *status_out) {
  unsigned short name_buffer[260];
  cupidbuild_windows_unicode_string_t unicode_name;
  cupidbuild_windows_object_attributes_t attributes;
  cupidbuild_windows_io_status_t status;
  BY_HANDLE_FILE_INFORMATION information;
  HANDLE handle = INVALID_HANDLE_VALUE;
  size_t name_size = strlen(name);
  size_t index;
  unsigned long access = FILE_READ_ATTRIBUTES | SYNCHRONIZE | extra_access;
  unsigned long options = CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
                          CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT;
  long result;
  if (status_out != (long *)0) {
    *status_out = (long)0xc000000du;
  }
  if (parent == INVALID_HANDLE_VALUE || name_size == 0u ||
      name_size >= sizeof(name_buffer) / sizeof(name_buffer[0])) {
    return INVALID_HANDLE_VALUE;
  }
  for (index = 0u; index < name_size; index++) {
    unsigned char character = (unsigned char)name[index];
    if (character >= 128u || character == '/' || character == '\\') {
      return INVALID_HANDLE_VALUE;
    }
    name_buffer[index] = (unsigned short)character;
  }
  name_buffer[name_size] = 0u;
  if (directory != 0) {
    access |= FILE_TRAVERSE;
    if (read_contents != 0) {
      access |= FILE_LIST_DIRECTORY;
    }
    options |= CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE;
  } else {
    if (read_contents != 0) {
      access |= GENERIC_READ;
    }
    options |= CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE;
  }
  unicode_name.length = (unsigned short)(name_size * 2u);
  unicode_name.maximum_length = (unsigned short)((name_size + 1u) * 2u);
  unicode_name.buffer = name_buffer;
  (void)memset(&attributes, 0, sizeof(attributes));
  attributes.length = (unsigned long)sizeof(attributes);
  attributes.root_directory = parent;
  attributes.object_name = &unicode_name;
  attributes.attributes = CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE |
                          CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE;
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_create_file(
      &handle, access, &attributes, &status, (void *)0, 0u,
      share_access,
      CUPIDBUILD_WINDOWS_FILE_OPEN, options, (void *)0, 0u);
  if (status_out != (long *)0) {
    *status_out = result;
  }
  if (result < 0 ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u ||
      (((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) !=
       (directory != 0))) {
    if (handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle);
    }
    return INVALID_HANDLE_VALUE;
  }
  return handle;
}

static HANDLE cupidbuild_host_windows_open_relative_access_status(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access, long *status_out) {
  return cupidbuild_host_windows_open_relative_access_share_status(
      parent, name, directory, read_contents, extra_access,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, status_out);
}

static HANDLE cupidbuild_host_windows_open_relative_access(
    HANDLE parent, const char *name, int directory, int read_contents,
    unsigned long extra_access) {
  return cupidbuild_host_windows_open_relative_access_status(
      parent, name, directory, read_contents, extra_access, (long *)0);
}

static HANDLE cupidbuild_host_windows_open_relative(
    HANDLE parent, const char *name, int directory, int read_contents) {
  return cupidbuild_host_windows_open_relative_access(
      parent, name, directory, read_contents, 0u);
}

static HANDLE cupidbuild_host_windows_open_relative_path_access(
    HANDLE root, const char *logical, int directory, int read_contents,
    unsigned long extra_access) {
  HANDLE current = root;
  HANDLE next;
  char component[260];
  size_t start = 0u;
  size_t size = strlen(logical);
  size_t index;
  int owns_current = 0;
  if (root == INVALID_HANDLE_VALUE || size == 0u) {
    return INVALID_HANDLE_VALUE;
  }
  for (index = 0u; index <= size; index++) {
    if (logical[index] == '/' || logical[index] == '\\' ||
        logical[index] == '\0') {
      size_t component_size = index - start;
      int last = index == size;
      if (component_size == 0u || component_size >= sizeof(component)) {
        if (owns_current != 0) {
          (void)CloseHandle(current);
        }
        return INVALID_HANDLE_VALUE;
      }
      (void)memcpy(component, logical + start, component_size);
      component[component_size] = '\0';
      next = cupidbuild_host_windows_open_relative_access(
          current, component, last != 0 ? directory : 1,
          last != 0 ? read_contents : 1,
          last != 0 ? extra_access : 0u);
      if (owns_current != 0) {
        (void)CloseHandle(current);
      }
      if (next == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
      }
      current = next;
      owns_current = 1;
      start = index + 1u;
    }
  }
  return current;
}

static HANDLE cupidbuild_host_windows_open_relative_path(
    HANDLE root, const char *logical, int directory, int read_contents) {
  return cupidbuild_host_windows_open_relative_path_access(
      root, logical, directory, read_contents, 0u);
}

static HANDLE cupidbuild_host_windows_open_repository(const char *path) {
  char absolute[CUPIDBUILD_HOST_PATH_BYTES];
  char anchor[4];
  HANDLE current;
  HANDLE next;
  char component[260];
  size_t start = 3u;
  size_t size;
  size_t index;
  if (_fullpath(absolute, path, sizeof(absolute)) == (char *)0) {
    return INVALID_HANDLE_VALUE;
  }
  size = strlen(absolute);
  if (size < 3u || absolute[1] != ':' ||
      (absolute[2] != '/' && absolute[2] != '\\') ||
      !((absolute[0] >= 'A' && absolute[0] <= 'Z') ||
        (absolute[0] >= 'a' && absolute[0] <= 'z'))) {
    return INVALID_HANDLE_VALUE;
  }
  anchor[0] = absolute[0];
  anchor[1] = ':';
  anchor[2] = '\\';
  anchor[3] = '\0';
  current = CreateFileA(
      anchor,
      (size == 3u ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
                        FILE_DELETE_CHILD
                  : 0u) |
          FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE |
          SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
  if (current == INVALID_HANDLE_VALUE) {
    return INVALID_HANDLE_VALUE;
  }
  if (size == 3u) {
    return current;
  }
  for (index = start; index <= size; index++) {
    if (absolute[index] == '/' || absolute[index] == '\\' ||
        absolute[index] == '\0') {
      size_t component_size = index - start;
      if (component_size == 0u || component_size >= sizeof(component)) {
        (void)CloseHandle(current);
        return INVALID_HANDLE_VALUE;
      }
      (void)memcpy(component, absolute + start, component_size);
      component[component_size] = '\0';
      next = cupidbuild_host_windows_open_relative_access(
          current, component, 1, 1,
          index == size ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
                              FILE_DELETE_CHILD
                        : 0u);
      (void)CloseHandle(current);
      if (next == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
      }
      current = next;
      start = index + 1u;
    }
  }
  return current;
}

static int cupidbuild_host_windows_read_repository_regular(
    HANDLE repository, const char *logical, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  HANDLE handle = cupidbuild_host_windows_open_relative_path(
      repository, logical, 0, 1);
  BY_HANDLE_FILE_INFORMATION information;
  unsigned char *bytes;
  size_t size;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (handle == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
      information.nFileSizeHigh != 0u || information.nFileSizeLow > limit) {
    if (handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle);
    }
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
  if (!CloseHandle(handle)) {
    free(bytes);
    return 0;
  }
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_windows_read_relative_regular(
    HANDLE parent, const char *name, int optional, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  long open_status = 0;
  HANDLE handle = cupidbuild_host_windows_open_relative_access_status(
      parent, name, 0, 1, 0u, &open_status);
  BY_HANDLE_FILE_INFORMATION information;
  unsigned char *bytes;
  size_t size;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (handle == INVALID_HANDLE_VALUE) {
    unsigned long status = (unsigned long)open_status;
    if (optional != 0 &&
        (status == 0xc000000fu || status == 0xc0000034u ||
         status == 0xc000003au)) {
      if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
      }
      return 1;
    }
    return 0;
  }
  if (!GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
      information.nFileSizeHigh != 0u || information.nFileSizeLow > limit) {
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
  if (!CloseHandle(handle)) {
    free(bytes);
    return 0;
  }
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_windows_query_directory_request(
    HANDLE directory, int restart, const char *requested, char *name,
    size_t name_capacity,
    DWORD *attributes_out, DWORD *file_id_high_out,
    DWORD *file_id_low_out, DWORD *last_write_high_out,
    DWORD *last_write_low_out, DWORD *change_high_out,
    DWORD *change_low_out, int *complete_out) {
  unsigned char storage[1031];
  unsigned char *information =
      storage + ((8u - ((size_t)storage & 7u)) & 7u);
  unsigned short requested_buffer[260];
  cupidbuild_windows_unicode_string_t requested_name;
  cupidbuild_windows_unicode_string_t *requested_pointer =
      (cupidbuild_windows_unicode_string_t *)0;
  cupidbuild_windows_io_status_t status;
  size_t bytes;
  size_t name_bytes;
  size_t index;
  long result;
  *complete_out = 0;
  if (requested != (const char *)0) {
    size_t requested_size = strlen(requested);
    if (requested_size == 0u || requested_size >= 260u) {
      return 0;
    }
    for (index = 0u; index < requested_size; index++) {
      unsigned char character = (unsigned char)requested[index];
      if (character >= 128u || character == '/' || character == '\\') {
        return 0;
      }
      requested_buffer[index] = (unsigned short)character;
    }
    requested_buffer[requested_size] = 0u;
    requested_name.length = (unsigned short)(requested_size * 2u);
    requested_name.maximum_length =
        (unsigned short)((requested_size + 1u) * 2u);
    requested_name.buffer = requested_buffer;
    requested_pointer = &requested_name;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_query_directory_file(
      directory, (HANDLE)0, (void *)0, (void *)0, &status, information,
      1024u, 38u, 1u,
      requested_pointer,
      restart != 0 ? 1u : 0u);
  if ((unsigned long)result == 0x80000006u ||
      (unsigned long)result == 0xc000000fu) {
    *complete_out = 1;
    return 1;
  }
  bytes = (size_t)status.information;
  if (result < 0 || bytes < 80u || bytes > 1024u) {
    return 0;
  }
  name_bytes = (size_t)cupidbuild_host_windows_u32(information + 60u);
  if (cupidbuild_host_windows_u32(information) != 0u || name_bytes == 0u ||
      (name_bytes & 1u) != 0u || name_bytes / 2u + 1u > name_capacity ||
      name_bytes > bytes - 80u) {
    return 0;
  }
  for (index = 0u; index < name_bytes / 2u; index++) {
    unsigned int character = (unsigned int)information[80u + index * 2u] |
                             ((unsigned int)information[81u + index * 2u]
                              << 8u);
    if (character == 0u || character >= 128u || character == '/' ||
        character == '\\') {
      return 0;
    }
    name[index] = (char)character;
  }
  name[name_bytes / 2u] = '\0';
  *attributes_out =
      (DWORD)cupidbuild_host_windows_u32(information + 56u);
  *file_id_low_out =
      (DWORD)cupidbuild_host_windows_u32(information + 72u);
  *file_id_high_out =
      (DWORD)cupidbuild_host_windows_u32(information + 76u);
  *last_write_low_out =
      (DWORD)cupidbuild_host_windows_u32(information + 24u);
  *last_write_high_out =
      (DWORD)cupidbuild_host_windows_u32(information + 28u);
  *change_low_out =
      (DWORD)cupidbuild_host_windows_u32(information + 32u);
  *change_high_out =
      (DWORD)cupidbuild_host_windows_u32(information + 36u);
  return 1;
}

typedef struct {
  char name[260];
  DWORD attributes;
  DWORD file_id_high;
  DWORD file_id_low;
  DWORD last_write_high;
  DWORD last_write_low;
  DWORD change_high;
  DWORD change_low;
  int complete;
} cupidbuild_host_windows_directory_record_t;

static int cupidbuild_host_windows_query_named_directory_record(
    HANDLE parent, const char *name,
    cupidbuild_host_windows_directory_record_t *record) {
  (void)memset(record, 0, sizeof(*record));
  return cupidbuild_host_windows_query_directory_request(
      parent, 1, name, record->name, sizeof(record->name),
      &record->attributes, &record->file_id_high, &record->file_id_low,
      &record->last_write_high, &record->last_write_low,
      &record->change_high, &record->change_low, &record->complete);
}

static int cupidbuild_host_windows_named_directory_record_valid(
    const cupidbuild_host_windows_directory_record_t *record,
    const char *name) {
  return record->complete == 0 && strcmp(record->name, name) == 0 &&
         (record->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
         (record->attributes &
          (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0u;
}

static int cupidbuild_host_windows_directory_record_equal(
    const cupidbuild_host_windows_directory_record_t *left,
    const cupidbuild_host_windows_directory_record_t *right) {
  return strcmp(left->name, right->name) == 0 &&
         left->attributes == right->attributes &&
         left->file_id_high == right->file_id_high &&
         left->file_id_low == right->file_id_low &&
         left->last_write_high == right->last_write_high &&
         left->last_write_low == right->last_write_low &&
         left->change_high == right->change_high &&
         left->change_low == right->change_low &&
         left->complete == right->complete;
}

static int cupidbuild_host_windows_directory_record_binding_equal(
    const cupidbuild_host_windows_directory_record_t *left,
    const cupidbuild_host_windows_directory_record_t *right) {
  return strcmp(left->name, right->name) == 0 &&
         left->attributes == right->attributes &&
         left->file_id_high == right->file_id_high &&
         left->file_id_low == right->file_id_low &&
         left->complete == right->complete;
}

static int cupidbuild_host_windows_query_directory(
    HANDLE directory, int restart, char *name, size_t name_capacity,
    DWORD *attributes_out, DWORD *file_id_high_out,
    DWORD *file_id_low_out, DWORD *last_write_high_out,
    DWORD *last_write_low_out, DWORD *change_high_out,
    DWORD *change_low_out, int *complete_out) {
  return cupidbuild_host_windows_query_directory_request(
      directory, restart, (const char *)0, name, name_capacity,
      attributes_out, file_id_high_out, file_id_low_out,
      last_write_high_out, last_write_low_out, change_high_out,
      change_low_out, complete_out);
}

#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_windows_named_directory_test_pause(
    cupidbuild_host_transaction_t *transaction, const char *logical) {
  const char *requested = getenv(
      "CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_LOGICAL");
  if (requested == (const char *)0 || strcmp(requested, logical) != 0) {
    return 1;
  }
  transaction->discovery_named_query_count++;
  if (transaction->discovery_named_query_count != 2u) {
    return 1;
  }
  return cupidbuild_host_profile_directory_test_pause(
      "CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_READY",
      "CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_RESUME");
}
#endif

static int cupidbuild_host_windows_named_directory_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *logical,
    HANDLE retained, cupidbuild_host_snapshot_t *snapshot,
    int *metadata_unsettled_out) {
  char parent_logical[CUPIDBUILD_HOST_PATH_BYTES];
  const char *name = logical;
  size_t index;
  size_t separator = (size_t)-1;
  HANDLE parent = INVALID_HANDLE_VALUE;
  int valid = 0;
  cupidbuild_host_windows_directory_record_t before;
  cupidbuild_host_windows_directory_record_t after;
  cupidbuild_host_snapshot_t opened;
  if (metadata_unsettled_out != (int *)0) {
    *metadata_unsettled_out = 0;
  }
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      logical == (const char *)0 || retained == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_path_is_relative_safe(logical)) {
    return 0;
  }
  for (index = 0u; logical[index] != '\0'; index++) {
    if (logical[index] == '/' || logical[index] == '\\') {
      separator = index;
      name = logical + index + 1u;
    }
  }
  if (name[0] == '\0') {
    return 0;
  }
  if (separator != (size_t)-1) {
    if (separator == 0u || separator + 1u > sizeof(parent_logical)) {
      return 0;
    }
    (void)memcpy(parent_logical, logical, separator);
    parent_logical[separator] = '\0';
    parent = cupidbuild_host_windows_open_relative_path(
        transaction->repository_root_handle, parent_logical, 1, 1);
    if (parent == INVALID_HANDLE_VALUE) {
      return 0;
    }
  } else {
    cupidbuild_host_snapshot_t parent_snapshot;
    parent = cupidbuild_host_windows_open_repository(
        transaction->repository_root);
    if (!cupidbuild_host_windows_directory_handle_snapshot(
            parent, &parent_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(
            &parent_snapshot, &transaction->repository_root_snapshot)) {
      if (parent != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(parent);
      }
      return 0;
    }
  }
  valid = cupidbuild_host_windows_query_named_directory_record(
              parent, name, &before) &&
          cupidbuild_host_windows_named_directory_record_valid(
              &before, name);
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  if (valid != 0 &&
      !cupidbuild_host_windows_named_directory_test_pause(
          transaction, logical)) {
    valid = 0;
  }
#endif
  if (valid != 0) {
    valid = cupidbuild_host_windows_directory_handle_snapshot(retained,
                                                               &opened) &&
            opened.identity[0] ==
                transaction->repository_root_snapshot.identity[0] &&
            opened.identity[1] == before.file_id_high &&
            opened.identity[2] == before.file_id_low &&
            cupidbuild_host_windows_query_named_directory_record(
                parent, name, &after) &&
            cupidbuild_host_windows_named_directory_record_valid(&after,
                                                                  name) &&
            cupidbuild_host_windows_directory_record_binding_equal(&before,
                                                                    &after);
    if (valid != 0 &&
        !cupidbuild_host_windows_directory_record_equal(&before, &after)) {
      valid = 0;
    } else if (valid != 0 &&
               (opened.modified[0] != before.last_write_high ||
                opened.modified[1] != before.last_write_low)) {
      if (metadata_unsettled_out != (int *)0) {
        *metadata_unsettled_out = 1;
      }
      valid = 0;
    }
  }
  if (!CloseHandle(parent)) {
    valid = 0;
  }
  if (!valid) {
    return 0;
  }
  opened.changed[0] = after.change_high;
  opened.changed[1] = after.change_low;
  *snapshot = opened;
  return 1;
}

static int cupidbuild_host_seed_members_repository(
    cupidbuild_host_transaction_t *transaction, const char *logical_directory,
    const char *suffix, const char *const *expected, size_t expected_count) {
  HANDLE directory;
  cupidbuild_host_snapshot_t root_snapshot;
  int restart = 1;
  int complete = 0;
  int valid = 1;
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          transaction->repository_root_handle, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  directory = cupidbuild_host_windows_open_relative_path(
      transaction->repository_root_handle, logical_directory, 1, 1);
  if (directory == INVALID_HANDLE_VALUE) {
    return 0;
  }
  while (valid != 0) {
    char name[260];
    DWORD attributes = 0u;
    DWORD file_id_high = 0u;
    DWORD file_id_low = 0u;
    DWORD last_write_high = 0u;
    DWORD last_write_low = 0u;
    DWORD change_high = 0u;
    DWORD change_low = 0u;
    if (!cupidbuild_host_windows_query_directory(
            directory, restart, name, sizeof(name), &attributes,
            &file_id_high, &file_id_low, &last_write_high,
            &last_write_low, &change_high, &change_low, &complete)) {
      valid = 0;
      break;
    }
    (void)attributes;
    (void)file_id_high;
    (void)file_id_low;
    (void)last_write_high;
    (void)last_write_low;
    (void)change_high;
    (void)change_low;
    restart = 0;
    if (complete != 0) {
      break;
    }
    if (cupidbuild_host_name_has_suffix(name, suffix) &&
        !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
      valid = 0;
    }
  }
  if (!CloseHandle(directory)) {
    valid = 0;
  }
  return valid;
}

static int cupidbuild_host_windows_rename_handle(
    HANDLE source, HANDLE destination_parent, const char *destination_name,
    int replace_if_exists) {
  cupidbuild_windows_rename_t *rename_information;
  cupidbuild_windows_io_status_t status;
  size_t name_size;
  size_t allocation_size;
  size_t index;
  long result;
  if (source == INVALID_HANDLE_VALUE ||
      destination_parent == INVALID_HANDLE_VALUE ||
      destination_name == (const char *)0) {
    return 0;
  }
  name_size = strlen(destination_name);
  if (name_size == 0u || name_size > 4096u) {
    return 0;
  }
  allocation_size = sizeof(*rename_information) - sizeof(unsigned short) +
                    name_size * sizeof(unsigned short);
  rename_information =
      (cupidbuild_windows_rename_t *)calloc(1u, allocation_size);
  if (rename_information == (cupidbuild_windows_rename_t *)0) {
    return 0;
  }
  rename_information->replace_if_exists =
      replace_if_exists != 0 ? 1u : 0u;
  rename_information->root_directory = destination_parent;
  rename_information->file_name_length = (DWORD)(name_size * 2u);
  for (index = 0u; index < name_size; index++) {
    unsigned char character = (unsigned char)destination_name[index];
    if (character >= 128u || character == '/' || character == '\\') {
      free(rename_information);
      return 0;
    }
    rename_information->file_name[index] = (unsigned short)character;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_set_information_file(
      source, &status, rename_information,
      (unsigned long)allocation_size, 10u);
  free(rename_information);
  return result >= 0;
}

static int cupidbuild_host_windows_restore_initial_output(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t restored;
  cupidbuild_host_snapshot_t retained;
  if (transaction->initial_output_parked == 0) {
    return transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
           transaction->initial_output_snapshot.present != 0;
  }
  if (transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
      transaction->initial_output_snapshot.present == 0 ||
      !cupidbuild_host_windows_rename_handle(
          transaction->initial_output_handle,
          transaction->output_parent_handle, transaction->output_name, 0)) {
    return 0;
  }
  transaction->initial_output_parked = 0;
  transaction->initial_output_backup_name[0] = '\0';
  return cupidbuild_host_windows_read_open_regular(
             transaction->initial_output_handle,
             CUPIDBUILD_HOST_FILE_LIMIT, &retained,
             (unsigned char **)0) &&
         cupidbuild_host_read_output(
             transaction, 0, &restored, (unsigned char **)0) &&
         cupidbuild_host_snapshot_equal(
             &retained, &transaction->initial_output_snapshot) &&
         cupidbuild_host_snapshot_equal(
             &restored, &transaction->initial_output_snapshot);
}

static int cupidbuild_host_windows_park_initial_output(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t parked;
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t missing;
  unsigned int attempt;
  if (transaction->initial_output_snapshot.present == 0) {
    return transaction->initial_output_handle == INVALID_HANDLE_VALUE;
  }
  if (transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
      transaction->initial_output_parked != 0) {
    return 0;
  }
  for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
    int written = snprintf(
        transaction->initial_output_backup_name,
        sizeof(transaction->initial_output_backup_name),
        ".cupidbuild-old-%08x-%08x", cupidbuild_host_process_id(), attempt);
    if (written < 0 ||
        (size_t)written >= sizeof(transaction->initial_output_backup_name)) {
      return 0;
    }
    if (cupidbuild_host_windows_rename_handle(
            transaction->initial_output_handle,
            transaction->output_parent_handle,
            transaction->initial_output_backup_name, 0)) {
      transaction->initial_output_parked = 1;
      break;
    }
  }
  if (transaction->initial_output_parked == 0) {
    transaction->initial_output_backup_name[0] = '\0';
    return 0;
  }
  return cupidbuild_host_windows_read_open_regular(
             transaction->initial_output_handle,
             CUPIDBUILD_HOST_FILE_LIMIT, &retained,
             (unsigned char **)0) &&
         cupidbuild_host_windows_read_relative_regular(
             transaction->output_parent_handle,
             transaction->initial_output_backup_name, 0,
             CUPIDBUILD_HOST_FILE_LIMIT, &parked,
             (unsigned char **)0) &&
         cupidbuild_host_read_output(
             transaction, 1, &missing, (unsigned char **)0) &&
         missing.present == 0 &&
         cupidbuild_host_snapshot_equal(
             &retained, &transaction->initial_output_snapshot) &&
         cupidbuild_host_snapshot_equal(
             &parked, &transaction->initial_output_snapshot);
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t published;
  cupidbuild_host_snapshot_t current_output;
  int candidate_at_output = 0;
  if (!cupidbuild_host_candidate_ready_for_publication(transaction) ||
      !cupidbuild_host_require_public_binding(
          transaction, &transaction->initial_output_snapshot, 1)
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
      || !cupidbuild_host_publication_test_pause("before-mutation")
#endif
      || (transaction->initial_output_snapshot.present != 0 &&
          !cupidbuild_host_windows_park_initial_output(transaction))) {
    if (transaction->initial_output_parked != 0) {
      if (!cupidbuild_host_windows_restore_initial_output(transaction)) {
        transaction->namespace_interfered = 1;
      }
    }
    return 0;
  }
  if (!cupidbuild_host_read_output(
          transaction, 1, &current_output, (unsigned char **)0) ||
      current_output.present != 0 ||
      !cupidbuild_host_windows_rename_handle(
          transaction->candidate_handle,
          transaction->output_parent_handle, transaction->output_name, 0)) {
    if (transaction->initial_output_parked != 0) {
      if (!cupidbuild_host_windows_restore_initial_output(transaction)) {
        transaction->namespace_interfered = 1;
      }
    }
    return 0;
  }
  candidate_at_output = 1;
  {
    int candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
        cupidbuild_host_publication_test_pause("after-install") &&
#endif
        cupidbuild_host_read_output(
                                 transaction, 0, &published,
                                 (unsigned char **)0) &&
                             cupidbuild_host_snapshot_equal(
                                 &published,
                                 &transaction->candidate_snapshot) &&
                             cupidbuild_host_published_candidate_matches(
                                 transaction);
    int public_binding = candidate_verified &&
                         cupidbuild_host_require_public_binding(
                             transaction,
                             &transaction->candidate_snapshot, 1);
    if (!public_binding) {
      cupidbuild_host_snapshot_t restored_candidate;
      int candidate_restored = 0;
      int output_restored = 0;
      if (cupidbuild_host_windows_rename_handle(
              transaction->candidate_handle, transaction->private_handle,
              "candidate.o", 0)) {
        candidate_at_output = 0;
        candidate_restored =
            cupidbuild_host_windows_read_relative_regular(
                transaction->private_handle, "candidate.o", 0,
                CUPIDBUILD_HOST_FILE_LIMIT, &restored_candidate,
                (unsigned char **)0) &&
            cupidbuild_host_snapshot_equal(
                &restored_candidate, &transaction->candidate_snapshot);
      }
      if (transaction->initial_output_parked != 0) {
        output_restored =
            cupidbuild_host_windows_restore_initial_output(transaction);
      } else {
        output_restored = 1;
      }
      if (!candidate_restored || !output_restored ||
          !cupidbuild_host_require_public_binding(
              transaction, &transaction->initial_output_snapshot, 0)) {
        transaction->namespace_interfered = 1;
      }
      if (candidate_at_output != 0) {
        transaction->candidate_published = 1;
      }
      return 0;
    }
  }
  transaction->candidate_publish_snapshot.present = 0;
  transaction->candidate_published = 1;
  transaction->publication_committed = 1;
  if (transaction->initial_output_parked != 0) {
    if (!cupidbuild_host_windows_dispose_retained_at(
            transaction->output_parent_handle,
            transaction->initial_output_backup_name,
            &transaction->initial_output_snapshot,
            &transaction->initial_output_handle)) {
      return 1;
    }
    transaction->initial_output_parked = 0;
    transaction->initial_output_backup_name[0] = '\0';
  }
  return 1;
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
#define CUPIDBUILD_LINUX_SYS_CHDIR 12
#define CUPIDBUILD_LINUX_SYS_CHMOD 15
#define CUPIDBUILD_LINUX_SYS_GETPID 20
#define CUPIDBUILD_LINUX_SYS_GETCWD 183
#define CUPIDBUILD_LINUX_SYS_KILL 37
#define CUPIDBUILD_LINUX_SYS_RENAME 38
#define CUPIDBUILD_LINUX_SYS_MKDIR 39
#define CUPIDBUILD_LINUX_SYS_RMDIR 40
#define CUPIDBUILD_LINUX_SYS_PIPE 42
#define CUPIDBUILD_LINUX_SYS_FCHMOD 94
#define CUPIDBUILD_LINUX_SYS_FSYNC 118
#define CUPIDBUILD_LINUX_SYS_FCHDIR 133
#define CUPIDBUILD_LINUX_SYS_NANOSLEEP 162
#define CUPIDBUILD_LINUX_SYS_PREAD64 180
#define CUPIDBUILD_LINUX_SYS_LSTAT64 196
#define CUPIDBUILD_LINUX_SYS_FSTAT64 197
#define CUPIDBUILD_LINUX_SYS_GETDENTS64 220
#define CUPIDBUILD_LINUX_SYS_FCNTL64 221
#define CUPIDBUILD_LINUX_SYS_OPENAT 295
#define CUPIDBUILD_LINUX_SYS_MKDIRAT 296
#define CUPIDBUILD_LINUX_SYS_FSTATAT64 300
#define CUPIDBUILD_LINUX_SYS_UNLINKAT 301
#define CUPIDBUILD_LINUX_SYS_RENAMEAT 302
#define CUPIDBUILD_LINUX_SYS_LINKAT 303
#define CUPIDBUILD_LINUX_SYS_RENAMEAT2 353
#define CUPIDBUILD_LINUX_SYS_MEMFD_CREATE 356
#define CUPIDBUILD_LINUX_SYS_EXECVEAT 358
#define CUPIDBUILD_LINUX_O_WRONLY 1u
#define CUPIDBUILD_LINUX_O_RDWR 2u
#define CUPIDBUILD_LINUX_O_CREAT 64u
#define CUPIDBUILD_LINUX_O_EXCL 128u
#define CUPIDBUILD_LINUX_O_NONBLOCK 2048u
#define CUPIDBUILD_LINUX_O_DIRECTORY 65536u
#define CUPIDBUILD_LINUX_O_NOFOLLOW 131072u
#define CUPIDBUILD_LINUX_O_CLOEXEC 524288u
#define CUPIDBUILD_LINUX_WNOHANG 1u
#define CUPIDBUILD_LINUX_AT_FDCWD (-100)
#define CUPIDBUILD_LINUX_AT_REMOVEDIR 512u
#define CUPIDBUILD_LINUX_AT_SYMLINK_FOLLOW 1024u
#define CUPIDBUILD_LINUX_RENAME_NOREPLACE 1u
#define CUPIDBUILD_LINUX_RENAME_EXCHANGE 2u
#define CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW 256u
#define CUPIDBUILD_LINUX_AT_EMPTY_PATH 4096u
#define CUPIDBUILD_LINUX_SIGKILL 9u
#define CUPIDBUILD_LINUX_EINTR 4
#define CUPIDBUILD_LINUX_EBUSY 16
#define CUPIDBUILD_LINUX_MFD_CLOEXEC 1u
#define CUPIDBUILD_LINUX_MFD_ALLOW_SEALING 2u
#define CUPIDBUILD_LINUX_F_SETFD 2u
#define CUPIDBUILD_LINUX_F_DUPFD 0u
#define CUPIDBUILD_LINUX_FD_CLOEXEC 1u
#define CUPIDBUILD_LINUX_F_ADD_SEALS 1033u
#define CUPIDBUILD_LINUX_F_SEAL_ALL 15u
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

static int cupidbuild_host_path_missing(const char *path) {
  unsigned char information[96];
  return cupidbuild_linux_stat(path, information) ==
         -CUPIDBUILD_LINUX_ENOENT;
}

static unsigned int cupidbuild_linux_mode(const unsigned char stat_bytes[96]) {
  return cupidbuild_linux_u32(stat_bytes + 16u);
}

static int cupidbuild_linux_descriptor_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  unsigned char information[96];
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0 ||
      cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFDIR) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
  snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
  return 1;
}

static int cupidbuild_linux_information_identity_equal(
    const unsigned char left[96], const unsigned char right[96]) {
  return cupidbuild_linux_u32(left) == cupidbuild_linux_u32(right) &&
         cupidbuild_linux_u32(left + 4u) ==
             cupidbuild_linux_u32(right + 4u) &&
         cupidbuild_linux_u32(left + 88u) ==
             cupidbuild_linux_u32(right + 88u) &&
         cupidbuild_linux_u32(left + 92u) ==
             cupidbuild_linux_u32(right + 92u);
}

static int cupidbuild_linux_open_relative(
    int parent, const char *name, int directory) {
  unsigned int flags = CUPIDBUILD_LINUX_O_NOFOLLOW |
                       CUPIDBUILD_LINUX_O_CLOEXEC;
  if (directory != 0) {
    flags |= CUPIDBUILD_LINUX_O_DIRECTORY;
  } else {
    flags |= CUPIDBUILD_LINUX_O_NONBLOCK;
  }
  return cupid_linux_syscall4(
      CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)parent,
      (unsigned int)name, flags, 0u);
}

static int cupidbuild_linux_open_relative_path(
    int root, const char *logical, int directory) {
  int current = root;
  int next;
  char component[260];
  size_t start = 0u;
  size_t size = strlen(logical);
  size_t index;
  int owns_current = 0;
  if (root < 0 || size == 0u) {
    return -1;
  }
  for (index = 0u; index <= size; index++) {
    if (logical[index] == '/' || logical[index] == '\0') {
      size_t component_size = index - start;
      int last = index == size;
      if (component_size == 0u || component_size >= sizeof(component)) {
        if (owns_current != 0) {
          (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                     (unsigned int)current);
        }
        return -1;
      }
      (void)memcpy(component, logical + start, component_size);
      component[component_size] = '\0';
      next = cupidbuild_linux_open_relative(
          current, component, last != 0 ? directory : 1);
      if (owns_current != 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                   (unsigned int)current);
      }
      if (next < 0) {
        return -1;
      }
      current = next;
      owns_current = 1;
      start = index + 1u;
    }
  }
  return current;
}

static int cupidbuild_linux_open_repository(const char *path) {
  const char *anchor;
  int current;
  int next;
  char component[260];
  size_t start;
  size_t size;
  size_t index;
  if (path == (const char *)0 || path[0] == '\0') {
    return -1;
  }
  anchor = path[0] == '/' ? "/" : ".";
  start = path[0] == '/' ? 1u : 0u;
  size = strlen(path);
  current = cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)anchor,
      CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW |
          CUPIDBUILD_LINUX_O_CLOEXEC,
      0u);
  if (current < 0 || (strcmp(path, "/") == 0) ||
      (strcmp(path, ".") == 0)) {
    return current;
  }
  for (index = start; index <= size; index++) {
    if (path[index] == '/' || path[index] == '\0') {
      size_t component_size = index - start;
      if (component_size == 0u || component_size >= sizeof(component)) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                   (unsigned int)current);
        return -1;
      }
      (void)memcpy(component, path + start, component_size);
      component[component_size] = '\0';
      if ((component_size == 1u && component[0] == '.') ||
          (component_size == 2u && component[0] == '.' &&
           component[1] == '.')) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                   (unsigned int)current);
        return -1;
      }
      next = cupidbuild_linux_open_relative(current, component, 1);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)current);
      if (next < 0) {
        return -1;
      }
      current = next;
      start = index + 1u;
    }
  }
  return current;
}

static int cupidbuild_linux_read_repository_regular(
    int repository, const char *logical, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  unsigned char information[96];
  int descriptor = cupidbuild_linux_open_relative_path(
      repository, logical, 0);
  unsigned int size;
  unsigned char *bytes;
  unsigned int offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
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
  if ((size_t)size > limit) {
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
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                           (unsigned int)descriptor) < 0) {
    free(bytes);
    return 0;
  }
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

static int cupidbuild_linux_read_relative_regular(
    int parent, const char *name, int optional, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  unsigned char information[96];
  int descriptor = cupidbuild_linux_open_relative(parent, name, 0);
  unsigned int size;
  unsigned char *bytes;
  unsigned int offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0) {
    if (optional != 0 && descriptor == -CUPIDBUILD_LINUX_ENOENT) {
      if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
      }
      return 1;
    }
    return 0;
  }
  if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFREG ||
      cupidbuild_linux_u32(information + 48u) != 0u) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
  size = cupidbuild_linux_u32(information + 44u);
  if ((size_t)size > limit) {
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
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                           (unsigned int)descriptor) < 0) {
    free(bytes);
    return 0;
  }
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
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
  snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
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

static int cupidbuild_host_discover_platform(
    cupidbuild_host_transaction_t *transaction, const char *logical_root,
    const char *const *suffixes, size_t suffix_count, int skip_hidden_files,
    int reject_matching_nonfiles, cupidbuild_host_path_list_t *paths) {
  cupidbuild_host_discovery_worklist_t worklist;
  cupidbuild_host_snapshot_t *visited;
  cupidbuild_host_snapshot_t repository_snapshot;
  cupidbuild_host_snapshot_t root_snapshot;
  int repository = transaction->repository_root_descriptor;
  int root = -1;
  size_t visited_count = 0u;
  size_t directory_count = 0u;
  int valid = 1;
  (void)memset(&worklist, 0, sizeof(worklist));
  visited = (cupidbuild_host_snapshot_t *)calloc(
      CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES, sizeof(*visited));
  if (visited == (cupidbuild_host_snapshot_t *)0) {
    free(visited);
    return 0;
  }
  if (!cupidbuild_linux_descriptor_snapshot(repository,
                                             &repository_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &repository_snapshot)) {
    valid = 0;
  } else {
    root = cupidbuild_linux_open_relative_path(repository, logical_root, 1);
    if (!cupidbuild_linux_descriptor_snapshot(root, &root_snapshot) ||
        cupidbuild_host_snapshot_identity_equal(&repository_snapshot,
                                                &root_snapshot) ||
        !cupidbuild_host_discovery_worklist_add(
            &worklist, logical_root, &root_snapshot, root)) {
      valid = 0;
    } else {
      visited[visited_count++] = root_snapshot;
      root = -1;
      directory_count = 1u;
    }
  }
  if (root >= 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)root);
  }
  while (valid != 0 && worklist.count != 0u) {
    unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
    cupidbuild_host_discovery_directory_t current =
        worklist.directories[worklist.count - 1u];
    cupidbuild_host_snapshot_t completed_snapshot;
    int retained = 0;
    worklist.count--;
    while (valid != 0) {
      int count = cupid_linux_syscall3(
          CUPIDBUILD_LINUX_SYS_GETDENTS64, (unsigned int)current.descriptor,
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
        char logical[CUPIDBUILD_HOST_PATH_BYTES];
        unsigned char information[96];
        unsigned int mode;
        int matches;
        if ((size_t)count - offset < 20u) {
          valid = 0;
          break;
        }
        record_size = cupidbuild_linux_u16(entries + offset + 16u);
        if (record_size < 20u ||
            (size_t)record_size > (size_t)count - offset) {
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
        offset += (size_t)record_size;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
          continue;
        }
        if (!cupidbuild_host_join(logical, sizeof(logical), current.logical,
                                  name) ||
            cupid_linux_syscall4(
                CUPIDBUILD_LINUX_SYS_FSTATAT64,
                (unsigned int)current.descriptor, (unsigned int)name,
                (unsigned int)information,
                CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) < 0) {
          valid = 0;
          break;
        }
        mode = cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT;
        if (name[0] == '.' &&
            (mode == CUPIDBUILD_LINUX_S_IFDIR || skip_hidden_files != 0)) {
          continue;
        }
        if (mode == CUPIDBUILD_LINUX_S_IFLNK) {
          valid = 0;
          break;
        }
        matches = cupidbuild_host_discovery_suffix(name, suffixes,
                                                    suffix_count);
        if (mode == CUPIDBUILD_LINUX_S_IFDIR && matches != 0 &&
            reject_matching_nonfiles != 0) {
          valid = 0;
          break;
        }
        if (mode == CUPIDBUILD_LINUX_S_IFDIR) {
          unsigned char opened_information[96];
          cupidbuild_host_snapshot_t child_snapshot;
          int child = cupidbuild_linux_open_relative(
              current.descriptor, name, 1);
          size_t prior;
          int alias = 0;
          if (child < 0 ||
              cupid_linux_syscall2(
                  CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)child,
                  (unsigned int)opened_information) < 0 ||
              !cupidbuild_linux_information_identity_equal(
                  information, opened_information) ||
              !cupidbuild_linux_descriptor_snapshot(child,
                                                     &child_snapshot)) {
            if (child >= 0) {
              (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                         (unsigned int)child);
            }
            valid = 0;
            break;
          }
          for (prior = 0u; prior < visited_count; prior++) {
            if (cupidbuild_host_snapshot_identity_equal(
                    &visited[prior], &child_snapshot)) {
              alias = 1;
              break;
            }
          }
          if (alias != 0 ||
              directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
              !cupidbuild_host_discovery_worklist_add(
                  &worklist, logical, &child_snapshot, child)) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                       (unsigned int)child);
            valid = 0;
            break;
          }
          visited[visited_count++] = child_snapshot;
          directory_count++;
        } else if (mode == CUPIDBUILD_LINUX_S_IFREG && matches != 0) {
          unsigned char opened_information[96];
          cupidbuild_host_snapshot_t child_snapshot;
          int child = cupidbuild_linux_open_relative(
              current.descriptor, name, 0);
          if (child < 0 ||
              cupid_linux_syscall2(
                  CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)child,
                  (unsigned int)opened_information) < 0 ||
              (cupidbuild_linux_mode(opened_information) &
               CUPIDBUILD_LINUX_S_IFMT) != CUPIDBUILD_LINUX_S_IFREG ||
              !cupidbuild_linux_information_identity_equal(
                  information, opened_information) ||
              !cupidbuild_host_read_open_file(
                  child, CUPIDBUILD_HOST_FILE_LIMIT, &child_snapshot,
                  (unsigned char **)0) ||
              !cupidbuild_host_discovery_add(
                  paths, logical, &child_snapshot)) {
            if (child >= 0) {
              (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                         (unsigned int)child);
            }
            valid = 0;
            break;
          }
          if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                   (unsigned int)child) < 0) {
            valid = 0;
            break;
          }
        } else if (matches != 0 && reject_matching_nonfiles != 0) {
          valid = 0;
          break;
        }
      }
    }
    if (valid != 0 &&
        (!cupidbuild_linux_descriptor_snapshot(
             current.descriptor, &completed_snapshot) ||
         !cupidbuild_host_snapshot_equal(&current.snapshot,
                                         &completed_snapshot))) {
      valid = 0;
    }
    if (valid != 0) {
      current.snapshot = completed_snapshot;
      if (!cupidbuild_host_bind_discovery_directory(
              transaction, &current, &retained)) {
        valid = 0;
      }
    }
    if (retained != 0) {
      current.descriptor = -1;
      current.logical = (char *)0;
    }
    if (current.descriptor >= 0 &&
        cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)current.descriptor) < 0) {
      valid = 0;
    }
    free(current.logical);
  }
  while (worklist.count != 0u) {
    cupidbuild_host_discovery_directory_t *directory =
        &worklist.directories[--worklist.count];
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)directory->descriptor);
    free(directory->logical);
  }
  free(worklist.directories);
  free(visited);
  return valid;
}

static int cupidbuild_host_seed_members_repository(
    cupidbuild_host_transaction_t *transaction, const char *logical_directory,
    const char *suffix, const char *const *expected, size_t expected_count) {
  unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
  cupidbuild_host_snapshot_t root_snapshot;
  int descriptor;
  int valid;
  if (!cupidbuild_linux_descriptor_snapshot(
          transaction->repository_root_descriptor, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  descriptor = cupidbuild_linux_open_relative_path(
      transaction->repository_root_descriptor, logical_directory, 1);
  valid = descriptor >= 0;
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
      if (record_size < 20u ||
          (size_t)record_size > (size_t)count - offset) {
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

static int cupidbuild_host_write_anonymous(
    const char *name, const unsigned char *bytes, size_t size,
    char *path, size_t path_capacity, cupidbuild_host_snapshot_t *snapshot,
    int *descriptor_out) {
  unsigned char information[96];
  size_t offset = 0u;
  int descriptor = cupid_linux_syscall2(
      CUPIDBUILD_LINUX_SYS_MEMFD_CREATE, (unsigned int)name,
      CUPIDBUILD_LINUX_MFD_CLOEXEC |
          CUPIDBUILD_LINUX_MFD_ALLOW_SEALING);
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
      cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                           (unsigned int)descriptor,
                           CUPIDBUILD_LINUX_F_ADD_SEALS,
                           CUPIDBUILD_LINUX_F_SEAL_ALL) < 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
  if (!cupidbuild_host_copy_text(path, path_capacity, name)) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)descriptor);
    return 0;
  }
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
  *descriptor_out = descriptor;
  return 1;
}

static int cupidbuild_host_open_anonymous(const char *name, char *path,
                                          size_t path_capacity,
                                          int *descriptor_out) {
  int descriptor = cupid_linux_syscall2(
      CUPIDBUILD_LINUX_SYS_MEMFD_CREATE, (unsigned int)name,
      CUPIDBUILD_LINUX_MFD_CLOEXEC |
          CUPIDBUILD_LINUX_MFD_ALLOW_SEALING);
  if (descriptor < 0 ||
      !cupidbuild_host_copy_text(path, path_capacity, name)) {
    if (descriptor >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)descriptor);
    }
    return 0;
  }
  *descriptor_out = descriptor;
  return 1;
}

static int cupidbuild_host_seal_anonymous(int descriptor) {
  return cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                              (unsigned int)descriptor,
                              CUPIDBUILD_LINUX_F_ADD_SEALS,
                              CUPIDBUILD_LINUX_F_SEAL_ALL) == 0;
}

static int cupidbuild_host_absolute_directory(char *destination,
                                              size_t capacity,
                                              const char *path) {
  char current[CUPIDBUILD_HOST_PATH_BYTES];
  if (path == (const char *)0 || path[0] == '\0') {
    return 0;
  }
  if (path[0] == '/') {
    return cupidbuild_host_copy_text(destination, capacity, path);
  }
  if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_GETCWD,
                           (unsigned int)current,
                           (unsigned int)sizeof(current)) < 0) {
    return 0;
  }
  return cupidbuild_host_join(destination, capacity, current, path);
}

static int cupidbuild_host_make_executable(const char *path) {
  return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_CHMOD,
                              (unsigned int)path, 0700u) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_UNLINK,
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

static int cupidbuild_host_linux_open_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  unsigned char information[96];
  if (snapshot == (cupidbuild_host_snapshot_t *)0) {
    return 1;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFREG ||
      cupidbuild_linux_u32(information + 48u) != 0u) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->size = (size_t)cupidbuild_linux_u32(information + 44u);
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  return 1;
}

static int cupidbuild_host_read_open_file(
    int descriptor, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  cupidbuild_host_snapshot_t current;
  unsigned char *bytes;
  size_t offset = 0u;
  if (!cupidbuild_host_linux_open_snapshot(descriptor, &current) ||
      current.size > limit) {
    return 0;
  }
  bytes = (unsigned char *)malloc(current.size + 1u);
  if (bytes == (unsigned char *)0) {
    return 0;
  }
  while (offset < current.size) {
    int count = cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_PREAD64, (unsigned int)descriptor,
        (unsigned int)(bytes + offset),
        (unsigned int)(current.size - offset), (unsigned int)offset, 0u);
    if (count <= 0) {
      free(bytes);
      return 0;
    }
    offset += (size_t)count;
  }
  bytes[current.size] = 0u;
  cupidbuild_sha256(bytes, current.size, current.sha256);
  if (snapshot != (cupidbuild_host_snapshot_t *)0) {
    *snapshot = current;
  }
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static void cupidbuild_host_write_launch_failure(int descriptor) {
  unsigned char marker = 1u;
  int result;
  do {
    result = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
        (unsigned int)&marker, 1u);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
}

static int cupidbuild_host_read_launch_status(int descriptor) {
  unsigned char marker = 0u;
  int result;
  do {
    result = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_READ, (unsigned int)descriptor,
        (unsigned int)&marker, 1u);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
  if (result == 0) {
    return 0;
  }
  if (result == 1 && marker == 1u) {
    return 1;
  }
  return -1;
}

static int cupidbuild_host_linux_duplicate_above_standard(int descriptor) {
  int result;
  if (descriptor > 2) {
    return descriptor;
  }
  do {
    result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                                  (unsigned int)descriptor,
                                  CUPIDBUILD_LINUX_F_DUPFD, 3u);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
  return result;
}

static int cupidbuild_host_linux_promote_above_standard(int *descriptor) {
  int original;
  int duplicated;
  if (descriptor == (int *)0 || *descriptor < 0) {
    return 0;
  }
  if (*descriptor > 2) {
    return 1;
  }
  original = *descriptor;
  duplicated = cupidbuild_host_linux_duplicate_above_standard(original);
  if (duplicated <= 2) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)original);
    *descriptor = -1;
    return 0;
  }
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)original);
  *descriptor = duplicated;
  return 1;
}

static int cupidbuild_host_linux_duplicate_stream(int source,
                                                  unsigned int target) {
  int result;
  if (source == (int)target) {
    return 1;
  }
  do {
    result = cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_DUP2,
                                  (unsigned int)source, target);
  } while (result == -CUPIDBUILD_LINUX_EINTR ||
           result == -CUPIDBUILD_LINUX_EBUSY);
  return result == (int)target;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const cupidbuild_host_snapshot_t *expected_tool,
                                       int tool_descriptor,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       int stdout_descriptor,
                                       int stderr_descriptor,
                                       const char *working_directory,
                                       int working_descriptor,
                                       const cupidbuild_host_transaction_t *
                                           inherited_transaction,
                                       cupidbuild_host_snapshot_t *stdout_opened,
                                       cupidbuild_host_snapshot_t *stderr_opened,
                                       unsigned int timeout_milliseconds) {
  const char **argv;
  size_t argument_count = 0u;
  size_t index;
  int child;
  int own_stream_descriptors = stdout_descriptor < 0;
  int own_tool_descriptor = tool_descriptor < 0;
  int launch_pipe[2] = {-1, -1};
  unsigned int elapsed = 0u;
  int status = 0;
  int wait_result;
  cupidbuild_linux_time_t pause_time;
  if (own_stream_descriptors == 0 &&
      (stdout_descriptor <= 2 || stderr_descriptor <= 2 ||
       stdout_descriptor == stderr_descriptor)) {
    return -1;
  }
  while (arguments[argument_count] != (const char *)0) {
    if (argument_count == (size_t)-2) {
      return -1;
    }
    argument_count++;
  }
  argv = (const char **)calloc(argument_count + 2u, sizeof(*argv));
  if (argv == (const char **)0) {
    return -1;
  }
  argv[0] = tool;
  for (index = 0u; index < argument_count; index++) {
    argv[index + 1u] = arguments[index];
  }
  if (own_tool_descriptor != 0) {
    tool_descriptor = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)tool,
        CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
  }
  {
    cupidbuild_host_snapshot_t tool_snapshot;
    int valid = tool_descriptor >= 0 &&
                cupidbuild_host_read_open_file(
                    tool_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
                    &tool_snapshot, (unsigned char **)0) &&
                cupidbuild_host_snapshot_equal(&tool_snapshot,
                                                expected_tool);
    if (valid == 0) {
      if (tool_descriptor >= 0 && own_tool_descriptor != 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                   (unsigned int)tool_descriptor);
      }
      free(argv);
      return -1;
    }
  }
  if (own_stream_descriptors != 0) {
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
  }
  if (stdout_descriptor < 0 || stderr_descriptor < 0 ||
      !cupidbuild_host_linux_open_snapshot(stdout_descriptor,
                                            stdout_opened) ||
      !cupidbuild_host_linux_open_snapshot(stderr_descriptor,
                                            stderr_opened)) {
    if (stdout_descriptor >= 0 && own_stream_descriptors != 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stdout_descriptor);
    }
    if (stderr_descriptor >= 0 && own_stream_descriptors != 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)tool_descriptor);
    }
    free(argv);
    return -1;
  }
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_PIPE,
                           (unsigned int)launch_pipe) != 0 ||
      !cupidbuild_host_linux_promote_above_standard(&launch_pipe[0]) ||
      !cupidbuild_host_linux_promote_above_standard(&launch_pipe[1]) ||
      cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                           (unsigned int)launch_pipe[1],
                           CUPIDBUILD_LINUX_F_SETFD,
                           CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
    if (launch_pipe[0] >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)launch_pipe[0]);
    }
    if (launch_pipe[1] >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)launch_pipe[1]);
    }
    if (own_stream_descriptors != 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stdout_descriptor);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)tool_descriptor);
    }
    free(argv);
    return -1;
  }
  child = cupid_linux_syscall0(CUPIDBUILD_LINUX_SYS_FORK);
  if (child == 0) {
    int exec_tool_descriptor;
    unsigned int inherited_index;
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)launch_pipe[0]);
    if ((inherited_transaction !=
             (const cupidbuild_host_transaction_t *)0 &&
         inherited_transaction->private_flat != 0 &&
         ((inherited_transaction->candidate_descriptor >= 0 &&
           cupid_linux_syscall3(
               CUPIDBUILD_LINUX_SYS_FCNTL64,
               (unsigned int)inherited_transaction->candidate_descriptor,
               CUPIDBUILD_LINUX_F_SETFD, 0u) != 0) ||
          (inherited_transaction->private_output_descriptor >= 0 &&
           cupid_linux_syscall3(
               CUPIDBUILD_LINUX_SYS_FCNTL64,
               (unsigned int)inherited_transaction
                   ->private_output_descriptor,
               CUPIDBUILD_LINUX_F_SETFD, 0u) != 0))) ||
        (working_descriptor >= 0 &&
         cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FCHDIR,
                              (unsigned int)working_descriptor) != 0) ||
        (working_descriptor < 0 && working_directory != (const char *)0 &&
         cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CHDIR,
                              (unsigned int)working_directory) != 0)) {
      cupidbuild_host_write_launch_failure(launch_pipe[1]);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
      return 125;
    }
    if (inherited_transaction !=
            (const cupidbuild_host_transaction_t *)0 &&
        inherited_transaction->private_flat != 0) {
      for (inherited_index = 0u;
           inherited_index < inherited_transaction->input_count;
           inherited_index++) {
        int descriptor = inherited_transaction
                             ->inputs[inherited_index]
                             .frozen_descriptor;
        if (descriptor >= 0 &&
            cupid_linux_syscall3(
                CUPIDBUILD_LINUX_SYS_FCNTL64,
                (unsigned int)descriptor,
                CUPIDBUILD_LINUX_F_SETFD, 0u) != 0) {
          cupidbuild_host_write_launch_failure(launch_pipe[1]);
          (void)cupid_linux_syscall1(
              CUPIDBUILD_LINUX_SYS_EXIT, 125u);
          return 125;
        }
      }
    }
    exec_tool_descriptor =
        cupidbuild_host_linux_duplicate_above_standard(tool_descriptor);
    if (exec_tool_descriptor < 0 ||
        !cupidbuild_host_linux_duplicate_stream(stdout_descriptor, 1u) ||
        !cupidbuild_host_linux_duplicate_stream(stderr_descriptor, 2u)) {
      cupidbuild_host_write_launch_failure(launch_pipe[1]);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
      return 125;
    }
    if (stdout_descriptor > 2) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stdout_descriptor);
    }
    if (stderr_descriptor > 2 && stderr_descriptor != stdout_descriptor) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)stderr_descriptor);
    }
    (void)cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_EXECVEAT, (unsigned int)exec_tool_descriptor,
        (unsigned int)"", (unsigned int)argv, 0u,
        CUPIDBUILD_LINUX_AT_EMPTY_PATH);
    cupidbuild_host_write_launch_failure(launch_pipe[1]);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
    return 125;
  }
  if (own_stream_descriptors != 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)stdout_descriptor);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)stderr_descriptor);
  }
  if (own_tool_descriptor != 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)tool_descriptor);
  }
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)launch_pipe[1]);
  if (child < 0) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                               (unsigned int)launch_pipe[0]);
    free(argv);
    return -1;
  }
  free(argv);
  pause_time.seconds = 0;
  pause_time.nanoseconds = 10000000;
  for (;;) {
    wait_result = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_WAITPID, (unsigned int)child,
        (unsigned int)&status, CUPIDBUILD_LINUX_WNOHANG);
    if (wait_result > 0) {
      break;
    }
    if (wait_result == -CUPIDBUILD_LINUX_EINTR) {
      continue;
    }
    if (wait_result < 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)launch_pipe[0]);
      return -1;
    }
    if (elapsed >= timeout_milliseconds) {
      (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_KILL,
                                 (unsigned int)child,
                                 CUPIDBUILD_LINUX_SIGKILL);
      do {
        wait_result = cupid_linux_syscall3(
            CUPIDBUILD_LINUX_SYS_WAITPID, (unsigned int)child,
            (unsigned int)&status, 0u);
      } while (wait_result == -CUPIDBUILD_LINUX_EINTR);
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)launch_pipe[0]);
      return -2;
    }
    (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_NANOSLEEP,
                               (unsigned int)&pause_time, 0u);
    elapsed += 10u;
  }
  wait_result = cupidbuild_host_read_launch_status(launch_pipe[0]);
  (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)launch_pipe[0]);
  if (wait_result != 0) {
    return -1;
  }
  if ((status & 0x7f) != 0) {
    return 125;
  }
  return (status >> 8) & 0xff;
}

static int cupidbuild_host_open_directory(const char *path) {
  return cupid_linux_syscall3(
      CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
      CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW |
          CUPIDBUILD_LINUX_O_CLOEXEC,
      0u);
}

static int cupidbuild_host_directory_descriptor_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  unsigned char information[96];
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0 ||
      cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64,
                           (unsigned int)descriptor,
                           (unsigned int)information) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFDIR) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
  snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
  return 1;
}

static int cupidbuild_host_close_directory(int descriptor) {
  return descriptor < 0 ||
         cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                              (unsigned int)descriptor) == 0;
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  return cupidbuild_host_posix_atomic_replace(transaction);
}
#else
#if defined(CUPIDBUILD_HOST_NATIVE_EINTR_TEST)
#define CUPIDBUILD_HOST_EINTR_READ 1u
#define CUPIDBUILD_HOST_EINTR_WRITE 2u
#define CUPIDBUILD_HOST_EINTR_WAIT_NOHANG 3u
#define CUPIDBUILD_HOST_EINTR_WAIT_BLOCKING 4u

void cupidbuild_host_native_eintr_test_arm(unsigned int operation);
int cupidbuild_host_native_eintr_test_retry_observed(
    unsigned int operation);

typedef struct {
  volatile unsigned int operation;
  volatile int injected;
  volatile int retried;
} cupidbuild_host_native_eintr_state_t;

static cupidbuild_host_native_eintr_state_t
    *cupidbuild_host_native_eintr_state;

void cupidbuild_host_native_eintr_test_arm(unsigned int operation) {
  if (cupidbuild_host_native_eintr_state ==
      (cupidbuild_host_native_eintr_state_t *)0) {
    void *mapping = mmap((void *)0,
                         sizeof(*cupidbuild_host_native_eintr_state),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mapping != MAP_FAILED) {
      cupidbuild_host_native_eintr_state =
          (cupidbuild_host_native_eintr_state_t *)mapping;
    }
  }
  if (cupidbuild_host_native_eintr_state !=
      (cupidbuild_host_native_eintr_state_t *)0) {
    cupidbuild_host_native_eintr_state->operation = operation;
    cupidbuild_host_native_eintr_state->injected = 0;
    cupidbuild_host_native_eintr_state->retried = 0;
  }
}

int cupidbuild_host_native_eintr_test_retry_observed(
    unsigned int operation) {
  return cupidbuild_host_native_eintr_state !=
             (cupidbuild_host_native_eintr_state_t *)0 &&
         cupidbuild_host_native_eintr_state->operation == operation &&
         cupidbuild_host_native_eintr_state->injected != 0 &&
         cupidbuild_host_native_eintr_state->retried != 0;
}

static int cupidbuild_host_native_eintr_test_inject(
    unsigned int operation) {
  if (cupidbuild_host_native_eintr_state ==
          (cupidbuild_host_native_eintr_state_t *)0 ||
      cupidbuild_host_native_eintr_state->operation != operation) {
    return 0;
  }
  if (cupidbuild_host_native_eintr_state->injected == 0) {
    cupidbuild_host_native_eintr_state->injected = 1;
    errno = EINTR;
    return 1;
  }
  cupidbuild_host_native_eintr_state->retried = 1;
  return 0;
}

static ssize_t cupidbuild_host_native_eintr_read(
    int descriptor, void *bytes, size_t size) {
  if (cupidbuild_host_native_eintr_test_inject(
          CUPIDBUILD_HOST_EINTR_READ)) {
    return -1;
  }
  return read(descriptor, bytes, size);
}

static ssize_t cupidbuild_host_native_eintr_write(
    int descriptor, const void *bytes, size_t size) {
  if (cupidbuild_host_native_eintr_test_inject(
          CUPIDBUILD_HOST_EINTR_WRITE)) {
    return -1;
  }
  return write(descriptor, bytes, size);
}

static pid_t cupidbuild_host_native_eintr_waitpid(
    pid_t process, int *status, int options) {
  unsigned int operation = (options & WNOHANG) != 0
                               ? CUPIDBUILD_HOST_EINTR_WAIT_NOHANG
                               : CUPIDBUILD_HOST_EINTR_WAIT_BLOCKING;
  if (cupidbuild_host_native_eintr_test_inject(operation)) {
    return (pid_t)-1;
  }
  return waitpid(process, status, options);
}

#define CUPIDBUILD_HOST_LAUNCH_READ cupidbuild_host_native_eintr_read
#define CUPIDBUILD_HOST_LAUNCH_WRITE cupidbuild_host_native_eintr_write
#define CUPIDBUILD_HOST_LAUNCH_WAITPID cupidbuild_host_native_eintr_waitpid
#else
#define CUPIDBUILD_HOST_LAUNCH_READ read
#define CUPIDBUILD_HOST_LAUNCH_WRITE write
#define CUPIDBUILD_HOST_LAUNCH_WAITPID waitpid
#endif

static int cupidbuild_host_path_missing(const char *path) {
  struct stat information;
  return lstat(path, &information) != 0 && errno == ENOENT;
}

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
  snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
  snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
  snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
  snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
  return 1;
}

static int cupidbuild_native_directory_descriptor_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  struct stat information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
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
  snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
  snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
  snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
  snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
  return 1;
}

static int cupidbuild_native_information_identity_equal(
    const struct stat *left, const struct stat *right) {
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int cupidbuild_native_open_relative(int parent, const char *name,
                                           int directory) {
  int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
  if (directory != 0) {
    flags |= O_DIRECTORY;
  } else {
    flags |= O_NONBLOCK;
  }
  return openat(parent, name, flags);
}

static int cupidbuild_native_open_relative_path(
    int root, const char *logical, int directory) {
  int current = root;
  int next;
  char component[260];
  size_t start = 0u;
  size_t size = strlen(logical);
  size_t index;
  int owns_current = 0;
  if (root < 0 || size == 0u) {
    return -1;
  }
  for (index = 0u; index <= size; index++) {
    if (logical[index] == '/' || logical[index] == '\0') {
      size_t component_size = index - start;
      int last = index == size;
      if (component_size == 0u || component_size >= sizeof(component)) {
        if (owns_current != 0) {
          (void)close(current);
        }
        return -1;
      }
      (void)memcpy(component, logical + start, component_size);
      component[component_size] = '\0';
      next = cupidbuild_native_open_relative(
          current, component, last != 0 ? directory : 1);
      if (owns_current != 0) {
        (void)close(current);
      }
      if (next < 0) {
        return -1;
      }
      current = next;
      owns_current = 1;
      start = index + 1u;
    }
  }
  return current;
}

static int cupidbuild_native_open_repository(const char *path) {
  const char *anchor;
  int current;
  int next;
  char component[260];
  size_t start;
  size_t size;
  size_t index;
  if (path == (const char *)0 || path[0] == '\0') {
    return -1;
  }
  anchor = path[0] == '/' ? "/" : ".";
  start = path[0] == '/' ? 1u : 0u;
  size = strlen(path);
  current = open(anchor, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (current < 0 || strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
    return current;
  }
  for (index = start; index <= size; index++) {
    if (path[index] == '/' || path[index] == '\0') {
      size_t component_size = index - start;
      if (component_size == 0u || component_size >= sizeof(component)) {
        (void)close(current);
        return -1;
      }
      (void)memcpy(component, path + start, component_size);
      component[component_size] = '\0';
      if ((component_size == 1u && component[0] == '.') ||
          (component_size == 2u && component[0] == '.' &&
           component[1] == '.')) {
        (void)close(current);
        return -1;
      }
      next = cupidbuild_native_open_relative(current, component, 1);
      (void)close(current);
      if (next < 0) {
        return -1;
      }
      current = next;
      start = index + 1u;
    }
  }
  return current;
}

static int cupidbuild_native_read_repository_regular(
    int repository, const char *logical, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  struct stat information;
  int descriptor = cupidbuild_native_open_relative_path(repository, logical,
                                                          0);
  size_t size;
  unsigned char *bytes;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0) {
    if (descriptor >= 0) {
      (void)close(descriptor);
    }
    return 0;
  }
  if ((unsigned long long)information.st_size >
      (unsigned long long)limit) {
    (void)close(descriptor);
    return 0;
  }
  size = (size_t)information.st_size;
  bytes = (unsigned char *)malloc(size + 1u);
  if (bytes == (unsigned char *)0) {
    (void)close(descriptor);
    return 0;
  }
  while (offset < size) {
    ssize_t count = read(descriptor, bytes + offset, size - offset);
    if (count <= 0) {
      free(bytes);
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
  if (close(descriptor) != 0) {
    free(bytes);
    return 0;
  }
  bytes[size] = 0u;
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
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_native_read_relative_regular(
    int parent, const char *name, int optional, size_t limit,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  struct stat information;
  int descriptor = cupidbuild_native_open_relative(parent, name, 0);
  size_t size;
  unsigned char *bytes;
  size_t offset = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0) {
    if (optional != 0 && errno == ENOENT) {
      if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
      }
      return 1;
    }
    return 0;
  }
  if (fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0 ||
      (unsigned long long)information.st_size >
          (unsigned long long)limit) {
    (void)close(descriptor);
    return 0;
  }
  size = (size_t)information.st_size;
  bytes = (unsigned char *)malloc(size + 1u);
  if (bytes == (unsigned char *)0) {
    (void)close(descriptor);
    return 0;
  }
  while (offset < size) {
    ssize_t count = read(descriptor, bytes + offset, size - offset);
    if (count <= 0) {
      free(bytes);
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
  if (close(descriptor) != 0) {
    free(bytes);
    return 0;
  }
  bytes[size] = 0u;
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
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
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

static int cupidbuild_host_discover_platform(
    cupidbuild_host_transaction_t *transaction, const char *logical_root,
    const char *const *suffixes, size_t suffix_count, int skip_hidden_files,
    int reject_matching_nonfiles, cupidbuild_host_path_list_t *paths) {
  cupidbuild_host_discovery_worklist_t worklist;
  cupidbuild_host_snapshot_t *visited;
  cupidbuild_host_snapshot_t repository_snapshot;
  cupidbuild_host_snapshot_t root_snapshot;
  int repository = transaction->repository_root_descriptor;
  int root = -1;
  size_t visited_count = 0u;
  size_t directory_count = 0u;
  int valid = 1;
  (void)memset(&worklist, 0, sizeof(worklist));
  visited = (cupidbuild_host_snapshot_t *)calloc(
      CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES, sizeof(*visited));
  if (visited == (cupidbuild_host_snapshot_t *)0) {
    free(visited);
    return 0;
  }
  if (!cupidbuild_native_directory_descriptor_snapshot(
          repository, &repository_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &repository_snapshot)) {
    valid = 0;
  } else {
    root = cupidbuild_native_open_relative_path(repository, logical_root, 1);
    if (!cupidbuild_native_directory_descriptor_snapshot(root,
                                                          &root_snapshot) ||
        cupidbuild_host_snapshot_identity_equal(&repository_snapshot,
                                                &root_snapshot) ||
        !cupidbuild_host_discovery_worklist_add(
            &worklist, logical_root, &root_snapshot, root)) {
      valid = 0;
    } else {
      visited[visited_count++] = root_snapshot;
      root = -1;
      directory_count = 1u;
    }
  }
  if (root >= 0) {
    (void)close(root);
  }
  while (valid != 0 && worklist.count != 0u) {
    cupidbuild_host_discovery_directory_t current =
        worklist.directories[worklist.count - 1u];
    cupidbuild_host_snapshot_t completed_snapshot;
    int enumeration_descriptor =
        fcntl(current.descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    DIR *stream = enumeration_descriptor < 0
                      ? (DIR *)0
                      : fdopendir(enumeration_descriptor);
    struct dirent *entry = (struct dirent *)0;
    int retained = 0;
    worklist.count--;
    if (stream == (DIR *)0) {
      if (enumeration_descriptor >= 0) {
        (void)close(enumeration_descriptor);
      }
      valid = 0;
    }
    errno = 0;
    while (valid != 0 &&
           (entry = readdir(stream)) != (struct dirent *)0) {
      char logical[CUPIDBUILD_HOST_PATH_BYTES];
      struct stat information;
      int matches;
      if (strcmp(entry->d_name, ".") == 0 ||
          strcmp(entry->d_name, "..") == 0) {
        errno = 0;
        continue;
      }
      if (!cupidbuild_host_join(logical, sizeof(logical), current.logical,
                                entry->d_name) ||
          fstatat(dirfd(stream), entry->d_name, &information,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        valid = 0;
        break;
      }
      if (entry->d_name[0] == '.' &&
          (S_ISDIR(information.st_mode) || skip_hidden_files != 0)) {
        errno = 0;
        continue;
      }
      if (S_ISLNK(information.st_mode)) {
        valid = 0;
        break;
      }
      matches = cupidbuild_host_discovery_suffix(
          entry->d_name, suffixes, suffix_count);
      if (S_ISDIR(information.st_mode) && matches != 0 &&
          reject_matching_nonfiles != 0) {
        valid = 0;
        break;
      }
      if (S_ISDIR(information.st_mode)) {
        struct stat opened_information;
        cupidbuild_host_snapshot_t child_snapshot;
        int child = cupidbuild_native_open_relative(
            dirfd(stream), entry->d_name, 1);
        size_t prior;
        int alias = 0;
        if (child < 0 || fstat(child, &opened_information) != 0 ||
            !cupidbuild_native_information_identity_equal(
                &information, &opened_information) ||
            !cupidbuild_native_directory_descriptor_snapshot(
                child, &child_snapshot)) {
          if (child >= 0) {
            (void)close(child);
          }
          valid = 0;
          break;
        }
        for (prior = 0u; prior < visited_count; prior++) {
          if (cupidbuild_host_snapshot_identity_equal(
                  &visited[prior], &child_snapshot)) {
            alias = 1;
            break;
          }
        }
        if (alias != 0 ||
            directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
            !cupidbuild_host_discovery_worklist_add(
                &worklist, logical, &child_snapshot, child)) {
          (void)close(child);
          valid = 0;
          break;
        }
        visited[visited_count++] = child_snapshot;
        directory_count++;
      } else if (S_ISREG(information.st_mode) && matches != 0) {
        struct stat opened_information;
        cupidbuild_host_snapshot_t child_snapshot;
        int child = cupidbuild_native_open_relative(
            dirfd(stream), entry->d_name, 0);
        if (child < 0 || fstat(child, &opened_information) != 0 ||
            !S_ISREG(opened_information.st_mode) ||
            !cupidbuild_native_information_identity_equal(
                &information, &opened_information) ||
            !cupidbuild_host_read_open_file(
                child, CUPIDBUILD_HOST_FILE_LIMIT, &child_snapshot,
                (unsigned char **)0) ||
            !cupidbuild_host_discovery_add(
                paths, logical, &child_snapshot)) {
          if (child >= 0) {
            (void)close(child);
          }
          valid = 0;
          break;
        }
        if (close(child) != 0) {
          valid = 0;
          break;
        }
      } else if (matches != 0 && reject_matching_nonfiles != 0) {
        valid = 0;
        break;
      }
      errno = 0;
    }
    if (valid != 0 && entry == (struct dirent *)0 && errno != 0) {
      valid = 0;
    }
    if (stream != (DIR *)0 && closedir(stream) != 0) {
      valid = 0;
    }
    if (valid != 0 &&
        (!cupidbuild_native_directory_descriptor_snapshot(
             current.descriptor, &completed_snapshot) ||
         !cupidbuild_host_snapshot_equal(&current.snapshot,
                                         &completed_snapshot))) {
      valid = 0;
    }
    if (valid != 0) {
      current.snapshot = completed_snapshot;
      if (!cupidbuild_host_bind_discovery_directory(
              transaction, &current, &retained)) {
        valid = 0;
      }
    }
    if (retained != 0) {
      current.descriptor = -1;
      current.logical = (char *)0;
    }
    if (current.descriptor >= 0 && close(current.descriptor) != 0) {
      valid = 0;
    }
    free(current.logical);
  }
  while (worklist.count != 0u) {
    cupidbuild_host_discovery_directory_t *directory =
        &worklist.directories[--worklist.count];
    (void)close(directory->descriptor);
    free(directory->logical);
  }
  free(worklist.directories);
  free(visited);
  return valid;
}

static int cupidbuild_host_seed_members_repository(
    cupidbuild_host_transaction_t *transaction, const char *logical_directory,
    const char *suffix, const char *const *expected, size_t expected_count) {
  cupidbuild_host_snapshot_t root_snapshot;
  int descriptor;
  DIR *stream;
  struct dirent *entry = (struct dirent *)0;
  int valid;
  if (!cupidbuild_native_directory_descriptor_snapshot(
          transaction->repository_root_descriptor, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  descriptor = cupidbuild_native_open_relative_path(
      transaction->repository_root_descriptor, logical_directory, 1);
  if (descriptor < 0) {
    return 0;
  }
  stream = fdopendir(descriptor);
  if (stream == (DIR *)0) {
    (void)close(descriptor);
    return 0;
  }
  valid = 1;
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

static int cupidbuild_host_write_anonymous(
    const char *name, const unsigned char *bytes, size_t size,
    char *path, size_t path_capacity, cupidbuild_host_snapshot_t *snapshot,
    int *descriptor_out) {
  struct stat information;
  size_t offset = 0u;
  int descriptor = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
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
  if (fsync(descriptor) != 0 || fstat(descriptor, &information) != 0 ||
      fcntl(descriptor, F_ADD_SEALS,
            F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) != 0) {
    (void)close(descriptor);
    return 0;
  }
  if (!cupidbuild_host_copy_text(path, path_capacity, name)) {
    (void)close(descriptor);
    return 0;
  }
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
  *descriptor_out = descriptor;
  return 1;
}

static int cupidbuild_host_open_anonymous(const char *name, char *path,
                                          size_t path_capacity,
                                          int *descriptor_out) {
  int descriptor = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (descriptor < 0 ||
      !cupidbuild_host_copy_text(path, path_capacity, name)) {
    if (descriptor >= 0) {
      (void)close(descriptor);
    }
    return 0;
  }
  *descriptor_out = descriptor;
  return 1;
}

static int cupidbuild_host_seal_anonymous(int descriptor) {
  return fcntl(descriptor, F_ADD_SEALS,
               F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) ==
         0;
}

static int cupidbuild_host_absolute_directory(char *destination,
                                              size_t capacity,
                                              const char *path) {
  char current[CUPIDBUILD_HOST_PATH_BYTES];
  if (path == (const char *)0 || path[0] == '\0') {
    return 0;
  }
  if (path[0] == '/') {
    return cupidbuild_host_copy_text(destination, capacity, path);
  }
  if (getcwd(current, sizeof(current)) == (char *)0) {
    return 0;
  }
  return cupidbuild_host_join(destination, capacity, current, path);
}

static int cupidbuild_host_make_executable(const char *path) {
  return chmod(path, 0700) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
  (void)unlink(path);
}

static unsigned int cupidbuild_host_process_id(void) {
  return (unsigned int)getpid();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
  return kill((pid_t)process_id, 0) == 0 || errno == EPERM;
}

static int cupidbuild_host_posix_open_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  struct stat information;
  if (snapshot == (cupidbuild_host_snapshot_t *)0) {
    return 1;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (fstat(descriptor, &information) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->size = (size_t)information.st_size;
  snapshot->identity[0] = (unsigned int)information.st_dev;
  snapshot->identity[1] =
      (unsigned int)((unsigned long long)information.st_dev >> 32u);
  snapshot->identity[2] = (unsigned int)information.st_ino;
  snapshot->identity[3] =
      (unsigned int)((unsigned long long)information.st_ino >> 32u);
  snapshot->modified[0] = (unsigned int)information.st_mtime;
  return 1;
}

static int cupidbuild_host_read_open_file(
    int descriptor, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  cupidbuild_host_snapshot_t current;
  unsigned char *bytes;
  size_t offset = 0u;
  if (!cupidbuild_host_posix_open_snapshot(descriptor, &current) ||
      current.size > limit) {
    return 0;
  }
  bytes = (unsigned char *)malloc(current.size + 1u);
  if (bytes == (unsigned char *)0) {
    return 0;
  }
  while (offset < current.size) {
    ssize_t count = pread(descriptor, bytes + offset,
                          current.size - offset, (off_t)offset);
    if (count <= 0) {
      free(bytes);
      return 0;
    }
    offset += (size_t)count;
  }
  bytes[current.size] = 0u;
  cupidbuild_sha256(bytes, current.size, current.sha256);
  if (snapshot != (cupidbuild_host_snapshot_t *)0) {
    *snapshot = current;
  }
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static char *cupidbuild_host_exec_argument(const char *argument) {
  char *mutable_argument;
  (void)memcpy(&mutable_argument, &argument, sizeof(mutable_argument));
  return mutable_argument;
}

static void cupidbuild_host_write_launch_failure(int descriptor) {
  unsigned char marker = 1u;
  ssize_t result;
  do {
    result = CUPIDBUILD_HOST_LAUNCH_WRITE(descriptor, &marker, 1u);
  } while (result < 0 && errno == EINTR);
}

static int cupidbuild_host_read_launch_status(int descriptor) {
  unsigned char marker = 0u;
  ssize_t result;
  do {
    result = CUPIDBUILD_HOST_LAUNCH_READ(descriptor, &marker, 1u);
  } while (result < 0 && errno == EINTR);
  if (result == 0) {
    return 0;
  }
  if (result == 1 && marker == 1u) {
    return 1;
  }
  return -1;
}

static int cupidbuild_host_posix_duplicate_above_standard(int descriptor) {
  int result;
  if (descriptor > STDERR_FILENO) {
    return descriptor;
  }
  do {
    result = fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
  } while (result < 0 && errno == EINTR);
  return result;
}

static int cupidbuild_host_posix_promote_above_standard(int *descriptor) {
  int original;
  int duplicated;
  if (descriptor == (int *)0 || *descriptor < 0) {
    return 0;
  }
  if (*descriptor > STDERR_FILENO) {
    return 1;
  }
  original = *descriptor;
#if defined(CUPIDBUILD_HOST_LOW_FD_FAILURE_TEST)
  if (getenv("CUPIDBUILD_LOW_FD_TEST_FAIL_PROMOTION") !=
      (const char *)0) {
    (void)close(original);
    *descriptor = -1;
    errno = EMFILE;
    return 0;
  }
#endif
  duplicated = cupidbuild_host_posix_duplicate_above_standard(original);
  if (duplicated <= STDERR_FILENO) {
    (void)close(original);
    *descriptor = -1;
    return 0;
  }
  (void)close(original);
  *descriptor = duplicated;
  return 1;
}

static int cupidbuild_host_posix_duplicate_stream(int source, int target) {
  int result;
  if (source == target) {
    return 1;
  }
  do {
    result = dup2(source, target);
  } while (result < 0 && (errno == EINTR || errno == EBUSY));
  return result == target;
}

static int cupidbuild_host_run_process(const char *tool,
                                       const cupidbuild_host_snapshot_t *expected_tool,
                                       int tool_descriptor,
                                       const char *const *arguments,
                                       const char *stdout_path,
                                       const char *stderr_path,
                                       int stdout_descriptor,
                                       int stderr_descriptor,
                                       const char *working_directory,
                                       int working_descriptor,
                                       const cupidbuild_host_transaction_t *
                                           inherited_transaction,
                                       cupidbuild_host_snapshot_t *stdout_opened,
                                       cupidbuild_host_snapshot_t *stderr_opened,
                                       unsigned int timeout_milliseconds) {
  char **argv;
  size_t argument_count = 0u;
  size_t index;
  pid_t child;
  int own_stream_descriptors = stdout_descriptor < 0;
  int own_tool_descriptor = tool_descriptor < 0;
  int launch_pipe[2] = {-1, -1};
  unsigned int elapsed = 0u;
  int status = 0;
  pid_t wait_result;
  struct timespec pause_time;
  if (own_stream_descriptors == 0 &&
      (stdout_descriptor <= STDERR_FILENO ||
       stderr_descriptor <= STDERR_FILENO ||
       stdout_descriptor == stderr_descriptor)) {
    return -1;
  }
  while (arguments[argument_count] != (const char *)0) {
    if (argument_count == (size_t)-2) {
      return -1;
    }
    argument_count++;
  }
  argv = (char **)calloc(argument_count + 2u, sizeof(*argv));
  if (argv == (char **)0) {
    return -1;
  }
  argv[0] = cupidbuild_host_exec_argument(tool);
  for (index = 0u; index < argument_count; index++) {
    argv[index + 1u] = cupidbuild_host_exec_argument(arguments[index]);
  }
  if (own_tool_descriptor != 0) {
    tool_descriptor = open(tool, O_RDONLY | O_NOFOLLOW);
  }
  {
    cupidbuild_host_snapshot_t tool_snapshot;
    int valid = tool_descriptor >= 0 &&
                cupidbuild_host_read_open_file(
                    tool_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
                    &tool_snapshot, (unsigned char **)0) &&
                cupidbuild_host_snapshot_equal(&tool_snapshot,
                                                expected_tool);
    if (valid == 0) {
      if (tool_descriptor >= 0 && own_tool_descriptor != 0) {
        (void)close(tool_descriptor);
      }
      free(argv);
      return -1;
    }
  }
  if (own_stream_descriptors != 0) {
    stdout_descriptor = open(stdout_path,
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    stderr_descriptor = open(stderr_path,
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  }
  if (stdout_descriptor < 0 || stderr_descriptor < 0 ||
      !cupidbuild_host_posix_open_snapshot(stdout_descriptor,
                                            stdout_opened) ||
      !cupidbuild_host_posix_open_snapshot(stderr_descriptor,
                                            stderr_opened)) {
    if (stdout_descriptor >= 0 && own_stream_descriptors != 0) {
      (void)close(stdout_descriptor);
    }
    if (stderr_descriptor >= 0 && own_stream_descriptors != 0) {
      (void)close(stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
      (void)close(tool_descriptor);
    }
    free(argv);
    return -1;
  }
  if (pipe(launch_pipe) != 0 ||
      !cupidbuild_host_posix_promote_above_standard(&launch_pipe[0]) ||
      !cupidbuild_host_posix_promote_above_standard(&launch_pipe[1]) ||
      fcntl(launch_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
    if (launch_pipe[0] >= 0) {
      (void)close(launch_pipe[0]);
    }
    if (launch_pipe[1] >= 0) {
      (void)close(launch_pipe[1]);
    }
    if (own_stream_descriptors != 0) {
      (void)close(stdout_descriptor);
      (void)close(stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
      (void)close(tool_descriptor);
    }
    free(argv);
    return -1;
  }
  child = fork();
  if (child == 0) {
    int exec_tool_descriptor;
    unsigned int inherited_index;
    (void)close(launch_pipe[0]);
    if ((inherited_transaction !=
             (const cupidbuild_host_transaction_t *)0 &&
         inherited_transaction->private_flat != 0 &&
         ((inherited_transaction->candidate_descriptor >= 0 &&
           fcntl(inherited_transaction->candidate_descriptor,
                 F_SETFD, 0) != 0) ||
          (inherited_transaction->private_output_descriptor >= 0 &&
           fcntl(inherited_transaction->private_output_descriptor,
                 F_SETFD, 0) != 0))) ||
        (working_descriptor >= 0 && fchdir(working_descriptor) != 0) ||
        (working_descriptor < 0 && working_directory != (const char *)0 &&
         chdir(working_directory) != 0)) {
      cupidbuild_host_write_launch_failure(launch_pipe[1]);
      _exit(125);
    }
    if (inherited_transaction !=
            (const cupidbuild_host_transaction_t *)0 &&
        inherited_transaction->private_flat != 0) {
      for (inherited_index = 0u;
           inherited_index < inherited_transaction->input_count;
           inherited_index++) {
        int descriptor = inherited_transaction
                             ->inputs[inherited_index]
                             .frozen_descriptor;
        if (descriptor >= 0 && fcntl(descriptor, F_SETFD, 0) != 0) {
          cupidbuild_host_write_launch_failure(launch_pipe[1]);
          _exit(125);
        }
      }
    }
    exec_tool_descriptor =
        cupidbuild_host_posix_duplicate_above_standard(tool_descriptor);
    if (exec_tool_descriptor < 0 ||
        !cupidbuild_host_posix_duplicate_stream(stdout_descriptor,
                                                 STDOUT_FILENO) ||
        !cupidbuild_host_posix_duplicate_stream(stderr_descriptor,
                                                 STDERR_FILENO)) {
      cupidbuild_host_write_launch_failure(launch_pipe[1]);
      _exit(125);
    }
    if (stdout_descriptor > STDERR_FILENO) {
      (void)close(stdout_descriptor);
    }
    if (stderr_descriptor > STDERR_FILENO &&
        stderr_descriptor != stdout_descriptor) {
      (void)close(stderr_descriptor);
    }
    (void)fexecve(exec_tool_descriptor, argv, environ);
    cupidbuild_host_write_launch_failure(launch_pipe[1]);
    _exit(125);
  }
  if (own_stream_descriptors != 0) {
    (void)close(stdout_descriptor);
    (void)close(stderr_descriptor);
  }
  if (own_tool_descriptor != 0) {
    (void)close(tool_descriptor);
  }
  (void)close(launch_pipe[1]);
  if (child < 0) {
    (void)close(launch_pipe[0]);
    free(argv);
    return -1;
  }
  free(argv);
  pause_time.tv_sec = 0;
  pause_time.tv_nsec = 10000000L;
  for (;;) {
    wait_result = CUPIDBUILD_HOST_LAUNCH_WAITPID(
        child, &status, WNOHANG);
    if (wait_result > 0) {
      break;
    }
    if (wait_result < 0 && errno == EINTR) {
      continue;
    }
    if (wait_result < 0) {
      (void)close(launch_pipe[0]);
      return -1;
    }
    if (elapsed >= timeout_milliseconds) {
      (void)kill(child, SIGKILL);
      do {
        wait_result = CUPIDBUILD_HOST_LAUNCH_WAITPID(child, &status, 0);
      } while (wait_result < 0 && errno == EINTR);
      (void)close(launch_pipe[0]);
      return -2;
    }
    (void)nanosleep(&pause_time, (struct timespec *)0);
    elapsed += 10u;
  }
  wait_result = (pid_t)cupidbuild_host_read_launch_status(launch_pipe[0]);
  (void)close(launch_pipe[0]);
  if (wait_result != 0) {
    return -1;
  }
  if (!WIFEXITED(status)) {
    return 125;
  }
  return WEXITSTATUS(status);
}

static int cupidbuild_host_open_directory(const char *path) {
  return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static int cupidbuild_host_directory_descriptor_snapshot(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
  struct stat information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
      !S_ISDIR(information.st_mode)) {
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
  snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
  snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
  snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
  snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
  return 1;
}

#if defined(CUPIDBUILD_HOST_CLOSE_FAILURE_TEST)
static unsigned int cupidbuild_host_close_failure_countdown;

void cupidbuild_host_close_failure_test_arm(unsigned int close_index) {
  cupidbuild_host_close_failure_countdown = close_index;
}
#endif

static int cupidbuild_host_close_directory(int descriptor) {
  int result;
  if (descriptor < 0) {
    return 1;
  }
  result = close(descriptor);
#if defined(CUPIDBUILD_HOST_CLOSE_FAILURE_TEST)
  if (cupidbuild_host_close_failure_countdown != 0u) {
    cupidbuild_host_close_failure_countdown--;
    if (cupidbuild_host_close_failure_countdown == 0u) {
      return 0;
    }
  }
#endif
  return result == 0;
}

static int cupidbuild_host_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  return cupidbuild_host_posix_atomic_replace(transaction);
}
#endif
#endif

static void cupidbuild_host_profile_parent_set_error(
    cupidbuild_host_profile_parent_t *preparation, const char *message) {
  if (preparation != (cupidbuild_host_profile_parent_t *)0) {
    (void)cupidbuild_host_copy_text(preparation->error,
                                    sizeof(preparation->error), message);
  }
}

static int cupidbuild_host_profile_parent_component_current(
    const char *path, const cupidbuild_host_snapshot_t *expected,
#if defined(_WIN32)
    HANDLE handle
#else
    int descriptor
#endif
);

#if defined(_WIN32)
static HANDLE cupidbuild_host_profile_parent_open_root(const char *path) {
  return cupidbuild_host_windows_open_repository(path);
}

static HANDLE cupidbuild_host_profile_parent_open_component(
    HANDLE parent, const char *name, int create, int delete_access) {
  unsigned short name_buffer[128];
  cupidbuild_windows_unicode_string_t unicode_name;
  cupidbuild_windows_object_attributes_t attributes;
  cupidbuild_windows_io_status_t status;
  HANDLE handle = INVALID_HANDLE_VALUE;
  size_t name_size = strlen(name);
  size_t index;
  long result;
  if (parent == INVALID_HANDLE_VALUE || name_size == 0u ||
      name_size >= sizeof(name_buffer) / sizeof(name_buffer[0])) {
    return INVALID_HANDLE_VALUE;
  }
  for (index = 0u; index < name_size; index++) {
    unsigned char character = (unsigned char)name[index];
    if (character >= 128u || character == '/' || character == '\\') {
      return INVALID_HANDLE_VALUE;
    }
    name_buffer[index] = (unsigned short)character;
  }
  name_buffer[name_size] = 0u;
  unicode_name.length = (unsigned short)(name_size * 2u);
  unicode_name.maximum_length = (unsigned short)((name_size + 1u) * 2u);
  unicode_name.buffer = name_buffer;
  (void)memset(&attributes, 0, sizeof(attributes));
  attributes.length = (unsigned long)sizeof(attributes);
  attributes.root_directory = parent;
  attributes.object_name = &unicode_name;
  attributes.attributes = CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE |
                          CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE;
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_create_file(
      &handle,
      (delete_access != 0 ? DELETE : 0u) | FILE_READ_ATTRIBUTES |
          FILE_TRAVERSE | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
          FILE_LIST_DIRECTORY | SYNCHRONIZE,
      &attributes, &status, (void *)0, 0u,
      FILE_SHARE_READ | FILE_SHARE_WRITE |
          (delete_access != 0 ? 0u : FILE_SHARE_DELETE),
      create != 0 ? CUPIDBUILD_WINDOWS_FILE_CREATE
                  : CUPIDBUILD_WINDOWS_FILE_OPEN,
      CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE |
          CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
          CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT,
      (void *)0, 0u);
  return result >= 0 ? handle : INVALID_HANDLE_VALUE;
}

#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
static int cupidbuild_host_profile_parent_test_replace_existing(
    HANDLE parent, const char *name) {
  static int replaced = 0;
  const char *requested =
      getenv("CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING");
  const char *displaced = strcmp(name, "build") == 0
                              ? "displaced-existing-build"
                              : "displaced-existing-bootstrap";
  HANDLE original;
  HANDLE replacement;
  int renamed;
  int original_closed;
  int replacement_closed;
  if (replaced != 0 || requested == (const char *)0 ||
      strcmp(requested, name) != 0) {
    return 1;
  }
  replaced = 1;
  original = cupidbuild_host_profile_parent_open_component(
      parent, name, 0, 1);
  if (original == INVALID_HANDLE_VALUE) {
    return 0;
  }
  renamed = cupidbuild_host_windows_rename_handle(
      original, parent, displaced, 0);
  original_closed = CloseHandle(original) != 0;
  if (!renamed || !original_closed) {
    return 0;
  }
  replacement = cupidbuild_host_profile_parent_open_component(
      parent, name, 1, 0);
  if (replacement == INVALID_HANDLE_VALUE) {
    return 0;
  }
  replacement_closed = CloseHandle(replacement) != 0;
  return replacement_closed;
}
#endif

static int cupidbuild_host_profile_parent_remove_component(
    HANDLE parent, const char *name, const char *path,
    const cupidbuild_host_snapshot_t *expected, HANDLE *pinned_handle) {
  cupidbuild_windows_io_status_t status;
  cupidbuild_host_snapshot_t delete_snapshot;
  HANDLE delete_handle;
  unsigned char disposition = 1u;
  long result;
  int delete_closed;
  int pinned_closed;
  if (pinned_handle == (HANDLE *)0 ||
      *pinned_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_profile_parent_component_current(
          path, expected, *pinned_handle)) {
    return 0;
  }
  delete_handle = cupidbuild_host_profile_parent_open_component(
      parent, name, 0, 1);
  if (delete_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          delete_handle, &delete_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(&delete_snapshot, expected)) {
    if (delete_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(delete_handle);
    }
    return 0;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_set_information_file(
      delete_handle, &status, &disposition,
      (unsigned long)sizeof(disposition), 13u);
  delete_closed = CloseHandle(delete_handle) != 0;
  pinned_closed = CloseHandle(*pinned_handle) != 0;
  *pinned_handle = INVALID_HANDLE_VALUE;
  return result >= 0 && delete_closed != 0 && pinned_closed != 0 &&
         cupidbuild_host_path_missing(path);
}
#else
static int cupidbuild_host_profile_parent_open_component(
    int parent, const char *name) {
  int descriptor;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  descriptor = cupid_linux_syscall4(
      CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)parent,
      (unsigned int)name,
      CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW |
          CUPIDBUILD_LINUX_O_CLOEXEC,
      0u);
#else
  descriptor = openat(parent, name,
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#endif
  return descriptor;
}

#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
static int cupidbuild_host_profile_parent_test_replace_root_before_open(
    int parent, const char *name) {
  static int replaced = 0;
  const char *requested = getenv(
      "CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_ROOT_BEFORE_OPEN");
  if (replaced != 0 || requested == (const char *)0 ||
      strcmp(requested, name) != 0 ||
      strcmp(name, "displaced-root-component") == 0) {
    return 1;
  }
  replaced = 1;
  return renameat(parent, name, parent, "displaced-root-component") == 0 &&
         symlinkat("displaced-root-component", parent, name) == 0;
}

static int cupidbuild_host_profile_parent_test_replace_existing(
    int parent, const char *name) {
  static int replaced = 0;
  const char *requested =
      getenv("CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING");
  const char *displaced = strcmp(name, "build") == 0
                              ? "displaced-existing-build"
                              : "displaced-existing-bootstrap";
  if (replaced != 0 || requested == (const char *)0 ||
      strcmp(requested, name) != 0) {
    return 1;
  }
  replaced = 1;
  return renameat(parent, name, parent, displaced) == 0 &&
         mkdirat(parent, name, 0700) == 0;
}
#endif

static int cupidbuild_host_profile_parent_open_root(const char *path) {
  char component[CUPIDBUILD_HOST_PATH_BYTES];
  size_t component_size;
  size_t index;
  int descriptor;
  int child;
  if (path == (const char *)0 || path[0] != '/') {
    return -1;
  }
  descriptor = cupidbuild_host_open_directory("/");
  if (descriptor < 0) {
    return -1;
  }
  index = 1u;
  for (;;) {
    while (path[index] == '/') {
      index++;
    }
    if (path[index] == '\0') {
      return descriptor;
    }
    component_size = 0u;
    while (path[index] != '\0' && path[index] != '/') {
      if (component_size >= CUPIDBUILD_HOST_PATH_BYTES - 1u) {
        cupidbuild_host_close_directory(descriptor);
        return -1;
      }
      component[component_size++] = path[index++];
    }
    component[component_size] = '\0';
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
    if (!cupidbuild_host_profile_parent_test_replace_root_before_open(
            descriptor, component)) {
      cupidbuild_host_close_directory(descriptor);
      return -1;
    }
#endif
    child = cupidbuild_host_profile_parent_open_component(
        descriptor, component);
    cupidbuild_host_close_directory(descriptor);
    if (child < 0) {
      return -1;
    }
    descriptor = child;
  }
}

#endif

static int cupidbuild_host_profile_parent_component_current(
    const char *path, const cupidbuild_host_snapshot_t *expected,
#if defined(_WIN32)
    HANDLE handle
#else
    int descriptor
#endif
) {
  cupidbuild_host_snapshot_t path_snapshot;
  cupidbuild_host_snapshot_t open_snapshot;
  if (!cupidbuild_host_directory_snapshot(path, &path_snapshot) ||
#if defined(_WIN32)
      !cupidbuild_host_windows_directory_handle_snapshot(handle,
                                                          &open_snapshot) ||
#else
      !cupidbuild_host_directory_descriptor_snapshot(descriptor,
                                                      &open_snapshot) ||
#endif
      !cupidbuild_host_snapshot_identity_equal(&path_snapshot, expected) ||
      !cupidbuild_host_snapshot_identity_equal(&open_snapshot, expected)) {
    return 0;
  }
  return 1;
}

int cupidbuild_host_profile_parent_prepare(
    const char *repository_root,
    cupidbuild_host_profile_parent_t **preparation_out) {
  cupidbuild_host_profile_parent_t *preparation;
  cupidbuild_host_snapshot_t open_snapshot;
  cupidbuild_host_snapshot_t initial_build_snapshot;
  cupidbuild_host_snapshot_t initial_bootstrap_snapshot;
  int build_missing;
  int bootstrap_missing;
  if (preparation_out == (cupidbuild_host_profile_parent_t **)0) {
    return 0;
  }
  *preparation_out = (cupidbuild_host_profile_parent_t *)0;
  preparation = (cupidbuild_host_profile_parent_t *)calloc(
      1u, sizeof(*preparation));
  if (preparation == (cupidbuild_host_profile_parent_t *)0) {
    return 0;
  }
  (void)memset(&initial_build_snapshot, 0,
               sizeof(initial_build_snapshot));
  (void)memset(&initial_bootstrap_snapshot, 0,
               sizeof(initial_bootstrap_snapshot));
#if defined(_WIN32)
  preparation->repository_root_handle = INVALID_HANDLE_VALUE;
  preparation->build_handle = INVALID_HANDLE_VALUE;
  preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
#else
  preparation->repository_root_descriptor = -1;
  preparation->build_descriptor = -1;
  preparation->bootstrap_descriptor = -1;
#endif
  *preparation_out = preparation;
  if (!cupidbuild_host_absolute_directory(
          preparation->repository_root, sizeof(preparation->repository_root),
          repository_root) ||
      !cupidbuild_host_join(preparation->build_path,
                            sizeof(preparation->build_path),
                            preparation->repository_root, "build") ||
      !cupidbuild_host_join(preparation->bootstrap_path,
                            sizeof(preparation->bootstrap_path),
                            preparation->build_path, "bootstrap") ||
      !cupidbuild_host_directory_snapshot(
          preparation->repository_root,
          &preparation->repository_root_snapshot)) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent repository root is invalid");
    return 0;
  }
#if defined(_WIN32)
  preparation->repository_root_handle =
      cupidbuild_host_profile_parent_open_root(preparation->repository_root);
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          preparation->repository_root_handle, &open_snapshot)) {
#else
  preparation->repository_root_descriptor =
      cupidbuild_host_profile_parent_open_root(preparation->repository_root);
  if (!cupidbuild_host_directory_descriptor_snapshot(
          preparation->repository_root_descriptor, &open_snapshot)) {
#endif
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent repository root cannot be pinned");
    return 0;
  }
  if (!cupidbuild_host_snapshot_identity_equal(
          &open_snapshot, &preparation->repository_root_snapshot)) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent repository root changed while opening");
    return 0;
  }
  build_missing = cupidbuild_host_path_missing(preparation->build_path);
  if (!build_missing &&
      !cupidbuild_host_directory_snapshot(preparation->build_path,
                                           &initial_build_snapshot)) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent build component is a link or collision");
    return 0;
  }
#if defined(_WIN32)
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
  if (!build_missing &&
      !cupidbuild_host_profile_parent_test_replace_existing(
          preparation->repository_root_handle, "build")) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent build replacement test failed");
    return 0;
  }
#endif
  preparation->build_handle = cupidbuild_host_profile_parent_open_component(
      preparation->repository_root_handle, "build", build_missing, 0);
  if (preparation->build_handle != INVALID_HANDLE_VALUE && build_missing) {
    preparation->build_created = 1;
  }
  if (preparation->build_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          preparation->build_handle, &open_snapshot)) {
#else
  if (build_missing) {
    cupidbuild_host_profile_parent_set_error(
        preparation,
        "profile parent build component must already exist on POSIX");
    return 0;
  }
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
  if (!cupidbuild_host_profile_parent_test_replace_existing(
          preparation->repository_root_descriptor, "build")) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent build replacement test failed");
    return 0;
  }
#endif
  preparation->build_descriptor =
      cupidbuild_host_profile_parent_open_component(
          preparation->repository_root_descriptor, "build");
  if (preparation->build_descriptor < 0 ||
      !cupidbuild_host_directory_descriptor_snapshot(
          preparation->build_descriptor, &open_snapshot)) {
#endif
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent build component cannot be prepared");
    return 0;
  }
#if defined(_WIN32)
  if (build_missing) {
    initial_build_snapshot = open_snapshot;
  }
#endif
  if (!cupidbuild_host_directory_snapshot(preparation->build_path,
                                           &preparation->build_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &open_snapshot, &preparation->build_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &initial_build_snapshot, &open_snapshot) ||
      cupidbuild_host_snapshot_identity_equal(
          &preparation->build_snapshot,
          &preparation->repository_root_snapshot)) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent build component changed or aliases root");
    return 0;
  }
  bootstrap_missing = cupidbuild_host_path_missing(preparation->bootstrap_path);
  if (!bootstrap_missing &&
      !cupidbuild_host_directory_snapshot(
          preparation->bootstrap_path, &initial_bootstrap_snapshot)) {
    cupidbuild_host_profile_parent_set_error(
        preparation,
        "profile parent bootstrap component is a link or collision");
    return 0;
  }
#if defined(_WIN32)
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
  if (!bootstrap_missing &&
      !cupidbuild_host_profile_parent_test_replace_existing(
          preparation->build_handle, "bootstrap")) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent bootstrap replacement test failed");
    return 0;
  }
#endif
  preparation->bootstrap_handle =
      cupidbuild_host_profile_parent_open_component(
          preparation->build_handle, "bootstrap", bootstrap_missing, 0);
  if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE &&
      bootstrap_missing) {
    preparation->bootstrap_created = 1;
  }
  if (preparation->bootstrap_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          preparation->bootstrap_handle, &open_snapshot)) {
#else
  if (bootstrap_missing) {
    cupidbuild_host_profile_parent_set_error(
        preparation,
        "profile parent bootstrap component must already exist on POSIX");
    return 0;
  }
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
  if (!cupidbuild_host_profile_parent_test_replace_existing(
          preparation->build_descriptor, "bootstrap")) {
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent bootstrap replacement test failed");
    return 0;
  }
#endif
  preparation->bootstrap_descriptor =
      cupidbuild_host_profile_parent_open_component(
          preparation->build_descriptor, "bootstrap");
  if (preparation->bootstrap_descriptor < 0 ||
      !cupidbuild_host_directory_descriptor_snapshot(
          preparation->bootstrap_descriptor, &open_snapshot)) {
#endif
    cupidbuild_host_profile_parent_set_error(
        preparation, "profile parent bootstrap component cannot be prepared");
    return 0;
  }
#if defined(_WIN32)
  if (bootstrap_missing) {
    initial_bootstrap_snapshot = open_snapshot;
  }
#endif
  if (!cupidbuild_host_directory_snapshot(
          preparation->bootstrap_path, &preparation->bootstrap_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &open_snapshot, &preparation->bootstrap_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &initial_bootstrap_snapshot, &open_snapshot) ||
      !cupidbuild_host_directory_snapshot(
          preparation->repository_root, &open_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &open_snapshot, &preparation->repository_root_snapshot) ||
      cupidbuild_host_snapshot_identity_equal(
          &preparation->bootstrap_snapshot,
          &preparation->repository_root_snapshot) ||
      cupidbuild_host_snapshot_identity_equal(
          &preparation->bootstrap_snapshot, &preparation->build_snapshot) ||
      !cupidbuild_host_profile_parent_component_current(
          preparation->bootstrap_path, &preparation->bootstrap_snapshot,
#if defined(_WIN32)
          preparation->bootstrap_handle
#else
          preparation->bootstrap_descriptor
#endif
          ) ||
      !cupidbuild_host_profile_parent_component_current(
          preparation->build_path, &preparation->build_snapshot,
#if defined(_WIN32)
          preparation->build_handle
#else
          preparation->build_descriptor
#endif
          )) {
    cupidbuild_host_profile_parent_set_error(
        preparation,
        "profile parent bootstrap component changed or aliases its parent");
    return 0;
  }
  return 1;
}

void cupidbuild_host_profile_parent_commit(
    cupidbuild_host_profile_parent_t *preparation) {
  if (preparation != (cupidbuild_host_profile_parent_t *)0) {
    preparation->committed = 1;
  }
}

int cupidbuild_host_profile_parent_close(
    cupidbuild_host_profile_parent_t *preparation) {
  int cleanup_succeeded = 1;
  if (preparation == (cupidbuild_host_profile_parent_t *)0) {
    return 1;
  }
#if defined(_WIN32)
  if (preparation->bootstrap_created != 0 && preparation->committed == 0) {
    int removed = cupidbuild_host_profile_parent_remove_component(
        preparation->build_handle, "bootstrap", preparation->bootstrap_path,
        &preparation->bootstrap_snapshot, &preparation->bootstrap_handle);
    if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(preparation->bootstrap_handle);
      preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
    }
    if (removed == 0 ||
        !cupidbuild_host_path_missing(preparation->bootstrap_path)) {
      cleanup_succeeded = 0;
    }
  } else if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(preparation->bootstrap_handle)) {
      cleanup_succeeded = 0;
    }
    preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
  }
  if (preparation->build_created != 0 && preparation->committed == 0) {
    int removed = cupidbuild_host_profile_parent_remove_component(
        preparation->repository_root_handle, "build",
        preparation->build_path, &preparation->build_snapshot,
        &preparation->build_handle);
    if (preparation->build_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(preparation->build_handle);
      preparation->build_handle = INVALID_HANDLE_VALUE;
    }
    if (removed == 0 ||
        !cupidbuild_host_path_missing(preparation->build_path)) {
      cleanup_succeeded = 0;
    }
  } else if (preparation->build_handle != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(preparation->build_handle)) {
      cleanup_succeeded = 0;
    }
    preparation->build_handle = INVALID_HANDLE_VALUE;
  }
  if (preparation->repository_root_handle != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(preparation->repository_root_handle)) {
      cleanup_succeeded = 0;
    }
    preparation->repository_root_handle = INVALID_HANDLE_VALUE;
  }
#else
  if (!cupidbuild_host_close_directory(preparation->bootstrap_descriptor)) {
    cleanup_succeeded = 0;
  }
  preparation->bootstrap_descriptor = -1;
  if (!cupidbuild_host_close_directory(preparation->build_descriptor)) {
    cleanup_succeeded = 0;
  }
  preparation->build_descriptor = -1;
  if (!cupidbuild_host_close_directory(
          preparation->repository_root_descriptor)) {
    cleanup_succeeded = 0;
  }
  preparation->repository_root_descriptor = -1;
#endif
  free(preparation);
  return cleanup_succeeded;
}

int cupidbuild_host_profile_parent_bind(
    cupidbuild_host_profile_parent_t *preparation,
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t transaction_root;
  cupidbuild_host_snapshot_t transaction_parent;
  if (preparation == (cupidbuild_host_profile_parent_t *)0 ||
      transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 ||
      !cupidbuild_host_profile_parent_component_current(
          preparation->repository_root,
          &preparation->repository_root_snapshot,
#if defined(_WIN32)
          preparation->repository_root_handle) ||
      !cupidbuild_host_profile_parent_component_current(
          preparation->bootstrap_path, &preparation->bootstrap_snapshot,
          preparation->bootstrap_handle) ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->repository_root_handle, &transaction_root) ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->output_parent_handle, &transaction_parent) ||
#else
          preparation->repository_root_descriptor) ||
      !cupidbuild_host_profile_parent_component_current(
          preparation->bootstrap_path, &preparation->bootstrap_snapshot,
          preparation->bootstrap_descriptor) ||
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->repository_root_descriptor, &transaction_root) ||
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->output_parent_descriptor, &transaction_parent) ||
#endif
      !cupidbuild_host_snapshot_identity_equal(
          &preparation->repository_root_snapshot,
          &transaction->repository_root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &preparation->repository_root_snapshot, &transaction_root) ||
      !cupidbuild_host_snapshot_identity_equal(
          &preparation->bootstrap_snapshot,
          &transaction->output_parent_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &preparation->bootstrap_snapshot, &transaction_parent)) {
    cupidbuild_host_set_error(
        transaction, "profile parent changed while checked tools ran");
    return 0;
  }
  return 1;
}

#if defined(_WIN32)
static int cupidbuild_host_windows_dispose_handle(HANDLE handle) {
  cupidbuild_windows_io_status_t status;
  unsigned char disposition = 1u;
  long result;
  if (handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_set_information_file(
      handle, &status, &disposition, (unsigned long)sizeof(disposition), 13u);
  return CloseHandle(handle) != 0 && result >= 0;
}

static int cupidbuild_host_windows_create_relative_regular(
    HANDLE parent, const char *name, const unsigned char *bytes,
    size_t size, int inheritable, cupidbuild_host_snapshot_t *snapshot,
    HANDLE *handle_out) {
  unsigned short name_buffer[4096];
  cupidbuild_windows_unicode_string_t unicode_name;
  cupidbuild_windows_object_attributes_t attributes;
  cupidbuild_windows_io_status_t status;
  BY_HANDLE_FILE_INFORMATION information;
  HANDLE handle = INVALID_HANDLE_VALUE;
  HANDLE write_handle = INVALID_HANDLE_VALUE;
  size_t name_size = name == (const char *)0 ? 0u : strlen(name);
  size_t index;
  size_t offset = 0u;
  long result;
  if (parent == INVALID_HANDLE_VALUE || snapshot == (void *)0 ||
      handle_out == (HANDLE *)0 || (bytes == (const unsigned char *)0 &&
                                    size != 0u) ||
      name_size == 0u ||
      name_size >= sizeof(name_buffer) / sizeof(name_buffer[0])) {
    return 0;
  }
  *handle_out = INVALID_HANDLE_VALUE;
  for (index = 0u; index < name_size; index++) {
    unsigned char character = (unsigned char)name[index];
    if (character >= 128u || character == '/' || character == '\\') {
      return 0;
    }
    name_buffer[index] = (unsigned short)character;
  }
  name_buffer[name_size] = 0u;
  unicode_name.length = (unsigned short)(name_size * 2u);
  unicode_name.maximum_length = (unsigned short)((name_size + 1u) * 2u);
  unicode_name.buffer = name_buffer;
  (void)memset(&attributes, 0, sizeof(attributes));
  attributes.length = (unsigned long)sizeof(attributes);
  attributes.root_directory = parent;
  attributes.object_name = &unicode_name;
  attributes.attributes = CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE |
                          CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE;
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_create_file(
      &handle, DELETE | GENERIC_READ | FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                   (inheritable != 0 ? GENERIC_WRITE : 0u),
      &attributes, &status, (void *)0, 0u,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      CUPIDBUILD_WINDOWS_FILE_CREATE,
      CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE |
          CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
          CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT,
      (void *)0, 0u);
  if (result < 0 || handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  write_handle = inheritable != 0
                     ? handle
                     : cupidbuild_host_windows_open_relative_access(
                           parent, name, 0, 0, GENERIC_WRITE);
  if (write_handle == INVALID_HANDLE_VALUE) {
    (void)cupidbuild_host_windows_dispose_handle(handle);
    return 0;
  }
  while (offset < size) {
    DWORD written = 0u;
    DWORD chunk = (DWORD)(size - offset);
    if (!WriteFile(write_handle, bytes + offset, chunk, &written,
                   (LPOVERLAPPED)0) ||
        written == 0u) {
      if (write_handle != handle) {
        (void)CloseHandle(write_handle);
      }
      (void)cupidbuild_host_windows_dispose_handle(handle);
      return 0;
    }
    offset += (size_t)written;
  }
  if (!FlushFileBuffers(write_handle)) {
    if (write_handle != handle) {
      (void)CloseHandle(write_handle);
    }
    (void)cupidbuild_host_windows_dispose_handle(handle);
    return 0;
  }
  if (write_handle != handle) {
    if (!CloseHandle(write_handle)) {
      (void)cupidbuild_host_windows_dispose_handle(handle);
      return 0;
    }
    write_handle = INVALID_HANDLE_VALUE;
  }
  if (!GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u) {
    (void)cupidbuild_host_windows_dispose_handle(handle);
    return 0;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  snapshot->present = 1;
  snapshot->size = size;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  cupidbuild_sha256(bytes, size, snapshot->sha256);
  *handle_out = handle;
  return 1;
}

static int cupidbuild_host_windows_create_lock(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size, cupidbuild_host_snapshot_t *snapshot) {
  return cupidbuild_host_windows_create_relative_regular(
      transaction->output_parent_handle, transaction->lock_name, bytes, size,
      0, snapshot, &transaction->lock_handle);
}

static int cupidbuild_host_windows_read_open_regular(
    HANDLE handle, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  BY_HANDLE_FILE_INFORMATION information;
  unsigned char *bytes;
  size_t size;
  size_t offset = 0u;
  if (handle == INVALID_HANDLE_VALUE ||
      SetFilePointer(handle, 0, 0, FILE_BEGIN) != 0u ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
      information.nFileSizeHigh != 0u ||
      information.nFileSizeLow > limit) {
    return 0;
  }
  size = (size_t)information.nFileSizeLow;
  bytes = (unsigned char *)malloc(size + 1u);
  if (bytes == (unsigned char *)0) {
    return 0;
  }
  while (offset < size) {
    DWORD read_bytes = 0u;
    DWORD chunk = (DWORD)(size - offset);
    if (!ReadFile(handle, bytes + offset, chunk, &read_bytes,
                  (LPOVERLAPPED)0) ||
        read_bytes == 0u) {
      free(bytes);
      return 0;
    }
    offset += (size_t)read_bytes;
  }
  bytes[size] = 0u;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  snapshot->present = 1;
  snapshot->size = size;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  cupidbuild_sha256(bytes, size, snapshot->sha256);
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  } else {
    free(bytes);
  }
  return 1;
}

static int cupidbuild_host_windows_regular_metadata(
    HANDLE handle, cupidbuild_host_snapshot_t *snapshot) {
  BY_HANDLE_FILE_INFORMATION information;
  if (handle == INVALID_HANDLE_VALUE ||
      snapshot == (cupidbuild_host_snapshot_t *)0 ||
      !GetFileInformationByHandle(handle, &information) ||
      (information.dwFileAttributes &
       (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY |
        FILE_ATTRIBUTE_REPARSE_POINT)) != 0u) {
    return 0;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  snapshot->present = 1;
  snapshot->size = information.nFileSizeHigh == 0u
                       ? (size_t)information.nFileSizeLow
                       : (size_t)-1;
  snapshot->identity[0] = information.dwVolumeSerialNumber;
  snapshot->identity[1] = information.nFileIndexHigh;
  snapshot->identity[2] = information.nFileIndexLow;
  snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
  snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
  return 1;
}

static int cupidbuild_host_windows_relative_regular_metadata(
    HANDLE parent, const char *name, int optional,
    cupidbuild_host_snapshot_t *snapshot) {
  long status = 0;
  unsigned long unsigned_status;
  HANDLE handle;
  int valid;
  if (snapshot == (cupidbuild_host_snapshot_t *)0) {
    return 0;
  }
  (void)memset(snapshot, 0, sizeof(*snapshot));
  handle = cupidbuild_host_windows_open_relative_access_status(
      parent, name, 0, 0, 0u, &status);
  if (handle == INVALID_HANDLE_VALUE) {
    unsigned_status = (unsigned long)status;
    return optional != 0 &&
           (unsigned_status == 0xc000000fu ||
            unsigned_status == 0xc0000034u ||
            unsigned_status == 0xc000003au ||
            unsigned_status == 0xc0000056u);
  }
  valid = cupidbuild_host_windows_regular_metadata(handle, snapshot);
  if (!CloseHandle(handle)) {
    valid = 0;
  }
  return valid;
}

static int cupidbuild_host_windows_dispose_retained_at(
    HANDLE parent, const char *name, cupidbuild_host_snapshot_t *expected,
    HANDLE *retained_handle) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t named;
  cupidbuild_host_snapshot_t after;
  int valid;
  int disposed;
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
  if (name != (const char *)0 &&
      strncmp(name, ".cupidbuild-old-", 16u) == 0 &&
      getenv("CUPIDBUILD_PUBLICATION_TEST_FAIL_OLD_DISPOSITION") !=
          (const char *)0) {
    return 0;
  }
#endif
  if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
      expected == (cupidbuild_host_snapshot_t *)0 ||
      retained_handle == (HANDLE *)0 ||
      *retained_handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  valid = expected->present != 0 &&
          cupidbuild_host_windows_regular_metadata(*retained_handle,
                                                    &retained) &&
          cupidbuild_host_windows_relative_regular_metadata(
              parent, name, 0, &named) &&
          cupidbuild_host_snapshot_identity_equal(&retained, expected) &&
          cupidbuild_host_snapshot_identity_equal(&named, expected) &&
          cupidbuild_host_snapshot_identity_equal(&retained, &named);
  if (valid == 0) {
    (void)CloseHandle(*retained_handle);
    *retained_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  disposed = cupidbuild_host_windows_dispose_handle(*retained_handle);
  *retained_handle = INVALID_HANDLE_VALUE;
  if (!disposed ||
      !cupidbuild_host_windows_relative_regular_metadata(
          parent, name, 1, &after) ||
      after.present != 0) {
    return 0;
  }
  expected->present = 0;
  return 1;
}

static int cupidbuild_host_windows_dispose_read_retained_at(
    HANDLE parent, const char *name, cupidbuild_host_snapshot_t *expected,
    HANDLE *retained_handle) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t named;
  cupidbuild_host_snapshot_t disposable;
  cupidbuild_host_snapshot_t after;
  HANDLE delete_handle = INVALID_HANDLE_VALUE;
  int valid;
  if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
      expected == (cupidbuild_host_snapshot_t *)0 ||
      retained_handle == (HANDLE *)0 ||
      *retained_handle == INVALID_HANDLE_VALUE) {
    return 0;
  }
  valid = expected->present != 0 &&
          cupidbuild_host_windows_regular_metadata(*retained_handle,
                                                    &retained) &&
          cupidbuild_host_windows_read_relative_regular(
              parent, name, 0, CUPIDBUILD_HOST_FILE_LIMIT, &named,
              (unsigned char **)0) &&
          cupidbuild_host_snapshot_identity_equal(&retained, expected) &&
          cupidbuild_host_snapshot_equal(&named, expected);
  if (valid == 0) {
    (void)CloseHandle(*retained_handle);
    *retained_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  if (!CloseHandle(*retained_handle)) {
    *retained_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  *retained_handle = INVALID_HANDLE_VALUE;
  delete_handle = cupidbuild_host_windows_open_relative_access_share_status(
      parent, name, 0, 1, DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      (long *)0);
  if (delete_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_read_open_regular(
          delete_handle, CUPIDBUILD_HOST_FILE_LIMIT, &disposable,
          (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&disposable, expected)) {
    if (delete_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(delete_handle);
    }
    return 0;
  }
  if (!cupidbuild_host_windows_dispose_handle(delete_handle) ||
      !cupidbuild_host_windows_relative_regular_metadata(
          parent, name, 1, &after) ||
      after.present != 0) {
    return 0;
  }
  expected->present = 0;
  return 1;
}

static int cupidbuild_host_windows_transition_retained_at(
    HANDLE parent, const char *name, cupidbuild_host_snapshot_t *expected,
    HANDLE *retained_handle, int seal, int require_unchanged,
    int retain_delete_access) {
  cupidbuild_host_snapshot_t current;
  cupidbuild_host_snapshot_t transition_snapshot;
  cupidbuild_host_snapshot_t final_snapshot;
  cupidbuild_host_snapshot_t named_snapshot;
  HANDLE transition_handle;
  HANDLE final_handle;
  int requested_profile_opened = 1;
  unsigned long final_share =
      seal != 0 ? FILE_SHARE_READ
                : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  unsigned long final_access = retain_delete_access != 0 ? DELETE : 0u;
  if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
      expected == (cupidbuild_host_snapshot_t *)0 ||
      retained_handle == (HANDLE *)0 ||
      *retained_handle == INVALID_HANDLE_VALUE || expected->present == 0 ||
      !cupidbuild_host_windows_read_open_regular(
          *retained_handle, CUPIDBUILD_HOST_FILE_LIMIT, &current,
          (unsigned char **)0) ||
      !cupidbuild_host_snapshot_identity_equal(&current, expected) ||
      (require_unchanged != 0 &&
       !cupidbuild_host_snapshot_equal(&current, expected))) {
    return 0;
  }
  transition_handle = cupidbuild_host_windows_open_relative_access_share_status(
      parent, name, 0, 1, 0u,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      (long *)0);
  if (transition_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_read_open_regular(
          transition_handle, CUPIDBUILD_HOST_FILE_LIMIT,
          &transition_snapshot, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&current, &transition_snapshot)) {
    if (transition_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(transition_handle);
    }
    return 0;
  }
  if (!CloseHandle(*retained_handle)) {
    (void)CloseHandle(transition_handle);
    *retained_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  *retained_handle = INVALID_HANDLE_VALUE;
  final_handle = cupidbuild_host_windows_open_relative_access_share_status(
      parent, name, 0, 1, final_access, final_share,
      (long *)0);
  if (final_handle == INVALID_HANDLE_VALUE) {
    requested_profile_opened = 0;
    if (seal != 0 && retain_delete_access != 0) {
      final_handle = cupidbuild_host_windows_open_relative_access_share_status(
          parent, name, 0, 1, DELETE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          (long *)0);
    }
  }
  if (final_handle == INVALID_HANDLE_VALUE) {
    final_handle = cupidbuild_host_windows_open_relative_access_share_status(
        parent, name, 0, 1, 0u,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        (long *)0);
  }
  if (final_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_read_open_regular(
          final_handle, CUPIDBUILD_HOST_FILE_LIMIT, &final_snapshot,
          (unsigned char **)0) ||
      !cupidbuild_host_windows_read_relative_regular(
          parent, name, 0, CUPIDBUILD_HOST_FILE_LIMIT, &named_snapshot,
          (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&current, &final_snapshot) ||
      !cupidbuild_host_snapshot_equal(&current, &named_snapshot)) {
    if (final_handle != INVALID_HANDLE_VALUE) {
      *retained_handle = final_handle;
    }
    (void)CloseHandle(transition_handle);
    return 0;
  }
  if (!CloseHandle(transition_handle)) {
    *retained_handle = final_handle;
    return 0;
  }
  *retained_handle = final_handle;
  *expected = final_snapshot;
  return requested_profile_opened != 0;
}

static int cupidbuild_host_windows_reclaim_lock(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected) {
  HANDLE handle = cupidbuild_host_windows_open_relative_access(
      transaction->output_parent_handle, transaction->lock_name, 0, 1,
      DELETE);
  cupidbuild_host_snapshot_t current;
  if (handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_read_open_regular(
          handle, CUPIDBUILD_HOST_FILE_LIMIT, &current,
          (unsigned char **)0) ||
      !cupidbuild_host_lock_snapshot_equal(&current, expected)) {
    if (handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(handle);
    }
    return 0;
  }
  return cupidbuild_host_windows_dispose_handle(handle);
}

static int cupidbuild_host_windows_release_lock(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t current;
  cupidbuild_host_snapshot_t after;
  int unchanged = cupidbuild_host_windows_read_relative_regular(
                      transaction->output_parent_handle,
                      transaction->lock_name, 0, CUPIDBUILD_HOST_FILE_LIMIT,
                      &current, (unsigned char **)0) &&
                  cupidbuild_host_lock_snapshot_equal(
                      &current, &transaction->lock_snapshot);
  int disposed = cupidbuild_host_windows_dispose_handle(
      transaction->lock_handle);
  transaction->lock_handle = INVALID_HANDLE_VALUE;
  if (!disposed || !unchanged ||
      !cupidbuild_host_windows_read_relative_regular(
          transaction->output_parent_handle, transaction->lock_name, 1,
          CUPIDBUILD_HOST_FILE_LIMIT, &after, (unsigned char **)0) ||
      after.present != 0) {
    return 0;
  }
  return 1;
}
#endif

#if !defined(_WIN32)
static int cupidbuild_host_unlink_entry_at(int directory, const char *name) {
  int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_UNLINKAT, (unsigned int)directory,
        (unsigned int)name, 0u);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    result = unlinkat(directory, name, 0);
  } while (result != 0 && errno == EINTR);
#endif
  return result == 0;
}

static int cupidbuild_host_link_entry_at(
    int source_directory, const char *source_name,
    int destination_directory, const char *destination_name) {
  int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_LINKAT, (unsigned int)source_directory,
        (unsigned int)source_name, (unsigned int)destination_directory,
        (unsigned int)destination_name, 0u);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    result = linkat(source_directory, source_name, destination_directory,
                    destination_name, 0);
  } while (result != 0 && errno == EINTR);
#endif
  return result == 0;
}

static int cupidbuild_host_link_open_file_at(
    int descriptor, int destination_directory,
    const char *destination_name) {
  char descriptor_path[64];
  int written = snprintf(descriptor_path, sizeof(descriptor_path),
                         "/proc/self/fd/%d", descriptor);
  int result;
  if (descriptor < 0 || written <= 0 ||
      (size_t)written >= sizeof(descriptor_path)) {
    return 0;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_LINKAT,
        (unsigned int)CUPIDBUILD_LINUX_AT_FDCWD,
        (unsigned int)descriptor_path,
        (unsigned int)destination_directory,
        (unsigned int)destination_name,
        CUPIDBUILD_LINUX_AT_SYMLINK_FOLLOW);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    result = linkat(AT_FDCWD, descriptor_path, destination_directory,
                    destination_name, AT_SYMLINK_FOLLOW);
  } while (result != 0 && errno == EINTR);
#endif
  return result == 0;
}

static int cupidbuild_host_rename_entry_at(
    int source_directory, const char *source_name,
    int destination_directory, const char *destination_name) {
  int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall4(
        CUPIDBUILD_LINUX_SYS_RENAMEAT, (unsigned int)source_directory,
        (unsigned int)source_name, (unsigned int)destination_directory,
        (unsigned int)destination_name);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    result = renameat(source_directory, source_name, destination_directory,
                      destination_name);
  } while (result != 0 && errno == EINTR);
#endif
  return result == 0;
}

#define CUPIDBUILD_HOST_NOREPLACE_FAILED 0
#define CUPIDBUILD_HOST_NOREPLACE_COMPLETE 1
#define CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED 2

static int cupidbuild_host_rename_entry_noreplace_status_at(
    int source_directory, const char *source_name,
    int destination_directory, const char *destination_name) {
  int result;
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  const char *test_destination =
      getenv("CUPIDBUILD_NOREPLACE_TEST_DESTINATION");
  int test_target =
      test_destination != (const char *)0 &&
      strcmp(test_destination, destination_name) == 0;
#endif
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_RENAMEAT2, (unsigned int)source_directory,
        (unsigned int)source_name, (unsigned int)destination_directory,
        (unsigned int)destination_name,
        CUPIDBUILD_LINUX_RENAME_NOREPLACE);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST)
  if (test_target != 0 &&
      getenv("CUPIDBUILD_NOREPLACE_TEST_FORCE_FALLBACK") !=
          (const char *)0) {
    result = -1;
  } else
#endif
  {
  do {
    result = renameat2(source_directory, source_name, destination_directory,
                       destination_name, RENAME_NOREPLACE);
  } while (result != 0 && errno == EINTR);
  }
#endif
  if (result == 0) {
    return CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
  }
  /* DrvFS rejects renameat2 flags. Hard-linking first preserves the
     no-replace rule for the regular files handled by this helper. */
  if (!cupidbuild_host_link_entry_at(
          source_directory, source_name,
          destination_directory, destination_name)) {
    return CUPIDBUILD_HOST_NOREPLACE_FAILED;
  }
  result =
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
      test_target != 0 &&
              getenv("CUPIDBUILD_NOREPLACE_TEST_FAIL_SOURCE_UNLINK") !=
                  (const char *)0
          ? 0
          :
#endif
          cupidbuild_host_unlink_entry_at(source_directory, source_name);
  if (!result) {
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
    if (test_target != 0 &&
        getenv("CUPIDBUILD_NOREPLACE_TEST_REPLACE_DESTINATION") !=
            (const char *)0) {
      static const unsigned char replacement[] = "foreign replacement\n";
      size_t offset = 0u;
      int replacement_descriptor;
      if (!cupidbuild_host_unlink_entry_at(
              destination_directory, destination_name)) {
        return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
      }
      do {
        replacement_descriptor = openat(
            destination_directory, destination_name,
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
      } while (replacement_descriptor < 0 && errno == EINTR);
      if (replacement_descriptor < 0) {
        return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
      }
      while (offset + 1u < sizeof(replacement)) {
        ssize_t count = write(
            replacement_descriptor, replacement + offset,
            sizeof(replacement) - 1u - offset);
        if (count < 0 && errno == EINTR) {
          continue;
        }
        if (count <= 0) {
          break;
        }
        offset += (size_t)count;
      }
      (void)close(replacement_descriptor);
    }
#endif
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_host_publication_test_pause("after-noreplace-link")) {
      return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
    }
#endif
    return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
  }
  return CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
}

static int cupidbuild_host_rename_entry_noreplace_at(
    int source_directory, const char *source_name,
    int destination_directory, const char *destination_name) {
  return cupidbuild_host_rename_entry_noreplace_status_at(
             source_directory, source_name,
             destination_directory, destination_name) ==
         CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
}

static int cupidbuild_host_regular_snapshot_at(
    int directory, const char *name, cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  return cupidbuild_linux_read_relative_regular(
      directory, name, 0, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
      (unsigned char **)0);
#else
  return cupidbuild_native_read_relative_regular(
      directory, name, 0, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
      (unsigned char **)0);
#endif
}

static int cupidbuild_host_regular_metadata_at(
    int directory, const char *name, cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  unsigned char information[96];
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      cupid_linux_syscall4(
          CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
          (unsigned int)name, (unsigned int)information,
          CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) < 0 ||
      (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
          CUPIDBUILD_LINUX_S_IFREG) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->size = cupidbuild_linux_u32(information + 48u) == 0u
                       ? (size_t)cupidbuild_linux_u32(information + 44u)
                       : (size_t)-1;
  snapshot->identity[0] = cupidbuild_linux_u32(information);
  snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
  snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
  snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
  snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
  snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
  return 1;
#else
  struct stat information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISREG(information.st_mode) || information.st_size < 0) {
    return 0;
  }
  snapshot->present = 1;
  snapshot->size = (size_t)information.st_size;
  snapshot->identity[0] =
      (unsigned int)(unsigned long long)information.st_dev;
  snapshot->identity[1] =
      (unsigned int)((unsigned long long)information.st_dev >> 32u);
  snapshot->identity[2] =
      (unsigned int)(unsigned long long)information.st_ino;
  snapshot->identity[3] =
      (unsigned int)((unsigned long long)information.st_ino >> 32u);
  snapshot->modified[0] = (unsigned int)information.st_mtime;
  return 1;
#endif
}

static int cupidbuild_host_open_regular_metadata(
    int descriptor, cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  return cupidbuild_host_linux_open_snapshot(descriptor, snapshot);
#else
  return cupidbuild_host_posix_open_snapshot(descriptor, snapshot);
#endif
}

static int cupidbuild_host_directory_entry_snapshot_at(
    int directory, const char *name,
    cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  unsigned char information[96];
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      cupid_linux_syscall4(
          CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
          (unsigned int)name, (unsigned int)information,
          CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) < 0 ||
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
#else
  struct stat information;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISDIR(information.st_mode)) {
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
#endif
}

static int cupidbuild_host_unlink_identity_entry_at(
    int directory, const char *name,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  return cupidbuild_host_regular_metadata_at(directory, name, &current) &&
         cupidbuild_host_snapshot_identity_equal(&current, expected) &&
         cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_promote_retained_descriptor(int *descriptor) {
  int duplicated;
  if (descriptor == (int *)0 || *descriptor < 0) {
    return 0;
  }
  if (*descriptor > 2) {
    return 1;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (!cupidbuild_host_linux_promote_above_standard(descriptor)) {
    return 0;
  }
  duplicated = *descriptor;
  if (
      cupid_linux_syscall3(
          CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)duplicated,
          CUPIDBUILD_LINUX_F_SETFD,
          CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
    if (duplicated >= 0) {
      (void)cupid_linux_syscall1(
          CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)duplicated);
    }
    *descriptor = -1;
    return 0;
  }
#else
  if (!cupidbuild_host_posix_promote_above_standard(descriptor)) {
    return 0;
  }
  duplicated = *descriptor;
  if (
      fcntl(duplicated, F_SETFD, FD_CLOEXEC) != 0) {
    if (duplicated >= 0) {
      (void)close(duplicated);
    }
    *descriptor = -1;
    return 0;
  }
#endif
  *descriptor = duplicated;
  return 1;
}

static int cupidbuild_host_write_exclusive_at(
    int directory, const char *name, const unsigned char *bytes, size_t size,
    int inheritable, cupidbuild_host_snapshot_t *snapshot,
    int *descriptor_out) {
  int descriptor;
  cupidbuild_host_snapshot_t created;
  size_t offset = 0u;
  (void)inheritable;
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      strchr(name, '/') != (char *)0 || strchr(name, '\\') != (char *)0 ||
      snapshot == (cupidbuild_host_snapshot_t *)0 ||
      descriptor_out == (int *)0 ||
      (bytes == (const unsigned char *)0 && size != 0u)) {
    return 0;
  }
  *descriptor_out = -1;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    descriptor = cupid_linux_syscall4(
        CUPIDBUILD_LINUX_SYS_OPENAT,
        (unsigned int)directory, (unsigned int)name,
        CUPIDBUILD_LINUX_O_RDWR | CUPIDBUILD_LINUX_O_CREAT |
            CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW |
            CUPIDBUILD_LINUX_O_CLOEXEC,
        0600u);
  } while (descriptor == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    descriptor = openat(
        directory, name,
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW |
            O_CLOEXEC,
        0600);
  } while (descriptor < 0 && errno == EINTR);
#endif
  if (descriptor < 0) {
    return 0;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (!cupidbuild_host_linux_open_snapshot(descriptor, &created)) {
#else
  if (!cupidbuild_host_posix_open_snapshot(descriptor, &created)) {
#endif
    if (cupidbuild_host_open_regular_metadata(descriptor, &created)) {
      (void)cupidbuild_host_unlink_identity_entry_at(
          directory, name, &created);
    }
    cupidbuild_host_close_directory(descriptor);
    return 0;
  }
  if (!cupidbuild_host_promote_retained_descriptor(&descriptor)) {
    (void)cupidbuild_host_unlink_identity_entry_at(
        directory, name, &created);
    return 0;
  }
  while (offset < size) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    int count = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
        (unsigned int)(bytes + offset), (unsigned int)(size - offset));
    if (count == -CUPIDBUILD_LINUX_EINTR) {
      continue;
    }
#else
    ssize_t count = write(descriptor, bytes + offset, size - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
#endif
    if (count <= 0) {
      (void)cupidbuild_host_unlink_identity_entry_at(
          directory, name, &created);
      cupidbuild_host_close_directory(descriptor);
      return 0;
    }
    offset += (size_t)count;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC,
                           (unsigned int)descriptor) != 0 ||
      !cupidbuild_host_linux_open_snapshot(descriptor, snapshot)) {
#else
  if (fsync(descriptor) != 0 ||
      !cupidbuild_host_posix_open_snapshot(descriptor, snapshot)) {
#endif
    (void)cupidbuild_host_unlink_identity_entry_at(
        directory, name, &created);
    cupidbuild_host_close_directory(descriptor);
    return 0;
  }
  cupidbuild_sha256(bytes, size, snapshot->sha256);
  *descriptor_out = descriptor;
  return 1;
}

static int cupidbuild_host_write_lock_exclusive_at(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size, cupidbuild_host_snapshot_t *snapshot) {
  int descriptor = -1;
  if (!cupidbuild_host_write_exclusive_at(
          transaction->output_parent_descriptor, transaction->lock_name,
          bytes, size, 0, snapshot, &descriptor)) {
    return 0;
  }
  cupidbuild_host_close_directory(descriptor);
  return 1;
}

static int cupidbuild_host_unlink_owned_entry_at(
    int directory, const char *name,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  return cupidbuild_host_regular_snapshot_at(directory, name, &current) &&
         cupidbuild_host_lock_snapshot_equal(&current, expected) &&
         cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_reclaim_lock_at(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected, unsigned int owner) {
  char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
  char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t moved;
  int removed_moved;
  int removed_retained;
  int restored;
  (void)memset(&retained, 0, sizeof(retained));
  (void)memset(&moved, 0, sizeof(moved));
  int retained_written = snprintf(
      retained_name, sizeof(retained_name), "%s.reclaim-link-%08x",
      transaction->lock_name, owner);
  int moved_written = snprintf(
      moved_name, sizeof(moved_name), "%s.reclaim-move-%08x",
      transaction->lock_name, owner);
  if (retained_written <= 0 ||
      (size_t)retained_written >= sizeof(retained_name) ||
      moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
      !cupidbuild_host_link_entry_at(
          transaction->output_parent_descriptor, transaction->lock_name,
          transaction->output_parent_descriptor, retained_name)) {
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          transaction->output_parent_descriptor, retained_name, &retained) ||
      !cupidbuild_host_lock_snapshot_equal(&retained, expected)) {
    (void)cupidbuild_host_unlink_owned_entry_at(
        transaction->output_parent_descriptor, retained_name, expected);
    return 0;
  }
  if (!cupidbuild_host_rename_entry_noreplace_at(
          transaction->output_parent_descriptor, transaction->lock_name,
          transaction->output_parent_descriptor, moved_name)) {
    (void)cupidbuild_host_unlink_owned_entry_at(
        transaction->output_parent_descriptor, retained_name, expected);
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          transaction->output_parent_descriptor, moved_name, &moved) ||
      !cupidbuild_host_lock_snapshot_equal(&moved, expected)) {
    cupidbuild_host_snapshot_t restored_snapshot;
    restored = cupidbuild_host_link_entry_at(
                   transaction->output_parent_descriptor, retained_name,
                   transaction->output_parent_descriptor,
                   transaction->lock_name) &&
               cupidbuild_host_regular_snapshot_at(
                   transaction->output_parent_descriptor,
                   transaction->lock_name, &restored_snapshot) &&
               cupidbuild_host_lock_snapshot_equal(
                   &restored_snapshot, expected);
    if (restored) {
      (void)cupidbuild_host_unlink_owned_entry_at(
          transaction->output_parent_descriptor, retained_name, expected);
    }
    return 0;
  }
  removed_moved = cupidbuild_host_unlink_owned_entry_at(
      transaction->output_parent_descriptor, moved_name, expected);
  removed_retained = cupidbuild_host_unlink_owned_entry_at(
      transaction->output_parent_descriptor, retained_name, expected);
  return removed_moved && removed_retained;
}

static int cupidbuild_host_release_lock_at(
    cupidbuild_host_transaction_t *transaction) {
  char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
  char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t moved;
  int removed_moved;
  int removed_retained;
  int restored;
  (void)memset(&retained, 0, sizeof(retained));
  (void)memset(&moved, 0, sizeof(moved));
  unsigned int process_id = cupidbuild_host_process_id();
  int retained_written = snprintf(
      retained_name, sizeof(retained_name), "%s.release-link-%08x",
      transaction->lock_name, process_id);
  int moved_written = snprintf(
      moved_name, sizeof(moved_name), "%s.release-move-%08x",
      transaction->lock_name, process_id);
  if (retained_written <= 0 ||
      (size_t)retained_written >= sizeof(retained_name) ||
      moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
      !cupidbuild_host_link_entry_at(
          transaction->output_parent_descriptor, transaction->lock_name,
          transaction->output_parent_descriptor, retained_name)) {
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          transaction->output_parent_descriptor, retained_name, &retained) ||
      !cupidbuild_host_lock_snapshot_equal(
          &retained, &transaction->lock_snapshot)) {
    (void)cupidbuild_host_unlink_owned_entry_at(
        transaction->output_parent_descriptor, retained_name,
        &transaction->lock_snapshot);
    return 0;
  }
  if (!cupidbuild_host_rename_entry_noreplace_at(
          transaction->output_parent_descriptor, transaction->lock_name,
          transaction->output_parent_descriptor, moved_name)) {
    (void)cupidbuild_host_unlink_owned_entry_at(
        transaction->output_parent_descriptor, retained_name,
        &transaction->lock_snapshot);
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          transaction->output_parent_descriptor, moved_name, &moved) ||
      !cupidbuild_host_lock_snapshot_equal(
          &moved, &transaction->lock_snapshot)) {
    cupidbuild_host_snapshot_t restored_snapshot;
    restored = cupidbuild_host_link_entry_at(
                   transaction->output_parent_descriptor, retained_name,
                   transaction->output_parent_descriptor,
                   transaction->lock_name) &&
               cupidbuild_host_regular_snapshot_at(
                   transaction->output_parent_descriptor,
                   transaction->lock_name, &restored_snapshot) &&
               cupidbuild_host_lock_snapshot_equal(
                   &restored_snapshot, &transaction->lock_snapshot);
    if (restored) {
      (void)cupidbuild_host_unlink_owned_entry_at(
          transaction->output_parent_descriptor, retained_name,
          &transaction->lock_snapshot);
    }
    return 0;
  }
  removed_moved = cupidbuild_host_unlink_owned_entry_at(
      transaction->output_parent_descriptor, moved_name,
      &transaction->lock_snapshot);
  removed_retained = cupidbuild_host_unlink_owned_entry_at(
      transaction->output_parent_descriptor, retained_name,
      &transaction->lock_snapshot);
  return removed_moved && removed_retained;
}
#endif

static const char *cupidbuild_host_private_entry_name(
    const cupidbuild_host_transaction_t *transaction, const char *name) {
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      name == (const char *)0 || transaction->private_flat == 0) {
    return name;
  }
  if (strcmp(name, "candidate.o") == 0) {
    return transaction->candidate_name;
  }
  if (strcmp(name, CUPIDBUILD_HOST_CANDIDATE_PUBLISH) == 0) {
    return transaction->candidate_publish_name;
  }
  if (strcmp(name, "candidate.map") == 0) {
    return transaction->private_output_name;
  }
  if (strcmp(name, "tool.stdout") == 0) {
    return transaction->tool_stdout_name;
  }
  if (strcmp(name, "tool.stderr") == 0) {
    return transaction->tool_stderr_name;
  }
  return name;
}

static int cupidbuild_host_private_entry_path(
    const cupidbuild_host_transaction_t *transaction, char *destination,
    size_t capacity, const char *name) {
  const char *entry_name =
      cupidbuild_host_private_entry_name(transaction, name);
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      destination == (char *)0 || entry_name == (const char *)0) {
    return 0;
  }
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    int descriptor = -1;
    int written;
    if (strcmp(entry_name, transaction->candidate_name) == 0) {
      descriptor = transaction->candidate_descriptor;
    } else if (strcmp(entry_name, transaction->private_output_name) == 0) {
      descriptor = transaction->private_output_descriptor;
    } else if (strcmp(entry_name, transaction->tool_stdout_name) == 0) {
      descriptor = transaction->tool_stdout_descriptor;
    } else if (strcmp(entry_name, transaction->tool_stderr_name) == 0) {
      descriptor = transaction->tool_stderr_descriptor;
    }
    if (descriptor < 0) {
      return 0;
    }
    written = snprintf(
        destination, capacity, "/proc/self/fd/%d", descriptor);
    return written >= 0 && (size_t)written < capacity;
  }
#endif
  return cupidbuild_host_join(
      destination, capacity, transaction->private_root, entry_name);
}

static int cupidbuild_host_read_private_regular(
    cupidbuild_host_transaction_t *transaction, const char *name,
    int optional, size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 || name == (const char *)0 ||
      name[0] == '\0') {
    return 0;
  }
  name = cupidbuild_host_private_entry_name(transaction, name);
#if defined(_WIN32)
  return cupidbuild_host_windows_read_relative_regular(
      transaction->private_handle, name, optional, limit, snapshot,
      bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  return cupidbuild_linux_read_relative_regular(
      transaction->private_descriptor, name, optional, limit, snapshot,
      bytes_out);
#else
  return cupidbuild_native_read_relative_regular(
      transaction->private_descriptor, name, optional, limit, snapshot,
      bytes_out);
#endif
#endif
}

static int cupidbuild_host_write_private_exclusive(
    cupidbuild_host_transaction_t *transaction, const char *name,
    const unsigned char *bytes, size_t size, int inheritable,
    cupidbuild_host_snapshot_t *snapshot,
#if defined(_WIN32)
    HANDLE *handle_out) {
#else
    int *descriptor_out) {
#endif
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 ||
      transaction->private_created == 0 ||
      !cupidbuild_host_private_root_is_owned(transaction)) {
    return 0;
  }
  name = cupidbuild_host_private_entry_name(transaction, name);
#if defined(_WIN32)
  return cupidbuild_host_windows_create_relative_regular(
      transaction->private_handle, name, bytes, size, inheritable, snapshot,
      handle_out);
#else
  return cupidbuild_host_write_exclusive_at(
      transaction->private_descriptor, name, bytes, size, inheritable,
      snapshot, descriptor_out);
#endif
}

#if !defined(_WIN32)
static int cupidbuild_host_open_anonymous_artifact(
    const char *name, cupidbuild_host_snapshot_t *snapshot,
    int *descriptor_out) {
  char label[CUPIDBUILD_HOST_PATH_BYTES];
  if (!cupidbuild_host_open_anonymous(
          name, label, sizeof(label), descriptor_out) ||
      !cupidbuild_host_promote_retained_descriptor(descriptor_out) ||
      !cupidbuild_host_read_open_file(
          *descriptor_out, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
          (unsigned char **)0)) {
    cupidbuild_host_close_directory(*descriptor_out);
    *descriptor_out = -1;
    return 0;
  }
  return 1;
}

static int cupidbuild_host_private_entry_is_anonymous(
    const cupidbuild_host_transaction_t *transaction, const char *name) {
  return transaction != (const cupidbuild_host_transaction_t *)0 &&
         transaction->private_flat != 0 && name != (const char *)0 &&
         strcmp(name, "candidate.o") != 0 &&
         strcmp(name, CUPIDBUILD_HOST_CANDIDATE_PUBLISH) != 0;
}
#endif

static int cupidbuild_host_read_retained_private_regular(
    cupidbuild_host_transaction_t *transaction, const char *name,
    size_t limit,
#if defined(_WIN32)
    HANDLE handle,
#else
    int descriptor,
#endif
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t named;
  unsigned char *bytes = (unsigned char *)0;
#if defined(_WIN32)
  int valid = cupidbuild_host_windows_read_open_regular(
      handle, limit, &retained,
      bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0);
#else
  int valid = descriptor >= 0 && cupidbuild_host_read_open_file(
      descriptor, limit, &retained,
      bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0);
#endif
  if (valid == 0 || retained.size > limit) {
    free(bytes);
    return 0;
  }
#if !defined(_WIN32)
  if (!cupidbuild_host_private_entry_is_anonymous(transaction, name)) {
#endif
    if (!cupidbuild_host_read_private_regular(
            transaction, name, 0, limit, &named, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&retained, &named)) {
      free(bytes);
      return 0;
    }
#if !defined(_WIN32)
  }
#endif
  *snapshot = retained;
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  }
  return 1;
}

#if !defined(_WIN32)
static int cupidbuild_host_unlink_exact_entry_at(
    int directory, const char *name,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  return cupidbuild_host_regular_snapshot_at(directory, name, &current) &&
         cupidbuild_host_snapshot_equal(&current, expected) &&
         cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_delete_owned_regular_at(
    int directory, const char *name,
    const cupidbuild_host_snapshot_t *expected) {
  char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
  char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t current;
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t moved;
  unsigned int process_id = cupidbuild_host_process_id();
  int retained_written;
  int moved_written;
  int removed_moved;
  int removed_retained;
  int restored;
  (void)memset(&retained, 0, sizeof(retained));
  (void)memset(&moved, 0, sizeof(moved));
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      expected == (const cupidbuild_host_snapshot_t *)0 ||
      !cupidbuild_host_regular_snapshot_at(directory, name, &current)) {
    cupidbuild_host_snapshot_t optional;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_linux_read_relative_regular(
            directory, name, 1, CUPIDBUILD_HOST_FILE_LIMIT, &optional,
            (unsigned char **)0)) {
#else
    if (!cupidbuild_native_read_relative_regular(
            directory, name, 1, CUPIDBUILD_HOST_FILE_LIMIT, &optional,
            (unsigned char **)0)) {
#endif
      return 0;
    }
    return optional.present == 0;
  }
  if (expected->present == 0 ||
      !cupidbuild_host_snapshot_equal(&current, expected)) {
    return 0;
  }
  retained_written = snprintf(
      retained_name, sizeof(retained_name), ".%s.cleanup-link-%08x",
      name, process_id);
  moved_written = snprintf(
      moved_name, sizeof(moved_name), ".%s.cleanup-move-%08x",
      name, process_id);
  if (retained_written <= 0 ||
      (size_t)retained_written >= sizeof(retained_name) ||
      moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
      !cupidbuild_host_link_entry_at(directory, name, directory,
                                     retained_name)) {
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          directory, retained_name, &retained) ||
      !cupidbuild_host_snapshot_equal(&retained, expected)) {
    (void)cupidbuild_host_unlink_exact_entry_at(
        directory, retained_name, expected);
    return 0;
  }
  if (!cupidbuild_host_rename_entry_noreplace_at(
          directory, name, directory, moved_name)) {
    (void)cupidbuild_host_unlink_exact_entry_at(
        directory, retained_name, expected);
    return 0;
  }
  if (!cupidbuild_host_regular_snapshot_at(directory, moved_name, &moved) ||
      !cupidbuild_host_snapshot_equal(&moved, expected)) {
    cupidbuild_host_snapshot_t restored_snapshot;
    restored = cupidbuild_host_link_entry_at(
                   directory, retained_name, directory, name) &&
               cupidbuild_host_regular_snapshot_at(
                   directory, name, &restored_snapshot) &&
               cupidbuild_host_snapshot_equal(
                   &restored_snapshot, expected);
    if (restored) {
      (void)cupidbuild_host_unlink_exact_entry_at(
          directory, retained_name, expected);
    }
    return 0;
  }
  removed_moved = cupidbuild_host_unlink_exact_entry_at(
      directory, moved_name, expected);
  removed_retained = cupidbuild_host_unlink_exact_entry_at(
      directory, retained_name, expected);
  return removed_moved && removed_retained;
}

static int cupidbuild_host_close_retained_descriptor(int *descriptor) {
  int result;
  if (descriptor == (int *)0 || *descriptor < 0) {
    return 0;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  result = cupid_linux_syscall1(
      CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)*descriptor);
#else
  result = close(*descriptor);
#endif
  *descriptor = -1;
  return result == 0;
}

static int cupidbuild_host_cleanup_retained_regular_at(
    int directory, const char *name, cupidbuild_host_snapshot_t *expected,
    int *descriptor) {
  char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
  char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t opened;
  cupidbuild_host_snapshot_t named;
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t moved;
  unsigned int process_id = cupidbuild_host_process_id();
  int retained_written;
  int moved_written;
  int valid;
  int removed_moved;
  int removed_retained;
  int closed;
  retained_name[0] = '\0';
  moved_name[0] = '\0';
  if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
      expected == (cupidbuild_host_snapshot_t *)0 ||
      descriptor == (int *)0 || *descriptor < 0) {
    return 0;
  }
  valid = expected->present != 0 &&
          cupidbuild_host_open_regular_metadata(*descriptor, &opened) &&
          cupidbuild_host_regular_metadata_at(directory, name, &named) &&
          cupidbuild_host_snapshot_identity_equal(&opened, expected) &&
          cupidbuild_host_snapshot_identity_equal(&named, expected) &&
          cupidbuild_host_snapshot_identity_equal(&opened, &named);
  if (valid == 0) {
    (void)cupidbuild_host_close_retained_descriptor(descriptor);
    return 0;
  }
  retained_written = snprintf(
      retained_name, sizeof(retained_name), ".%s.cleanup-link-%08x",
      name, process_id);
  moved_written = snprintf(
      moved_name, sizeof(moved_name), ".%s.cleanup-move-%08x",
      name, process_id);
  if (retained_written <= 0 ||
      (size_t)retained_written >= sizeof(retained_name) ||
      moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
      !cupidbuild_host_link_open_file_at(
          *descriptor, directory, retained_name) ||
      !cupidbuild_host_regular_metadata_at(
          directory, retained_name, &retained) ||
      !cupidbuild_host_snapshot_identity_equal(&retained, expected)) {
    (void)cupidbuild_host_unlink_identity_entry_at(
        directory, retained_name, expected);
    (void)cupidbuild_host_close_retained_descriptor(descriptor);
    return 0;
  }
  if (!cupidbuild_host_rename_entry_noreplace_at(
          directory, name, directory, moved_name)) {
    (void)cupidbuild_host_unlink_identity_entry_at(
        directory, retained_name, expected);
    (void)cupidbuild_host_close_retained_descriptor(descriptor);
    return 0;
  }
  if (!cupidbuild_host_regular_metadata_at(directory, moved_name, &moved) ||
      !cupidbuild_host_snapshot_identity_equal(&moved, expected)) {
    cupidbuild_host_snapshot_t restored;
    int restored_expected =
        cupidbuild_host_link_entry_at(
            directory, retained_name, directory, name) &&
        cupidbuild_host_regular_metadata_at(directory, name, &restored) &&
        cupidbuild_host_snapshot_identity_equal(&restored, expected);
    if (restored_expected != 0) {
      (void)cupidbuild_host_unlink_identity_entry_at(
          directory, retained_name, expected);
    }
    (void)cupidbuild_host_close_retained_descriptor(descriptor);
    return 0;
  }
  removed_moved = cupidbuild_host_unlink_identity_entry_at(
      directory, moved_name, expected);
  removed_retained = cupidbuild_host_unlink_identity_entry_at(
      directory, retained_name, expected);
  closed = cupidbuild_host_close_retained_descriptor(descriptor);
  if (!removed_moved || !removed_retained || !closed) {
    return 0;
  }
  expected->present = 0;
  return 1;
}
#endif

static int cupidbuild_host_delete_owned_private_name(
    cupidbuild_host_transaction_t *transaction, const char *name,
    cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 || name == (const char *)0 ||
      expected == (cupidbuild_host_snapshot_t *)0 ||
      !cupidbuild_host_read_private_regular(
          transaction, name, 1, CUPIDBUILD_HOST_FILE_LIMIT, &current,
          (unsigned char **)0)) {
    return 0;
  }
#if !defined(_WIN32)
  if (cupidbuild_host_private_entry_is_anonymous(transaction, name)) {
    expected->present = 0;
    return 1;
  }
#endif
  name = cupidbuild_host_private_entry_name(transaction, name);
  if (current.present == 0) {
    expected->present = 0;
    return 1;
  }
  if (expected->present == 0 ||
      !cupidbuild_host_snapshot_equal(&current, expected)) {
    return 0;
  }
#if defined(_WIN32)
  {
    HANDLE handle = cupidbuild_host_windows_open_relative_access(
        transaction->private_handle, name, 0, 1, DELETE);
    cupidbuild_host_snapshot_t opened;
    cupidbuild_host_snapshot_t after;
    if (handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(
            handle, CUPIDBUILD_HOST_FILE_LIMIT, &opened,
            (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&opened, expected)) {
      if (handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(handle);
      }
      return 0;
    }
    if (!cupidbuild_host_windows_dispose_handle(handle) ||
        !cupidbuild_host_read_private_regular(
            transaction, name, 1, CUPIDBUILD_HOST_FILE_LIMIT, &after,
            (unsigned char **)0) ||
        after.present != 0) {
      return 0;
    }
  }
#else
  if (!cupidbuild_host_delete_owned_regular_at(
          transaction->private_descriptor, name, expected)) {
    return 0;
  }
#endif
  expected->present = 0;
  return 1;
}

static int cupidbuild_host_close_private_stream_handles(
    cupidbuild_host_transaction_t *transaction) {
  int valid = 1;
#if defined(_WIN32)
  if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE) {
    if (!cupidbuild_host_windows_dispose_retained_at(
            transaction->private_handle, "tool.stdout",
            &transaction->tool_stdout_snapshot,
            &transaction->tool_stdout_handle)) {
      valid = 0;
    }
  }
  if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE) {
    if (!cupidbuild_host_windows_dispose_retained_at(
            transaction->private_handle, "tool.stderr",
            &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_handle)) {
      valid = 0;
    }
  }
#else
  if (transaction->tool_stdout_descriptor >= 0) {
    if (!(transaction->private_flat != 0
              ? cupidbuild_host_close_retained_descriptor(
                    &transaction->tool_stdout_descriptor)
              : cupidbuild_host_cleanup_retained_regular_at(
                    transaction->private_descriptor,
                    cupidbuild_host_private_entry_name(
                        transaction, "tool.stdout"),
                    &transaction->tool_stdout_snapshot,
                    &transaction->tool_stdout_descriptor))) {
      valid = 0;
    }
  }
  if (transaction->tool_stderr_descriptor >= 0) {
    if (!(transaction->private_flat != 0
              ? cupidbuild_host_close_retained_descriptor(
                    &transaction->tool_stderr_descriptor)
              : cupidbuild_host_cleanup_retained_regular_at(
                    transaction->private_descriptor,
                    cupidbuild_host_private_entry_name(
                        transaction, "tool.stderr"),
                    &transaction->tool_stderr_snapshot,
                    &transaction->tool_stderr_descriptor))) {
      valid = 0;
    }
  }
#endif
  transaction->tool_stdout_sealed = 0;
  transaction->tool_stderr_sealed = 0;
  return valid;
}

static int cupidbuild_host_prepare_private_streams(
    cupidbuild_host_transaction_t *transaction) {
  static const unsigned char empty[1] = {0u};
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
#if defined(_WIN32)
      transaction->tool_stdout_handle != INVALID_HANDLE_VALUE ||
      transaction->tool_stderr_handle != INVALID_HANDLE_VALUE ||
#else
      transaction->tool_stdout_descriptor >= 0 ||
      transaction->tool_stderr_descriptor >= 0 ||
#endif
      transaction->private_created == 0) {
    return 0;
  }
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    if (!cupidbuild_host_open_anonymous_artifact(
            "cupidbuild-stdout", &transaction->tool_stdout_snapshot,
            &transaction->tool_stdout_descriptor) ||
        !cupidbuild_host_open_anonymous_artifact(
            "cupidbuild-stderr", &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_descriptor) ||
        !cupidbuild_host_private_entry_path(
            transaction, transaction->tool_stdout,
            sizeof(transaction->tool_stdout), "tool.stdout") ||
        !cupidbuild_host_private_entry_path(
            transaction, transaction->tool_stderr,
            sizeof(transaction->tool_stderr), "tool.stderr")) {
      (void)cupidbuild_host_close_retained_descriptor(
          &transaction->tool_stdout_descriptor);
      (void)cupidbuild_host_close_retained_descriptor(
          &transaction->tool_stderr_descriptor);
      (void)memset(&transaction->tool_stdout_snapshot, 0,
                   sizeof(transaction->tool_stdout_snapshot));
      (void)memset(&transaction->tool_stderr_snapshot, 0,
                   sizeof(transaction->tool_stderr_snapshot));
      return 0;
    }
    return 1;
  }
#endif
  if (!cupidbuild_host_write_private_exclusive(
          transaction, "tool.stdout", empty, 0u, 1,
          &transaction->tool_stdout_snapshot,
#if defined(_WIN32)
          &transaction->tool_stdout_handle
#else
          &transaction->tool_stdout_descriptor
#endif
          )) {
    return 0;
  }
  if (!cupidbuild_host_write_private_exclusive(
          transaction, "tool.stderr", empty, 0u, 1,
          &transaction->tool_stderr_snapshot,
#if defined(_WIN32)
          &transaction->tool_stderr_handle
#else
          &transaction->tool_stderr_descriptor
#endif
          )) {
#if defined(_WIN32)
    (void)cupidbuild_host_windows_dispose_retained_at(
        transaction->private_handle, "tool.stdout",
        &transaction->tool_stdout_snapshot,
        &transaction->tool_stdout_handle);
#else
    (void)cupidbuild_host_cleanup_retained_regular_at(
        transaction->private_descriptor,
        cupidbuild_host_private_entry_name(transaction, "tool.stdout"),
        &transaction->tool_stdout_snapshot,
        &transaction->tool_stdout_descriptor);
#endif
    return 0;
  }
  return cupidbuild_host_private_entry_path(
             transaction, transaction->tool_stdout,
             sizeof(transaction->tool_stdout), "tool.stdout") &&
         cupidbuild_host_private_entry_path(
             transaction, transaction->tool_stderr,
             sizeof(transaction->tool_stderr), "tool.stderr");
}

static int cupidbuild_host_prepare_private_stderr(
    cupidbuild_host_transaction_t *transaction) {
  static const unsigned char empty[1] = {0u};
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->private_created == 0 ||
      !cupidbuild_host_private_root_is_owned(transaction)) {
    return 0;
  }
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    if (!cupidbuild_host_open_anonymous_artifact(
            "cupidbuild-stderr", &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_descriptor) ||
        !cupidbuild_host_private_entry_path(
            transaction, transaction->tool_stderr,
            sizeof(transaction->tool_stderr), "tool.stderr")) {
      (void)cupidbuild_host_close_retained_descriptor(
          &transaction->tool_stderr_descriptor);
      (void)memset(&transaction->tool_stderr_snapshot, 0,
                   sizeof(transaction->tool_stderr_snapshot));
      return 0;
    }
    return 1;
  }
#endif
  return cupidbuild_host_write_private_exclusive(
             transaction, "tool.stderr", empty, 0u, 1,
             &transaction->tool_stderr_snapshot,
#if defined(_WIN32)
             &transaction->tool_stderr_handle
#else
             &transaction->tool_stderr_descriptor
#endif
             ) &&
         cupidbuild_host_private_entry_path(
             transaction, transaction->tool_stderr,
             sizeof(transaction->tool_stderr), "tool.stderr");
}

static int cupidbuild_host_capture_private_streams(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t standard_output;
  cupidbuild_host_snapshot_t standard_error;
#if defined(_WIN32)
  if ((transaction->tool_stdout_sealed == 0 &&
       !cupidbuild_host_windows_transition_retained_at(
           transaction->private_handle, "tool.stdout",
           &transaction->tool_stdout_snapshot,
           &transaction->tool_stdout_handle, 1, 0, 1)) ||
      (transaction->tool_stderr_sealed == 0 &&
       !cupidbuild_host_windows_transition_retained_at(
           transaction->private_handle, "tool.stderr",
           &transaction->tool_stderr_snapshot,
           &transaction->tool_stderr_handle, 1, 0, 1))) {
    return 0;
  }
  transaction->tool_stdout_sealed = 1;
  transaction->tool_stderr_sealed = 1;
#endif
#if !defined(_WIN32)
  if (transaction->private_flat != 0 &&
      ((transaction->tool_stdout_sealed == 0 &&
        !cupidbuild_host_seal_anonymous(
            transaction->tool_stdout_descriptor)) ||
       (transaction->tool_stderr_sealed == 0 &&
        !cupidbuild_host_seal_anonymous(
            transaction->tool_stderr_descriptor)))) {
    return 0;
  }
  transaction->tool_stdout_sealed = 1;
  transaction->tool_stderr_sealed = 1;
#endif
  if (!cupidbuild_host_read_retained_private_regular(
          transaction, "tool.stdout", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
          transaction->tool_stdout_handle,
#else
          transaction->tool_stdout_descriptor,
#endif
          &standard_output, (unsigned char **)0) ||
      !cupidbuild_host_read_retained_private_regular(
          transaction, "tool.stderr", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
          transaction->tool_stderr_handle,
#else
          transaction->tool_stderr_descriptor,
#endif
          &standard_error, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_identity_equal(
          &standard_output, &transaction->tool_stdout_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &standard_error, &transaction->tool_stderr_snapshot)) {
    return 0;
  }
  transaction->tool_stdout_snapshot = standard_output;
  transaction->tool_stderr_snapshot = standard_error;
  return 1;
}

static int cupidbuild_host_prepare_private_candidate(
    cupidbuild_host_transaction_t *transaction) {
  static const unsigned char empty[1] = {0u};
  if (transaction->candidate_snapshot.present != 0) {
#if defined(_WIN32)
    return transaction->candidate_handle != INVALID_HANDLE_VALUE &&
           cupidbuild_host_private_entry_path(
               transaction, transaction->candidate,
               sizeof(transaction->candidate), "candidate.o");
#else
    return transaction->candidate_descriptor >= 0 &&
           cupidbuild_host_private_entry_path(
               transaction, transaction->candidate,
               sizeof(transaction->candidate), "candidate.o");
#endif
  }
  return cupidbuild_host_write_private_exclusive(
             transaction, "candidate.o", empty, 0u, 0,
             &transaction->candidate_snapshot,
#if defined(_WIN32)
             &transaction->candidate_handle
#else
             &transaction->candidate_descriptor
#endif
             ) &&
         cupidbuild_host_private_entry_path(
             transaction, transaction->candidate,
             sizeof(transaction->candidate), "candidate.o");
}

static int cupidbuild_host_capture_retained_candidate(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t candidate;
#if defined(_WIN32)
  if (transaction->candidate_sealed == 0 &&
      !cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "candidate.o",
          &transaction->candidate_snapshot,
          &transaction->candidate_handle, 1, 0, 1)) {
    return 0;
  }
  transaction->candidate_sealed = 1;
#endif
  if (!cupidbuild_host_read_retained_private_regular(
          transaction, "candidate.o", CUPIDBUILD_HOST_FILE_LIMIT,
#if defined(_WIN32)
          transaction->candidate_handle,
#else
          transaction->candidate_descriptor,
#endif
          &candidate, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_identity_equal(
          &candidate, &transaction->candidate_snapshot) ||
      (transaction->candidate_captured != 0 &&
       !cupidbuild_host_snapshot_equal(
           &candidate, &transaction->candidate_snapshot))) {
    return 0;
  }
  transaction->candidate_snapshot = candidate;
  return 1;
}

static int cupidbuild_host_close_private_output_handle(
    cupidbuild_host_transaction_t *transaction) {
#if defined(_WIN32)
  if (transaction->private_output_handle != INVALID_HANDLE_VALUE) {
    int disposed = cupidbuild_host_windows_dispose_retained_at(
        transaction->private_handle, "candidate.map",
        &transaction->private_output_snapshot,
        &transaction->private_output_handle);
    transaction->private_output_sealed = 0;
    return disposed;
  }
#else
  if (transaction->private_output_descriptor >= 0) {
    int disposed = transaction->private_flat != 0
                       ? cupidbuild_host_close_retained_descriptor(
                             &transaction->private_output_descriptor)
                       : cupidbuild_host_cleanup_retained_regular_at(
                             transaction->private_descriptor,
                             cupidbuild_host_private_entry_name(
                                 transaction, "candidate.map"),
                             &transaction->private_output_snapshot,
                             &transaction->private_output_descriptor);
    transaction->private_output_sealed = 0;
    return disposed;
  }
#endif
  transaction->private_output_sealed = 0;
  return 1;
}

static int cupidbuild_host_prepare_private_output_stream(
    cupidbuild_host_transaction_t *transaction) {
  static const unsigned char empty[1] = {0u};
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    if (!cupidbuild_host_open_anonymous_artifact(
            "cupidbuild-output", &transaction->private_output_snapshot,
            &transaction->private_output_descriptor) ||
        !cupidbuild_host_private_entry_path(
            transaction, transaction->private_output,
            sizeof(transaction->private_output), "candidate.map")) {
      (void)cupidbuild_host_close_retained_descriptor(
          &transaction->private_output_descriptor);
      (void)memset(&transaction->private_output_snapshot, 0,
                   sizeof(transaction->private_output_snapshot));
      return 0;
    }
    return 1;
  }
#endif
  return cupidbuild_host_write_private_exclusive(
             transaction, "candidate.map", empty, 0u, 1,
             &transaction->private_output_snapshot,
#if defined(_WIN32)
             &transaction->private_output_handle
#else
             &transaction->private_output_descriptor
#endif
             ) &&
         cupidbuild_host_private_entry_path(
             transaction, transaction->private_output,
             sizeof(transaction->private_output), "candidate.map");
}

static int cupidbuild_host_prepare_private_output_path(
    cupidbuild_host_transaction_t *transaction) {
  static const unsigned char empty[1] = {0u};
  if (transaction->private_output_snapshot.present != 0) {
#if defined(_WIN32)
    return transaction->private_output_handle != INVALID_HANDLE_VALUE &&
           cupidbuild_host_private_entry_path(
               transaction, transaction->private_output,
               sizeof(transaction->private_output), "candidate.map");
#else
    return transaction->private_output_descriptor >= 0 &&
           cupidbuild_host_private_entry_path(
               transaction, transaction->private_output,
               sizeof(transaction->private_output), "candidate.map");
#endif
  }
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    return cupidbuild_host_prepare_private_output_stream(transaction);
  }
#endif
  return cupidbuild_host_write_private_exclusive(
             transaction, "candidate.map", empty, 0u, 0,
             &transaction->private_output_snapshot,
#if defined(_WIN32)
             &transaction->private_output_handle
#else
             &transaction->private_output_descriptor
#endif
             ) &&
         cupidbuild_host_private_entry_path(
             transaction, transaction->private_output,
             sizeof(transaction->private_output), "candidate.map");
}

static int cupidbuild_host_capture_retained_private_output(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t output;
#if defined(_WIN32)
  if (transaction->private_output_sealed == 0 &&
      !cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "candidate.map",
          &transaction->private_output_snapshot,
          &transaction->private_output_handle, 1, 0, 1)) {
    return 0;
  }
  transaction->private_output_sealed = 1;
#endif
#if !defined(_WIN32)
  if (transaction->private_flat != 0 &&
      transaction->private_output_sealed == 0) {
    if (!cupidbuild_host_seal_anonymous(
            transaction->private_output_descriptor)) {
      return 0;
    }
    transaction->private_output_sealed = 1;
  }
#endif
  if (!cupidbuild_host_read_retained_private_regular(
          transaction, "candidate.map", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
          transaction->private_output_handle,
#else
          transaction->private_output_descriptor,
#endif
          &output, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_identity_equal(
          &output, &transaction->private_output_snapshot) ||
      (transaction->private_output_captured != 0 &&
       !cupidbuild_host_snapshot_equal(
           &output, &transaction->private_output_snapshot))) {
    return 0;
  }
  transaction->private_output_snapshot = output;
  return 1;
}

const char *cupidbuild_host_profile_parent_error(
    const cupidbuild_host_profile_parent_t *preparation) {
  if (preparation == (const cupidbuild_host_profile_parent_t *)0 ||
      preparation->error[0] == '\0') {
    return "profile parent preparation could not be allocated";
  }
  return preparation->error;
}

int cupidbuild_host_seed_members_exact(
                                       cupidbuild_host_transaction_t *transaction,
                                       const char *directory,
                                       const char *suffix,
                                       const char *const *expected,
                                       size_t expected_count) {
  const char *logical_directory;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      directory == (const char *)0 || directory[0] == '\0' ||
      suffix == (const char *)0 || suffix[0] == '\0' ||
      expected == (const char *const *)0 || expected_count == 0u) {
    return 0;
  }
  if (transaction->runner_transaction != 0) {
    return cupidbuild_host_seed_members_platform(directory, suffix, expected,
                                                  expected_count);
  }
  return cupidbuild_host_repository_logical_path(
             transaction, directory, &logical_directory) &&
         cupidbuild_host_seed_members_repository(
             transaction, logical_directory, suffix, expected,
             expected_count);
}

int cupidbuild_host_discover_files(
    cupidbuild_host_transaction_t *transaction,
    const char *const *logical_roots,
    size_t root_count, const char *const *suffixes, size_t suffix_count,
    int skip_hidden_files, int reject_matching_nonfiles,
    cupidbuild_host_path_list_t *paths_out) {
  size_t index;
  if (paths_out == (cupidbuild_host_path_list_t *)0) {
    return 0;
  }
  (void)memset(paths_out, 0, sizeof(*paths_out));
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 ||
      logical_roots == (const char *const *)0 || root_count == 0u ||
      suffixes == (const char *const *)0 || suffix_count == 0u) {
    return 0;
  }
  for (index = 0u; index < root_count; index++) {
    if (!cupidbuild_host_path_is_relative_safe(logical_roots[index]) ||
        !cupidbuild_host_discover_platform(
            transaction, logical_roots[index], suffixes, suffix_count,
            skip_hidden_files, reject_matching_nonfiles, paths_out)) {
      cupidbuild_host_path_list_close(paths_out);
      return 0;
    }
  }
  if (cupidbuild_host_discovery_sort(paths_out)) {
    return 1;
  }
  cupidbuild_host_path_list_close(paths_out);
  return 0;
}

int cupidbuild_host_seal_discovery(
    cupidbuild_host_transaction_t *transaction) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 ||
      transaction->discovery_directory_count == 0u) {
    return 0;
  }
  transaction->discovery_sealed = 1;
  return 1;
}

#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_profile_directory_test_pause(
    const char *ready_variable, const char *resume_variable) {
  const char *ready = getenv(ready_variable);
  const char *resume = getenv(resume_variable);
  FILE *signal;
  unsigned int attempt;
  if (ready == (const char *)0 || resume == (const char *)0) {
    return 1;
  }
  signal = fopen(ready, "wb");
  if (signal == (FILE *)0 || fclose(signal) != 0) {
    return 0;
  }
  for (attempt = 0u; attempt < 30000u; attempt++) {
    FILE *permission = fopen(resume, "rb");
    if (permission != (FILE *)0) {
      return fclose(permission) == 0;
    }
#if defined(_WIN32)
    Sleep(1u);
#else
    {
      struct timespec pause_time;
      pause_time.tv_sec = 0;
      pause_time.tv_nsec = 1000000L;
      (void)nanosleep(&pause_time, (struct timespec *)0);
    }
#endif
  }
  return 0;
}
#endif

static int cupidbuild_host_require_discovery_directory(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_discovery_directory_t *directory) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t named;
#if defined(_WIN32)
  HANDLE opened;
  if (!cupidbuild_host_windows_named_directory_snapshot(
          transaction, directory->logical, directory->handle,
          &retained, (int *)0)) {
    return 0;
  }
  opened = cupidbuild_host_windows_open_relative_path(
      transaction->repository_root_handle, directory->logical, 1, 1);
  if (!cupidbuild_host_windows_named_directory_snapshot(
          transaction, directory->logical, opened, &named, (int *)0)) {
    if (opened != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(opened);
    }
    return 0;
  }
  if (!CloseHandle(opened)) {
    return 0;
  }
#else
  int opened;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (!cupidbuild_linux_descriptor_snapshot(directory->descriptor,
                                             &retained)) {
    return 0;
  }
  opened = cupidbuild_linux_open_relative_path(
      transaction->repository_root_descriptor, directory->logical, 1);
  if (!cupidbuild_linux_descriptor_snapshot(opened, &named)) {
    if (opened >= 0) {
      (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                 (unsigned int)opened);
    }
    return 0;
  }
  if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                           (unsigned int)opened) < 0) {
    return 0;
  }
#else
  if (!cupidbuild_native_directory_descriptor_snapshot(
          directory->descriptor, &retained)) {
    return 0;
  }
  opened = cupidbuild_native_open_relative_path(
      transaction->repository_root_descriptor, directory->logical, 1);
  if (!cupidbuild_native_directory_descriptor_snapshot(opened, &named)) {
    if (opened >= 0) {
      (void)close(opened);
    }
    return 0;
  }
  if (close(opened) != 0) {
    return 0;
  }
#endif
#endif
  if (!cupidbuild_host_snapshot_equal(&directory->snapshot, &retained) ||
      !cupidbuild_host_snapshot_equal(&directory->snapshot, &named)) {
    return 0;
  }
  return 1;
}

static int cupidbuild_host_require_discovery_directory_pass(
    cupidbuild_host_transaction_t *transaction) {
  size_t index;
  for (index = 0u; index < transaction->discovery_directory_count; index++) {
    if (!cupidbuild_host_require_discovery_directory(
            transaction, &transaction->discovery_directories[index])) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_host_require_discovery_directories(
    cupidbuild_host_transaction_t *transaction) {
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  transaction->discovery_boundary_count++;
  if (transaction->discovery_boundary_count == 2u &&
      !cupidbuild_host_profile_directory_test_pause(
          "CUPIDBUILD_PROFILE_TEST_DIRECTORY_READY",
          "CUPIDBUILD_PROFILE_TEST_DIRECTORY_RESUME")) {
    return 0;
  }
#endif
  if (transaction->discovery_directory_count != 0u &&
      transaction->discovery_sealed == 0) {
    return 0;
  }
  if (!cupidbuild_host_require_discovery_directory_pass(transaction)) {
    return 0;
  }
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  if (transaction->discovery_boundary_count == 2u &&
      !cupidbuild_host_profile_directory_test_pause(
          "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_READY",
          "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_RESUME")) {
    return 0;
  }
#endif
  return cupidbuild_host_require_discovery_directory_pass(transaction);
}

static int cupidbuild_host_write_lock_exclusive(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size, cupidbuild_host_snapshot_t *snapshot) {
#if defined(_WIN32)
  return cupidbuild_host_windows_create_lock(transaction, bytes, size,
                                              snapshot);
#else
  return cupidbuild_host_write_lock_exclusive_at(transaction, bytes, size,
                                                  snapshot);
#endif
}

static int cupidbuild_host_read_lock(
    const cupidbuild_host_transaction_t *transaction, int optional,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
#if defined(_WIN32)
  return cupidbuild_host_windows_read_relative_regular(
      transaction->output_parent_handle, transaction->lock_name, optional,
      CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  return cupidbuild_linux_read_relative_regular(
      transaction->output_parent_descriptor, transaction->lock_name,
      optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
  return cupidbuild_native_read_relative_regular(
      transaction->output_parent_descriptor, transaction->lock_name,
      optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#endif
#endif
}

static int cupidbuild_host_reclaim_lock(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected, unsigned int owner) {
#if defined(_WIN32)
  (void)owner;
  return cupidbuild_host_windows_reclaim_lock(transaction, expected);
#else
  return cupidbuild_host_reclaim_lock_at(transaction, expected, owner);
#endif
}

static int cupidbuild_host_read_owner(
    cupidbuild_host_transaction_t *transaction,
                                      unsigned int *owner_out,
                                      cupidbuild_host_snapshot_t *snapshot_out) {
  cupidbuild_host_snapshot_t snapshot;
  unsigned char *bytes = (unsigned char *)0;
  unsigned int owner = 0u;
  size_t index;
  if (!cupidbuild_host_read_lock(transaction, 0, &snapshot, &bytes) ||
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
  unsigned int attempt;
  unsigned int process_id = cupidbuild_host_process_id();
  int owner_written = snprintf(owner_text, sizeof(owner_text), "%u\n",
                               process_id);
  if (owner_written <= 0 ||
      (size_t)owner_written >= sizeof(owner_text)) {
    return 0;
  }
  for (attempt = 0u; attempt < 4u; attempt++) {
    if (cupidbuild_host_write_lock_exclusive(
            transaction, (const unsigned char *)owner_text,
            (size_t)owner_written, &transaction->lock_snapshot)) {
      transaction->lock_held = 1;
      return 1;
    }
    {
      unsigned int owner = 0u;
      cupidbuild_host_snapshot_t stale_snapshot;
      if (!cupidbuild_host_read_owner(transaction, &owner, &stale_snapshot)) {
        cupidbuild_host_set_error(transaction,
                                  "publication lock is not a regular owner file");
        return 0;
      }
      if (cupidbuild_host_process_alive(owner)) {
        cupidbuild_host_set_error(transaction,
                                  "publication lock is held by a live process");
        return 0;
      }
      if (!cupidbuild_host_reclaim_lock(transaction, &stale_snapshot, owner)) {
        cupidbuild_host_set_error(
            transaction, "publication lock could not enter stale recovery");
        return 0;
      }
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
         cupidbuild_host_read_lock(transaction, 0, &current,
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

static int cupidbuild_host_release_lock(
    cupidbuild_host_transaction_t *transaction) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->lock_held == 0) {
    return 1;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_release_lock(transaction)) {
    return 0;
  }
#else
  if (!cupidbuild_host_release_lock_at(transaction)) {
    return 0;
  }
#endif
  transaction->lock_held = 0;
  return 1;
}

static int cupidbuild_host_register_input(
    cupidbuild_host_transaction_t *transaction, const char *live_path,
    const char *frozen_path, const char *frozen_name,
    const cupidbuild_host_snapshot_t *snapshot) {
  cupidbuild_host_input_t *input;
  unsigned int index;
  for (index = 0u; index < transaction->input_count; index++) {
    if (cupidbuild_host_snapshot_identity_equal(
            &transaction->inputs[index].snapshot, snapshot)) {
      cupidbuild_host_set_error(
          transaction, "transaction inputs may not share a file identity");
      return 0;
    }
  }
  if (transaction->input_count >= CUPIDBUILD_HOST_INPUTS) {
    cupidbuild_host_set_error(transaction, "too many frozen transaction inputs");
    return 0;
  }
  if (transaction->input_count == transaction->input_capacity) {
    size_t capacity = transaction->input_capacity == 0u
                          ? 16u
                          : (size_t)transaction->input_capacity * 2u;
    if (capacity > CUPIDBUILD_HOST_INPUTS) {
      capacity = CUPIDBUILD_HOST_INPUTS;
    }
    if (!cupidbuild_host_reserve_inputs(transaction, capacity)) {
      return 0;
    }
  }
  input = &transaction->inputs[transaction->input_count];
#if defined(_WIN32)
  input->frozen_handle = INVALID_HANDLE_VALUE;
#else
  input->frozen_descriptor = -1;
#endif
  if (!cupidbuild_host_copy_text(input->live_path, sizeof(input->live_path),
                                 live_path) ||
      !cupidbuild_host_copy_text(input->frozen_path,
                                 sizeof(input->frozen_path), frozen_path) ||
      !cupidbuild_host_copy_text(input->frozen_name,
                                 sizeof(input->frozen_name), frozen_name)) {
    cupidbuild_host_set_error(transaction, "transaction input path is too long");
    return 0;
  }
  input->snapshot = *snapshot;
  transaction->input_count++;
  return 1;
}

int cupidbuild_host_reserve_inputs(
    cupidbuild_host_transaction_t *transaction, size_t capacity) {
  cupidbuild_host_input_t *grown;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      capacity > CUPIDBUILD_HOST_INPUTS) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      cupidbuild_host_set_error(transaction,
                                "too many frozen transaction inputs");
    }
    return 0;
  }
  if (capacity <= transaction->input_capacity) {
    return 1;
  }
  grown = (cupidbuild_host_input_t *)realloc(
      transaction->inputs, capacity * sizeof(*grown));
  if (grown == (cupidbuild_host_input_t *)0) {
    cupidbuild_host_set_error(transaction,
                              "transaction input table cannot be allocated");
    return 0;
  }
  (void)memset(grown + transaction->input_capacity, 0,
               (capacity - transaction->input_capacity) * sizeof(*grown));
  transaction->inputs = grown;
  transaction->input_capacity = (unsigned int)capacity;
  return 1;
}

static int cupidbuild_host_repository_logical_path(
    const cupidbuild_host_transaction_t *transaction, const char *path,
    const char **logical_out) {
  size_t root_size;
  size_t index;
  const char *logical;
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      path == (const char *)0 || logical_out == (const char **)0) {
    return 0;
  }
  root_size = strlen(transaction->repository_root);
  if (root_size == 0u) {
    return 0;
  }
  for (index = 0u; index < root_size; index++) {
    char left = path[index];
    char right = transaction->repository_root[index];
#if defined(_WIN32)
    if (left == '\\') {
      left = '/';
    }
    if (right == '\\') {
      right = '/';
    }
    left = cupidbuild_host_ascii_fold(left);
    right = cupidbuild_host_ascii_fold(right);
#endif
    if (left == '\0' || left != right) {
      return 0;
    }
  }
  if (transaction->repository_root[root_size - 1u] == '/' ||
      transaction->repository_root[root_size - 1u] == '\\') {
    logical = path + root_size;
  } else if (path[root_size] == '/' || path[root_size] == '\\') {
    logical = path + root_size + 1u;
  } else {
    return 0;
  }
  if (!cupidbuild_host_path_is_relative_safe(logical)) {
    return 0;
  }
  *logical_out = logical;
  return 1;
}

static int cupidbuild_host_read_transaction_input(
    cupidbuild_host_transaction_t *transaction, const char *path,
    size_t limit, cupidbuild_host_snapshot_t *snapshot,
    unsigned char **bytes_out) {
  const char *logical;
  cupidbuild_host_snapshot_t root_snapshot;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  if (transaction->runner_transaction != 0) {
    return limit == CUPIDBUILD_HOST_FILE_LIMIT
               ? cupidbuild_host_read_regular(path, 0, snapshot, bytes_out)
               : 0;
  }
  if (!cupidbuild_host_repository_logical_path(transaction, path, &logical)) {
    return 0;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          transaction->repository_root_handle, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  return cupidbuild_host_windows_read_repository_regular(
      transaction->repository_root_handle, logical, limit, snapshot,
      bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (!cupidbuild_linux_descriptor_snapshot(
          transaction->repository_root_descriptor, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  return cupidbuild_linux_read_repository_regular(
      transaction->repository_root_descriptor, logical, limit, snapshot,
      bytes_out);
#else
  if (!cupidbuild_native_directory_descriptor_snapshot(
          transaction->repository_root_descriptor, &root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &root_snapshot)) {
    return 0;
  }
  return cupidbuild_native_read_repository_regular(
      transaction->repository_root_descriptor, logical, limit, snapshot,
      bytes_out);
#endif
#endif
}

int cupidbuild_host_input_matches_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *live_path,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  return transaction != (cupidbuild_host_transaction_t *)0 &&
         live_path != (const char *)0 &&
         expected != (const cupidbuild_host_snapshot_t *)0 &&
         cupidbuild_host_read_transaction_input(
             transaction, live_path, CUPIDBUILD_HOST_FILE_LIMIT, &current,
             (unsigned char **)0) &&
         cupidbuild_host_snapshot_equal(&current, expected);
}

static int cupidbuild_host_read_output(
    cupidbuild_host_transaction_t *transaction, int optional,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0) {
    return 0;
  }
#if defined(_WIN32)
  return cupidbuild_host_windows_read_relative_regular(
      transaction->output_parent_handle, transaction->output_name, optional,
      CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  return cupidbuild_linux_read_relative_regular(
      transaction->output_parent_descriptor, transaction->output_name,
      optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
  return cupidbuild_native_read_relative_regular(
      transaction->output_parent_descriptor, transaction->output_name,
      optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#endif
#endif
}

#if !defined(_WIN32)
static int cupidbuild_host_flat_entry_conflicts(
    const cupidbuild_host_transaction_t *transaction, const char *name,
    const char *additional_live_path) {
  char path[CUPIDBUILD_HOST_PATH_BYTES];
  unsigned int index;
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      name == (const char *)0 || name[0] == '\0' ||
      !cupidbuild_host_join(
          path, sizeof(path), transaction->repository_root, name) ||
      !cupidbuild_host_directory_entry_missing_at(
          transaction->repository_root_descriptor, name) ||
      strcmp(path, transaction->source_path) == 0 ||
      strcmp(path, transaction->output_path) == 0 ||
      strcmp(path, transaction->lock_path) == 0 ||
      (additional_live_path != (const char *)0 &&
       strcmp(path, additional_live_path) == 0)) {
    return 1;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if (strcmp(path, transaction->inputs[index].live_path) == 0 ||
        strcmp(path, transaction->inputs[index].frozen_path) == 0) {
      return 1;
    }
  }
  return 0;
}
#endif

#if defined(_WIN32)
static int cupidbuild_host_windows_pin_initial_output(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t named;
  long status = 0;
  unsigned long unsigned_status;
  HANDLE handle = cupidbuild_host_windows_open_relative_access_share_status(
      transaction->output_parent_handle, transaction->output_name, 0, 1,
      DELETE, FILE_SHARE_READ, &status);
  if (handle == INVALID_HANDLE_VALUE) {
    unsigned_status = (unsigned long)status;
    if (unsigned_status != 0xc000000fu &&
        unsigned_status != 0xc0000034u &&
        unsigned_status != 0xc000003au) {
      return 0;
    }
    return cupidbuild_host_windows_read_relative_regular(
               transaction->output_parent_handle, transaction->output_name,
               1, CUPIDBUILD_HOST_FILE_LIMIT,
               &transaction->initial_output_snapshot,
               (unsigned char **)0) &&
           transaction->initial_output_snapshot.present == 0;
  }
  if (!cupidbuild_host_windows_read_open_regular(
          handle, CUPIDBUILD_HOST_FILE_LIMIT, &retained,
          (unsigned char **)0) ||
      !cupidbuild_host_windows_read_relative_regular(
          transaction->output_parent_handle, transaction->output_name, 0,
          CUPIDBUILD_HOST_FILE_LIMIT, &named, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&retained, &named)) {
    (void)CloseHandle(handle);
    return 0;
  }
  transaction->initial_output_snapshot = retained;
  transaction->initial_output_handle = handle;
  return 1;
}
#endif

int cupidbuild_host_freeze_input(cupidbuild_host_transaction_t *transaction,
                                 const char *live_path,
                                 const char *private_name,
                                 const char **frozen_path_out,
                                 cupidbuild_host_snapshot_t *snapshot_out) {
  char frozen_path[CUPIDBUILD_HOST_PATH_BYTES];
  cupidbuild_host_snapshot_t snapshot;
  cupidbuild_host_snapshot_t frozen_snapshot;
  unsigned char *bytes = (unsigned char *)0;
#if defined(_WIN32)
  HANDLE frozen_handle = INVALID_HANDLE_VALUE;
#else
  int frozen_descriptor = -1;
#endif
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      private_name == (const char *)0 || strchr(private_name, '/') != 0 ||
      strchr(private_name, '\\') != 0 ||
      !cupidbuild_host_read_transaction_input(
          transaction, live_path, CUPIDBUILD_HOST_FILE_LIMIT, &snapshot,
          &bytes)) {
    cupidbuild_host_set_error(transaction,
                              "transaction input cannot be pinned and read");
    free(bytes);
    return 0;
  }
  if (transaction->runner_transaction != 0) {
#if defined(_WIN32)
    if (!cupidbuild_host_join(frozen_path, sizeof(frozen_path),
                              transaction->private_root, private_name) ||
        !cupidbuild_host_windows_create_relative_regular(
            transaction->private_handle, private_name, bytes, snapshot.size,
            0, &frozen_snapshot, &frozen_handle)) {
      cupidbuild_host_set_error(transaction,
                                "private frozen input cannot be created");
      free(bytes);
      return 0;
    }
#else
    if (!cupidbuild_host_write_anonymous(
            private_name, bytes, snapshot.size, frozen_path,
            sizeof(frozen_path), &frozen_snapshot, &frozen_descriptor)) {
      cupidbuild_host_set_error(transaction,
                                "anonymous frozen input cannot be created");
      free(bytes);
      return 0;
    }
#endif
  } else {
#if defined(_WIN32)
    if (!cupidbuild_host_private_entry_path(
            transaction, frozen_path, sizeof(frozen_path), private_name) ||
        !cupidbuild_host_write_private_exclusive(
            transaction, private_name, bytes, snapshot.size, 0,
            &frozen_snapshot, &frozen_handle)) {
      cupidbuild_host_set_error(transaction,
                                "private frozen input cannot be created");
      free(bytes);
      return 0;
    }
#else
    if (transaction->private_flat != 0) {
      int written;
      if (!cupidbuild_host_write_anonymous(
              private_name, bytes, snapshot.size, frozen_path,
              sizeof(frozen_path), &frozen_snapshot, &frozen_descriptor) ||
          !cupidbuild_host_promote_retained_descriptor(&frozen_descriptor)) {
        cupidbuild_host_set_error(
            transaction, "anonymous frozen input cannot be created");
        free(bytes);
        return 0;
      }
      written = snprintf(frozen_path, sizeof(frozen_path),
                         "/proc/self/fd/%d", frozen_descriptor);
      if (written < 0 || (size_t)written >= sizeof(frozen_path)) {
        cupidbuild_host_close_directory(frozen_descriptor);
        frozen_descriptor = -1;
        cupidbuild_host_set_error(
            transaction, "anonymous frozen input path cannot be rendered");
        free(bytes);
        return 0;
      }
    } else if (!cupidbuild_host_private_entry_path(
                   transaction, frozen_path, sizeof(frozen_path),
                   private_name) ||
               !cupidbuild_host_write_private_exclusive(
                   transaction, private_name, bytes, snapshot.size, 0,
                   &frozen_snapshot, &frozen_descriptor)) {
      cupidbuild_host_set_error(transaction,
                                "private frozen input cannot be created");
      free(bytes);
      return 0;
    }
#endif
  }
#if defined(_WIN32)
  if (transaction->runner_transaction == 0 ||
      frozen_handle != INVALID_HANDLE_VALUE) {
    if (!cupidbuild_host_windows_transition_retained_at(
            transaction->private_handle, private_name, &frozen_snapshot,
            &frozen_handle, 1, 1, 0)) {
      if (frozen_handle != INVALID_HANDLE_VALUE) {
        (void)cupidbuild_host_windows_dispose_read_retained_at(
            transaction->private_handle, private_name, &frozen_snapshot,
            &frozen_handle);
      }
      cupidbuild_host_set_error(
          transaction, "private frozen input cannot be pinned read-only");
      free(bytes);
      return 0;
    }
  }
#else
  if (transaction->runner_transaction == 0 &&
      transaction->private_flat == 0) {
    int created_descriptor = frozen_descriptor;
    cupidbuild_host_snapshot_t reopened_snapshot;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    frozen_descriptor = cupidbuild_linux_open_relative(
        transaction->private_descriptor, private_name, 0);
#else
    frozen_descriptor = cupidbuild_native_open_relative(
        transaction->private_descriptor, private_name, 0);
#endif
    if (frozen_descriptor < 0 ||
        !cupidbuild_host_promote_retained_descriptor(
            &frozen_descriptor) ||
        !cupidbuild_host_read_open_file(
            frozen_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
            &reopened_snapshot, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(
            &reopened_snapshot, &frozen_snapshot)) {
      cupidbuild_host_close_directory(frozen_descriptor);
      (void)cupidbuild_host_cleanup_retained_regular_at(
          transaction->private_descriptor, private_name, &frozen_snapshot,
          &created_descriptor);
      cupidbuild_host_set_error(
          transaction, "private frozen input cannot be pinned");
      free(bytes);
      return 0;
    }
    cupidbuild_host_close_directory(created_descriptor);
  }
#endif
  free(bytes);
  if (!cupidbuild_host_register_input(
          transaction, live_path, frozen_path, private_name, &snapshot)) {
#if defined(_WIN32)
    if (frozen_handle != INVALID_HANDLE_VALUE) {
      (void)cupidbuild_host_windows_dispose_read_retained_at(
          transaction->private_handle, private_name, &frozen_snapshot,
          &frozen_handle);
    } else
#else
    if (frozen_descriptor >= 0) {
      if (transaction->runner_transaction != 0 ||
          transaction->private_flat != 0) {
        cupidbuild_host_close_directory(frozen_descriptor);
        frozen_descriptor = -1;
      } else {
        (void)cupidbuild_host_cleanup_retained_regular_at(
            transaction->private_descriptor, private_name, &frozen_snapshot,
            &frozen_descriptor);
      }
    } else
#endif
    cupidbuild_host_delete_file(frozen_path);
    return 0;
  }
  transaction->inputs[transaction->input_count - 1u].frozen_snapshot =
      frozen_snapshot;
#if defined(_WIN32)
  transaction->inputs[transaction->input_count - 1u].frozen_handle =
      frozen_handle;
#else
  transaction->inputs[transaction->input_count - 1u].frozen_descriptor =
      frozen_descriptor;
#endif
  if (transaction->initial_output_snapshot.present != 0 &&
      memcmp(transaction->initial_output_snapshot.identity, snapshot.identity,
             sizeof(snapshot.identity)) == 0) {
    cupidbuild_host_set_error(transaction,
                              "output may not replace an input");
#if !defined(_WIN32)
    if (frozen_descriptor >= 0) {
      if (transaction->runner_transaction != 0 ||
          transaction->private_flat != 0) {
        cupidbuild_host_close_directory(frozen_descriptor);
        frozen_descriptor = -1;
      } else {
        (void)cupidbuild_host_cleanup_retained_regular_at(
            transaction->private_descriptor, private_name, &frozen_snapshot,
            &frozen_descriptor);
      }
    } else
#else
    if (frozen_handle != INVALID_HANDLE_VALUE) {
      (void)cupidbuild_host_windows_dispose_read_retained_at(
          transaction->private_handle, private_name, &frozen_snapshot,
          &frozen_handle);
    } else
#endif
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

unsigned char *cupidbuild_host_read_frozen_input(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path,
    size_t limit, size_t *size_out) {
  unsigned int index;
  cupidbuild_host_snapshot_t snapshot;
  unsigned char *bytes = (unsigned char *)0;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      frozen_path == (const char *)0 || size_out == (size_t *)0) {
    return (unsigned char *)0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if (strcmp(transaction->inputs[index].frozen_path, frozen_path) != 0) {
      continue;
    }
#if defined(_WIN32)
    if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE) {
      cupidbuild_host_snapshot_t named;
      int valid = transaction->runner_transaction != 0
                      ? cupidbuild_host_windows_read_open_regular(
                            transaction->inputs[index].frozen_handle, limit,
                            &snapshot, &bytes) &&
                            cupidbuild_host_read_regular_limit(
                                frozen_path, 0, limit, &named,
                                (unsigned char **)0) &&
                            cupidbuild_host_snapshot_equal(&snapshot, &named)
                      : cupidbuild_host_read_retained_private_regular(
                            transaction,
                            transaction->inputs[index].frozen_name,
                            limit,
                            transaction->inputs[index].frozen_handle,
                            &snapshot, &bytes);
      if (!valid ||
          !cupidbuild_host_snapshot_equal(
              &snapshot, &transaction->inputs[index].frozen_snapshot)) {
        free(bytes);
        return (unsigned char *)0;
      }
    } else
#else
    if (transaction->inputs[index].frozen_descriptor >= 0) {
      if (transaction->runner_transaction != 0) {
        if (!cupidbuild_host_read_open_file(
                transaction->inputs[index].frozen_descriptor, limit,
                &snapshot, &bytes)) {
          return (unsigned char *)0;
        }
      } else if (!cupidbuild_host_read_retained_private_regular(
                     transaction,
                     transaction->inputs[index].frozen_name,
                     limit,
                     transaction->inputs[index].frozen_descriptor,
                     &snapshot, &bytes) ||
                 !cupidbuild_host_snapshot_equal(
                     &snapshot,
                     &transaction->inputs[index].frozen_snapshot)) {
        free(bytes);
        return (unsigned char *)0;
      }
    } else
#endif
    if (!cupidbuild_host_read_regular(frozen_path, 0, &snapshot, &bytes) ||
        snapshot.size > limit) {
      free(bytes);
      return (unsigned char *)0;
    }
    *size_out = snapshot.size;
    return bytes;
  }
  return (unsigned char *)0;
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
#if !defined(_WIN32)
      if (transaction->inputs[index].frozen_descriptor >= 0) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        if (cupid_linux_syscall2(
                CUPIDBUILD_LINUX_SYS_FCHMOD,
                (unsigned int)transaction->inputs[index].frozen_descriptor,
                0700u) == 0) {
#else
        if (fchmod(transaction->inputs[index].frozen_descriptor, 0700) == 0) {
#endif
          return 1;
        }
      } else
#endif
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

static int cupidbuild_host_transaction_open_internal(
    const char *repository_root, const char *source_logical,
    const char *output_logical,
    cupidbuild_host_profile_parent_t *profile_parent,
    cupidbuild_host_transaction_t **transaction_out) {
  cupidbuild_host_transaction_t *transaction;
  cupidbuild_host_snapshot_t created_private_snapshot;
  cupidbuild_host_snapshot_t opened_private_snapshot;
  cupidbuild_host_snapshot_t live_private_snapshot;
  unsigned int attempt;
  const char *frozen = (const char *)0;
  int lock_written;
  size_t lock_capacity;
  if (transaction_out == (cupidbuild_host_transaction_t **)0) {
    return 0;
  }
  *transaction_out = (cupidbuild_host_transaction_t *)0;
  transaction = (cupidbuild_host_transaction_t *)calloc(1u, sizeof(*transaction));
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  (void)memset(&created_private_snapshot, 0,
               sizeof(created_private_snapshot));
  (void)memset(&opened_private_snapshot, 0,
               sizeof(opened_private_snapshot));
  (void)memset(&live_private_snapshot, 0,
               sizeof(live_private_snapshot));
#if defined(_WIN32)
  transaction->repository_root_handle = INVALID_HANDLE_VALUE;
  transaction->output_parent_handle = INVALID_HANDLE_VALUE;
  transaction->initial_output_handle = INVALID_HANDLE_VALUE;
  transaction->lock_handle = INVALID_HANDLE_VALUE;
  transaction->candidate_handle = INVALID_HANDLE_VALUE;
  transaction->private_output_handle = INVALID_HANDLE_VALUE;
  transaction->tool_stdout_handle = INVALID_HANDLE_VALUE;
  transaction->tool_stderr_handle = INVALID_HANDLE_VALUE;
  transaction->private_handle = INVALID_HANDLE_VALUE;
  transaction->working_directory_handle = INVALID_HANDLE_VALUE;
#else
  transaction->repository_root_descriptor = -1;
  transaction->output_parent_descriptor = -1;
  transaction->working_directory_descriptor = -1;
  transaction->private_reservation_descriptor = -1;
  transaction->candidate_descriptor = -1;
  transaction->private_output_descriptor = -1;
  transaction->private_descriptor = -1;
  transaction->tool_stdout_descriptor = -1;
  transaction->tool_stderr_descriptor = -1;
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
          repository_root, &transaction->repository_root_snapshot) ||
      !cupidbuild_host_directory_snapshot(
          transaction->output_parent, &transaction->output_parent_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "guarded artifact paths are invalid");
    *transaction_out = transaction;
    return 0;
  }
#if defined(_WIN32)
  {
  cupidbuild_host_snapshot_t open_root_snapshot;
  transaction->repository_root_handle =
      cupidbuild_host_windows_open_repository(repository_root);
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          transaction->repository_root_handle,
          &open_root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &open_root_snapshot)) {
    cupidbuild_host_set_error(transaction, "repository root cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  }
#else
  {
  cupidbuild_host_snapshot_t open_root_snapshot;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  transaction->repository_root_descriptor =
      cupidbuild_linux_open_repository(repository_root);
  if (transaction->repository_root_descriptor >= 0 &&
      transaction->repository_root_descriptor <= 2) {
    int original = transaction->repository_root_descriptor;
    int duplicated =
        cupidbuild_host_linux_duplicate_above_standard(original);
    (void)cupid_linux_syscall1(
        CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)original);
    transaction->repository_root_descriptor = duplicated;
    if (duplicated > 2) {
      if (cupid_linux_syscall3(
              CUPIDBUILD_LINUX_SYS_FCNTL64,
              (unsigned int)transaction->repository_root_descriptor,
              CUPIDBUILD_LINUX_F_SETFD,
              CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
        cupidbuild_host_close_directory(
            transaction->repository_root_descriptor);
        transaction->repository_root_descriptor = -1;
      }
    }
  }
  if (!cupidbuild_linux_descriptor_snapshot(
          transaction->repository_root_descriptor,
          &open_root_snapshot) ||
#else
  transaction->repository_root_descriptor =
      cupidbuild_native_open_repository(repository_root);
  if (transaction->repository_root_descriptor >= 0 &&
      transaction->repository_root_descriptor <= STDERR_FILENO) {
    int original = transaction->repository_root_descriptor;
    int duplicated =
        cupidbuild_host_posix_duplicate_above_standard(original);
    (void)close(original);
    transaction->repository_root_descriptor = duplicated;
    if (duplicated > STDERR_FILENO) {
      if (fcntl(transaction->repository_root_descriptor, F_SETFD,
                FD_CLOEXEC) != 0) {
        cupidbuild_host_close_directory(
            transaction->repository_root_descriptor);
        transaction->repository_root_descriptor = -1;
      }
    }
  }
  if (!cupidbuild_native_directory_descriptor_snapshot(
          transaction->repository_root_descriptor,
          &open_root_snapshot) ||
#endif
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->repository_root_snapshot, &open_root_snapshot)) {
    cupidbuild_host_set_error(transaction, "repository root cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  }
#endif
  lock_capacity = sizeof(transaction->lock_name);
  lock_written = snprintf(transaction->lock_name, lock_capacity,
                          "%s.cupidbuild.lock", transaction->output_name);
  if (lock_written < 0 || (size_t)lock_written >= lock_capacity ||
      !cupidbuild_host_join(transaction->lock_path,
                            sizeof(transaction->lock_path),
                            transaction->output_parent,
                            transaction->lock_name)) {
    cupidbuild_host_set_error(transaction,
                              "guarded artifact paths are invalid");
    *transaction_out = transaction;
    return 0;
  }
#if defined(_WIN32)
  {
    const char *parent_logical;
    cupidbuild_host_snapshot_t open_parent_snapshot;
  if (strcmp(transaction->output_parent, transaction->repository_root) == 0) {
    transaction->output_parent_handle = transaction->repository_root_handle;
    transaction->output_parent_is_repository_root = 1;
  } else {
    if (!cupidbuild_host_repository_logical_path(
            transaction, transaction->output_parent, &parent_logical)) {
      cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
      *transaction_out = transaction;
      return 0;
    }
    transaction->output_parent_handle =
        cupidbuild_host_windows_open_relative_path_access(
            transaction->repository_root_handle, parent_logical, 1, 1,
            FILE_ADD_FILE | FILE_DELETE_CHILD);
  }
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          transaction->output_parent_handle, &open_parent_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->output_parent_snapshot, &open_parent_snapshot)) {
    cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  }
#else
  {
    const char *parent_logical;
    cupidbuild_host_snapshot_t open_parent_snapshot;
  if (strcmp(transaction->output_parent, transaction->repository_root) == 0) {
    transaction->output_parent_descriptor =
        transaction->repository_root_descriptor;
    transaction->output_parent_is_repository_root = 1;
  } else if (!cupidbuild_host_repository_logical_path(
                 transaction, transaction->output_parent,
                 &parent_logical)) {
    cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  if (transaction->output_parent_is_repository_root == 0) {
    transaction->output_parent_descriptor =
        cupidbuild_linux_open_relative_path(
            transaction->repository_root_descriptor, parent_logical, 1);
  }
  if (!cupidbuild_linux_descriptor_snapshot(
          transaction->output_parent_descriptor, &open_parent_snapshot) ||
#else
  if (transaction->output_parent_is_repository_root == 0) {
    transaction->output_parent_descriptor =
        cupidbuild_native_open_relative_path(
            transaction->repository_root_descriptor, parent_logical, 1);
  }
  if (!cupidbuild_native_directory_descriptor_snapshot(
          transaction->output_parent_descriptor, &open_parent_snapshot) ||
#endif
      !cupidbuild_host_snapshot_identity_equal(
          &transaction->output_parent_snapshot, &open_parent_snapshot)) {
    cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  }
#endif
  if (profile_parent != (cupidbuild_host_profile_parent_t *)0 &&
      !cupidbuild_host_profile_parent_bind(profile_parent, transaction)) {
    *transaction_out = transaction;
    return 0;
  }
  if (!cupidbuild_host_acquire_lock(transaction)) {
    *transaction_out = transaction;
    return 0;
  }
  for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
#if defined(_WIN32)
    int written = snprintf(transaction->private_name,
                           sizeof(transaction->private_name),
                           ".cupidbuild-object-%08x", attempt);
    if (written < 0 || (size_t)written >= sizeof(transaction->private_name) ||
        !cupidbuild_host_join(transaction->private_root,
                              sizeof(transaction->private_root),
                              repository_root, transaction->private_name)) {
      break;
    }
    transaction->private_handle =
        cupidbuild_host_profile_parent_open_component(
            transaction->repository_root_handle, transaction->private_name,
            1, 1);
    if (transaction->private_handle != INVALID_HANDLE_VALUE) {
      transaction->private_created = 1;
      (void)cupidbuild_host_windows_directory_handle_snapshot(
          transaction->private_handle, &created_private_snapshot);
      break;
    }
#else
    int prefix_written = snprintf(
        transaction->private_prefix, sizeof(transaction->private_prefix),
        ".cupidbuild-object-%08x", attempt);
    int reservation_written;
    int candidate_written;
    int publish_written;
    int output_written;
    int stdout_written;
    int stderr_written;
    if (prefix_written < 0 ||
        (size_t)prefix_written >= sizeof(transaction->private_prefix)) {
      break;
    }
    reservation_written = snprintf(
        transaction->private_name, sizeof(transaction->private_name),
        "%s.reserve", transaction->private_prefix);
    candidate_written = snprintf(
        transaction->candidate_name, sizeof(transaction->candidate_name),
        "%s.candidate.o", transaction->private_prefix);
    publish_written = snprintf(
        transaction->candidate_publish_name,
        sizeof(transaction->candidate_publish_name),
        "%s.candidate.publish", transaction->private_prefix);
    output_written = snprintf(
        transaction->private_output_name,
        sizeof(transaction->private_output_name),
        "%s.candidate.map", transaction->private_prefix);
    stdout_written = snprintf(
        transaction->tool_stdout_name,
        sizeof(transaction->tool_stdout_name),
        "%s.tool.stdout", transaction->private_prefix);
    stderr_written = snprintf(
        transaction->tool_stderr_name,
        sizeof(transaction->tool_stderr_name),
        "%s.tool.stderr", transaction->private_prefix);
    if (reservation_written < 0 || candidate_written < 0 ||
        publish_written < 0 || output_written < 0 || stdout_written < 0 ||
        stderr_written < 0 ||
        (size_t)reservation_written >= sizeof(transaction->private_name) ||
        (size_t)candidate_written >= sizeof(transaction->candidate_name) ||
        (size_t)publish_written >=
            sizeof(transaction->candidate_publish_name) ||
        (size_t)output_written >=
            sizeof(transaction->private_output_name) ||
        (size_t)stdout_written >= sizeof(transaction->tool_stdout_name) ||
        (size_t)stderr_written >= sizeof(transaction->tool_stderr_name) ||
        !cupidbuild_host_copy_text(
            transaction->private_root, sizeof(transaction->private_root),
            transaction->repository_root)) {
      break;
    }
    if (cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->private_name, (const char *)0) ||
        cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->candidate_name, (const char *)0) ||
        cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->candidate_publish_name,
            (const char *)0) ||
        cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->private_output_name,
            (const char *)0) ||
        cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->tool_stdout_name,
            (const char *)0) ||
        cupidbuild_host_flat_entry_conflicts(
            transaction, transaction->tool_stderr_name,
            (const char *)0)) {
      continue;
    }
    if (cupidbuild_host_write_exclusive_at(
            transaction->repository_root_descriptor,
            transaction->private_name, (const unsigned char *)"", 0u, 0,
            &created_private_snapshot,
            &transaction->private_reservation_descriptor)) {
      transaction->private_created = 1;
      transaction->private_flat = 1;
      transaction->private_descriptor =
          transaction->repository_root_descriptor;
      break;
    }
#endif
  }
#if defined(_WIN32)
  if (transaction->private_created == 0 ||
      created_private_snapshot.present == 0 ||
      transaction->private_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->private_handle, &opened_private_snapshot) ||
      !cupidbuild_host_directory_snapshot(
          transaction->private_root, &live_private_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &created_private_snapshot, &opened_private_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &created_private_snapshot, &live_private_snapshot)) {
    cupidbuild_host_set_error(
        transaction, "private artifact transaction cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  transaction->private_root_snapshot = created_private_snapshot;
  if (!cupidbuild_host_copy_text(
          transaction->candidate_name,
          sizeof(transaction->candidate_name), "candidate.o") ||
      !cupidbuild_host_copy_text(
          transaction->candidate_publish_name,
          sizeof(transaction->candidate_publish_name),
          CUPIDBUILD_HOST_CANDIDATE_PUBLISH) ||
      !cupidbuild_host_copy_text(
          transaction->private_output_name,
          sizeof(transaction->private_output_name), "candidate.map") ||
      !cupidbuild_host_copy_text(
          transaction->tool_stdout_name,
          sizeof(transaction->tool_stdout_name), "tool.stdout") ||
      !cupidbuild_host_copy_text(
          transaction->tool_stderr_name,
          sizeof(transaction->tool_stderr_name), "tool.stderr")) {
    cupidbuild_host_set_error(
        transaction, "private artifact names cannot be prepared");
    *transaction_out = transaction;
    return 0;
  }
#else
  transaction->working_directory_descriptor =
      cupidbuild_host_open_directory("/proc");
  if (transaction->private_created == 0 ||
      transaction->private_flat == 0 ||
      transaction->private_descriptor < 0 ||
      transaction->working_directory_descriptor < 0 ||
      transaction->private_reservation_descriptor < 0 ||
      created_private_snapshot.present == 0 ||
      !cupidbuild_host_read_open_file(
          transaction->private_reservation_descriptor,
          CUPIDBUILD_HOST_FILE_LIMIT, &opened_private_snapshot,
          (unsigned char **)0) ||
      !cupidbuild_host_regular_snapshot_at(
          transaction->repository_root_descriptor,
          transaction->private_name, &live_private_snapshot) ||
      !cupidbuild_host_snapshot_equal(
          &created_private_snapshot, &opened_private_snapshot) ||
      !cupidbuild_host_snapshot_equal(
          &created_private_snapshot, &live_private_snapshot)) {
    cupidbuild_host_set_error(
        transaction, "private artifact reservation cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  transaction->private_reservation_snapshot = created_private_snapshot;
  transaction->private_root_snapshot = transaction->repository_root_snapshot;
#endif
#if defined(_WIN32)
  if (!cupidbuild_host_private_entry_path(
          transaction, transaction->candidate,
          sizeof(transaction->candidate), "candidate.o") ||
      !cupidbuild_host_private_entry_path(
          transaction, transaction->private_output,
          sizeof(transaction->private_output), "candidate.map") ||
      !cupidbuild_host_private_entry_path(
          transaction, transaction->tool_stdout,
          sizeof(transaction->tool_stdout), "tool.stdout") ||
      !cupidbuild_host_private_entry_path(
          transaction, transaction->tool_stderr,
          sizeof(transaction->tool_stderr), "tool.stderr")) {
    cupidbuild_host_set_error(
        transaction, "private artifact paths cannot be prepared");
    *transaction_out = transaction;
    return 0;
  }
#endif
  if (!cupidbuild_host_freeze_input(transaction, transaction->source_path,
                                    "source.asm", &frozen,
                                    (cupidbuild_host_snapshot_t *)0) ||
      !cupidbuild_host_copy_text(transaction->frozen_source,
                                 sizeof(transaction->frozen_source), frozen) ||
#if defined(_WIN32)
      !cupidbuild_host_windows_pin_initial_output(transaction)) {
#else
      !cupidbuild_host_read_output(
          transaction, 1, &transaction->initial_output_snapshot,
          (unsigned char **)0)) {
#endif
    if (transaction->error[0] == '\0') {
      cupidbuild_host_set_error(transaction,
                                "private artifact transaction cannot be opened");
    }
    *transaction_out = transaction;
    return 0;
  }
#if !defined(_WIN32)
  if (!cupidbuild_host_prepare_private_candidate(transaction) ||
      !cupidbuild_host_prepare_private_output_path(transaction)) {
    cupidbuild_host_set_error(
        transaction, "private artifact files cannot be prepared");
    *transaction_out = transaction;
    return 0;
  }
#endif
  if (transaction->initial_output_snapshot.present != 0 &&
      memcmp(transaction->initial_output_snapshot.identity,
             transaction->inputs[0].snapshot.identity,
             sizeof(transaction->initial_output_snapshot.identity)) == 0) {
    cupidbuild_host_set_error(transaction,
                              "output may not replace an input");
    *transaction_out = transaction;
    return 0;
  }
  *transaction_out = transaction;
  return 1;
}

int cupidbuild_host_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical,
    cupidbuild_host_transaction_t **transaction_out) {
  return cupidbuild_host_transaction_open_internal(
      repository_root, source_logical, output_logical,
      (cupidbuild_host_profile_parent_t *)0, transaction_out);
}

int cupidbuild_host_profile_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical,
    cupidbuild_host_profile_parent_t *profile_parent,
    cupidbuild_host_transaction_t **transaction_out) {
  return cupidbuild_host_transaction_open_internal(
      repository_root, source_logical, output_logical, profile_parent,
      transaction_out);
}

int cupidbuild_host_runner_open(
    const char *working_directory,
    cupidbuild_host_transaction_t **transaction_out) {
  cupidbuild_host_transaction_t *transaction;
#if defined(_WIN32)
  cupidbuild_host_snapshot_t handle_snapshot;
  cupidbuild_host_snapshot_t private_handle_snapshot;
  static const unsigned char empty[1] = {0u};
  unsigned int attempt;
#else
  cupidbuild_host_snapshot_t descriptor_snapshot;
#endif
  if (transaction_out == (cupidbuild_host_transaction_t **)0) {
    return 0;
  }
  *transaction_out = (cupidbuild_host_transaction_t *)0;
  transaction =
      (cupidbuild_host_transaction_t *)calloc(1u, sizeof(*transaction));
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  transaction->runner_transaction = 1;
#if defined(_WIN32)
  transaction->repository_root_handle = INVALID_HANDLE_VALUE;
  transaction->output_parent_handle = INVALID_HANDLE_VALUE;
  transaction->initial_output_handle = INVALID_HANDLE_VALUE;
  transaction->lock_handle = INVALID_HANDLE_VALUE;
  transaction->candidate_handle = INVALID_HANDLE_VALUE;
  transaction->private_output_handle = INVALID_HANDLE_VALUE;
  transaction->tool_stdout_handle = INVALID_HANDLE_VALUE;
  transaction->tool_stderr_handle = INVALID_HANDLE_VALUE;
  transaction->private_handle = INVALID_HANDLE_VALUE;
  transaction->working_directory_handle = INVALID_HANDLE_VALUE;
#else
  transaction->repository_root_descriptor = -1;
  transaction->output_parent_descriptor = -1;
  transaction->working_directory_descriptor = -1;
  transaction->private_reservation_descriptor = -1;
  transaction->candidate_descriptor = -1;
  transaction->private_output_descriptor = -1;
  transaction->private_descriptor = -1;
  transaction->tool_stdout_descriptor = -1;
  transaction->tool_stderr_descriptor = -1;
#endif
  if (!cupidbuild_host_absolute_directory(
          transaction->repository_root, sizeof(transaction->repository_root),
          working_directory) ||
      !cupidbuild_host_directory_snapshot(transaction->repository_root,
                                          &transaction->repository_root_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool working directory is invalid");
    *transaction_out = transaction;
    return 0;
  }
#if !defined(_WIN32)
  transaction->output_parent_descriptor =
      cupidbuild_host_open_directory(transaction->repository_root);
  if (transaction->output_parent_descriptor < 0 ||
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->output_parent_descriptor, &descriptor_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &descriptor_snapshot, &transaction->repository_root_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool working directory cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  if (!cupidbuild_host_open_anonymous(
          "cupidbuild-stdout", transaction->tool_stdout,
          sizeof(transaction->tool_stdout),
          &transaction->tool_stdout_descriptor) ||
      !cupidbuild_host_promote_retained_descriptor(
          &transaction->tool_stdout_descriptor) ||
      !cupidbuild_host_open_anonymous(
          "cupidbuild-stderr", transaction->tool_stderr,
          sizeof(transaction->tool_stderr),
          &transaction->tool_stderr_descriptor) ||
      !cupidbuild_host_promote_retained_descriptor(
          &transaction->tool_stderr_descriptor)) {
    cupidbuild_host_set_error(transaction,
                              "anonymous checked-tool streams cannot be opened");
    *transaction_out = transaction;
    return 0;
  }
  *transaction_out = transaction;
  return 1;
#endif
#if defined(_WIN32)
  transaction->working_directory_handle = CreateFileA(
      transaction->repository_root,
      FILE_READ_ATTRIBUTES | FILE_TRAVERSE | FILE_ADD_SUBDIRECTORY |
          FILE_LIST_DIRECTORY | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)0,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      (HANDLE)0);
  if (transaction->working_directory_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->working_directory_handle, &handle_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &handle_snapshot, &transaction->repository_root_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool working directory cannot be pinned");
    *transaction_out = transaction;
    return 0;
  }
  for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
    int written = snprintf(transaction->output_name,
                           sizeof(transaction->output_name),
                           ".cupidbuild-run-%08x-%08x",
                           cupidbuild_host_process_id(), attempt);
    if (written < 0 || (size_t)written >= sizeof(transaction->output_name) ||
        !cupidbuild_host_copy_text(transaction->private_name,
                                  sizeof(transaction->private_name),
                                  transaction->output_name) ||
        !cupidbuild_host_join(transaction->private_root,
                              sizeof(transaction->private_root),
                              transaction->repository_root,
                              transaction->output_name)) {
      break;
    }
    transaction->private_handle =
        cupidbuild_host_profile_parent_open_component(
            transaction->working_directory_handle,
            transaction->output_name, 1, 1);
    if (transaction->private_handle != INVALID_HANDLE_VALUE) {
      transaction->private_created = 1;
      break;
    }
  }
  if (transaction->private_created == 0 ||
#if defined(_WIN32)
      transaction->private_handle == INVALID_HANDLE_VALUE ||
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->private_handle, &private_handle_snapshot) ||
#else
      transaction->private_descriptor < 0 ||
#endif
      !cupidbuild_host_directory_snapshot(
          transaction->private_root, &transaction->private_root_snapshot) ||
#if defined(_WIN32)
      !cupidbuild_host_snapshot_identity_equal(
          &private_handle_snapshot, &transaction->private_root_snapshot) ||
#endif
      !cupidbuild_host_join(transaction->tool_stdout,
                            sizeof(transaction->tool_stdout),
                            transaction->private_root, "tool.stdout") ||
      !cupidbuild_host_join(transaction->tool_stderr,
                            sizeof(transaction->tool_stderr),
                            transaction->private_root, "tool.stderr")) {
    cupidbuild_host_set_error(transaction,
                              "private checked-tool runner cannot be opened");
    *transaction_out = transaction;
    return 0;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_create_relative_regular(
          transaction->private_handle, "tool.stdout", empty, 0u, 1,
          &transaction->tool_stdout_snapshot,
          &transaction->tool_stdout_handle) ||
      !cupidbuild_host_windows_create_relative_regular(
          transaction->private_handle, "tool.stderr", empty, 0u, 1,
          &transaction->tool_stderr_snapshot,
          &transaction->tool_stderr_handle)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool streams cannot be opened");
    *transaction_out = transaction;
    return 0;
  }
#endif
  *transaction_out = transaction;
  return 1;
#endif
}

#if defined(_WIN32)
static int cupidbuild_host_read_runner_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *path,
    int optional, cupidbuild_host_snapshot_t *snapshot) {
  (void)transaction;
  return cupidbuild_host_read_regular(path, optional, snapshot,
                                      (unsigned char **)0);
}

static int cupidbuild_host_delete_runner_directory(
    cupidbuild_host_transaction_t *transaction) {
  BY_HANDLE_FILE_INFORMATION information;
  cupidbuild_windows_io_status_t status;
  unsigned char disposition = 1u;
  long result;
  if (transaction->private_handle == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(transaction->private_handle, &information) ||
      information.dwVolumeSerialNumber !=
          transaction->private_root_snapshot.identity[0] ||
      information.nFileIndexHigh !=
          transaction->private_root_snapshot.identity[1] ||
      information.nFileIndexLow !=
          transaction->private_root_snapshot.identity[2]) {
    return 0;
  }
  (void)memset(&status, 0, sizeof(status));
  result = cupid_windows_nt_set_information_file(
      transaction->private_handle, &status, &disposition,
      (unsigned long)sizeof(disposition), 13u);
  if (!CloseHandle(transaction->private_handle) || result < 0) {
    transaction->private_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  transaction->private_handle = INVALID_HANDLE_VALUE;
  return 1;
}

static int cupidbuild_host_remove_runner_private(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  char name[CUPIDBUILD_HOST_PATH_BYTES];
  int complete = 1;
  if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE &&
      !cupidbuild_host_windows_dispose_retained_at(
          transaction->private_handle, "tool.stdout",
          &transaction->tool_stdout_snapshot,
          &transaction->tool_stdout_handle)) {
    complete = 0;
  }
  if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE &&
      !cupidbuild_host_windows_dispose_retained_at(
          transaction->private_handle, "tool.stderr",
          &transaction->tool_stderr_snapshot,
          &transaction->tool_stderr_handle)) {
    complete = 0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if (!cupidbuild_host_basename(
            name, sizeof(name),
            transaction->inputs[index].frozen_path) ||
        (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
         !cupidbuild_host_windows_dispose_read_retained_at(
             transaction->private_handle, name,
             &transaction->inputs[index].frozen_snapshot,
             &transaction->inputs[index].frozen_handle))) {
      complete = 0;
    }
  }
  return complete != 0 &&
         cupidbuild_host_delete_runner_directory(transaction);
}
#else
static int cupidbuild_host_remove_runner_private(
    cupidbuild_host_transaction_t *transaction) {
  return transaction->private_created == 0;
}
#endif

static int cupidbuild_host_private_root_is_owned(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t pinned;
  cupidbuild_host_snapshot_t entry;
  if (transaction->private_created == 0) {
    return 1;
  }
#if defined(_WIN32)
  {
    HANDLE opened;
    HANDLE repository = transaction->runner_transaction != 0
                            ? transaction->working_directory_handle
                            : transaction->repository_root_handle;
    int valid;
    if (transaction->private_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(
            transaction->private_handle, &pinned)) {
      return 0;
    }
    opened = cupidbuild_host_windows_open_relative(
        repository, transaction->private_name, 1, 0);
    valid = opened != INVALID_HANDLE_VALUE &&
            cupidbuild_host_windows_directory_handle_snapshot(opened, &entry);
    if (opened != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(opened);
    }
    if (!valid) {
      return 0;
    }
  }
#else
  if (transaction->private_flat != 0) {
    cupidbuild_host_snapshot_t reservation;
    if (transaction->private_descriptor !=
            transaction->repository_root_descriptor ||
        transaction->private_reservation_descriptor < 0 ||
        !cupidbuild_host_directory_descriptor_snapshot(
            transaction->repository_root_descriptor, &pinned) ||
        !cupidbuild_host_read_open_file(
            transaction->private_reservation_descriptor,
            CUPIDBUILD_HOST_FILE_LIMIT, &reservation,
            (unsigned char **)0) ||
        !cupidbuild_host_regular_snapshot_at(
            transaction->repository_root_descriptor,
            transaction->private_name, &entry)) {
      return 0;
    }
    return cupidbuild_host_snapshot_identity_equal(
               &pinned, &transaction->repository_root_snapshot) &&
           cupidbuild_host_snapshot_equal(
               &reservation,
               &transaction->private_reservation_snapshot) &&
           cupidbuild_host_snapshot_equal(
               &entry, &transaction->private_reservation_snapshot);
  }
  if (transaction->private_descriptor < 0 ||
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->private_descriptor, &pinned) ||
      !cupidbuild_host_directory_entry_snapshot_at(
          transaction->repository_root_descriptor,
          transaction->private_name, &entry)) {
    return 0;
  }
#endif
  return cupidbuild_host_snapshot_identity_equal(
             &pinned, &transaction->private_root_snapshot) &&
         cupidbuild_host_snapshot_identity_equal(
             &entry, &transaction->private_root_snapshot);
}

#if defined(_WIN32)
static int cupidbuild_host_windows_relative_directory_missing(
    HANDLE parent, const char *name) {
  long status = 0;
  HANDLE handle = cupidbuild_host_windows_open_relative_access_status(
      parent, name, 1, 0, 0u, &status);
  unsigned long unsigned_status = (unsigned long)status;
  if (handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(handle);
    return 0;
  }
  return unsigned_status == 0xc000000fu ||
         unsigned_status == 0xc0000034u ||
         unsigned_status == 0xc000003au;
}
#else
static int cupidbuild_host_directory_entry_missing_at(
    int directory, const char *name) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  unsigned char information[96];
  return cupid_linux_syscall4(
             CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
             (unsigned int)name, (unsigned int)information,
             CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) ==
         -CUPIDBUILD_LINUX_ENOENT;
#else
  struct stat information;
  int result = fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW);
  return result != 0 && errno == ENOENT;
#endif
}
#endif

static int cupidbuild_host_remove_owned_private_root(
    cupidbuild_host_transaction_t *transaction) {
  if (transaction->private_created == 0) {
    return 1;
  }
  if (!cupidbuild_host_private_root_is_owned(transaction)) {
    return 0;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_dispose_handle(transaction->private_handle)) {
    transaction->private_handle = INVALID_HANDLE_VALUE;
    return 0;
  }
  transaction->private_handle = INVALID_HANDLE_VALUE;
  if (!cupidbuild_host_windows_relative_directory_missing(
          transaction->repository_root_handle,
          transaction->private_name)) {
    return 0;
  }
#else
  if (transaction->private_flat != 0) {
    if (!cupidbuild_host_cleanup_retained_regular_at(
            transaction->repository_root_descriptor,
            transaction->private_name,
            &transaction->private_reservation_snapshot,
            &transaction->private_reservation_descriptor)) {
      return 0;
    }
    transaction->private_created = 0;
    return 1;
  }
  {
    char moved_name[128];
    cupidbuild_host_snapshot_t moved;
    cupidbuild_host_snapshot_t source;
    unsigned int process_id = cupidbuild_host_process_id();
    unsigned int attempt;
    int moved_entry = 0;
    int removed;
    for (attempt = 0u; attempt < 16u; attempt++) {
      int written = snprintf(
          moved_name, sizeof(moved_name), "%s.cleanup-%08x-%02x",
          transaction->private_name, process_id, attempt);
      if (written <= 0 || (size_t)written >= sizeof(moved_name) ||
          !cupidbuild_host_directory_entry_snapshot_at(
              transaction->repository_root_descriptor,
              transaction->private_name, &source) ||
          !cupidbuild_host_snapshot_identity_equal(
              &source, &transaction->private_root_snapshot)) {
        break;
      }
      if (cupidbuild_host_rename_entry_noreplace_at(
              transaction->repository_root_descriptor,
              transaction->private_name,
              transaction->repository_root_descriptor, moved_name)) {
        moved_entry = 1;
        break;
      }
    }
    if (moved_entry == 0 ||
        !cupidbuild_host_directory_entry_snapshot_at(
            transaction->repository_root_descriptor, moved_name, &moved)) {
      return 0;
    }
    if (!cupidbuild_host_snapshot_identity_equal(
            &moved, &transaction->private_root_snapshot)) {
      (void)cupidbuild_host_rename_entry_noreplace_at(
          transaction->repository_root_descriptor, moved_name,
          transaction->repository_root_descriptor,
          transaction->private_name);
      return 0;
    }
    if (!cupidbuild_host_directory_entry_snapshot_at(
            transaction->repository_root_descriptor, moved_name, &source) ||
        !cupidbuild_host_snapshot_identity_equal(
            &source, &transaction->private_root_snapshot)) {
      (void)cupidbuild_host_rename_entry_noreplace_at(
          transaction->repository_root_descriptor, moved_name,
          transaction->repository_root_descriptor,
          transaction->private_name);
      return 0;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
      removed = cupid_linux_syscall3(
          CUPIDBUILD_LINUX_SYS_UNLINKAT,
          (unsigned int)transaction->repository_root_descriptor,
          (unsigned int)moved_name, CUPIDBUILD_LINUX_AT_REMOVEDIR);
    } while (removed == -CUPIDBUILD_LINUX_EINTR);
    removed = removed == 0;
#else
    do {
      removed = unlinkat(transaction->repository_root_descriptor,
                         moved_name, AT_REMOVEDIR);
    } while (removed != 0 && errno == EINTR);
    removed = removed == 0;
#endif
    if (!removed ||
        !cupidbuild_host_directory_entry_missing_at(
            transaction->repository_root_descriptor, moved_name)) {
      if (!removed) {
        (void)cupidbuild_host_rename_entry_noreplace_at(
            transaction->repository_root_descriptor, moved_name,
            transaction->repository_root_descriptor,
            transaction->private_name);
      }
      return 0;
    }
    cupidbuild_host_close_directory(transaction->private_descriptor);
    transaction->private_descriptor = -1;
  }
#endif
  transaction->private_created = 0;
  return 1;
}

static int cupidbuild_host_close_discovery_directories(
    cupidbuild_host_transaction_t *transaction) {
  size_t index;
  int valid = 1;
  for (index = 0u; index < transaction->discovery_directory_count; index++) {
    cupidbuild_host_discovery_directory_t *directory =
        &transaction->discovery_directories[index];
#if defined(_WIN32)
    if (directory->handle != INVALID_HANDLE_VALUE &&
        !CloseHandle(directory->handle)) {
      valid = 0;
    }
    directory->handle = INVALID_HANDLE_VALUE;
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (directory->descriptor >= 0 &&
        cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                             (unsigned int)directory->descriptor) < 0) {
      valid = 0;
    }
#else
    if (directory->descriptor >= 0 && close(directory->descriptor) != 0) {
      valid = 0;
    }
#endif
    directory->descriptor = -1;
#endif
    free(directory->logical);
  }
  free(transaction->discovery_directories);
  transaction->discovery_directories =
      (cupidbuild_host_discovery_directory_t *)0;
  transaction->discovery_directory_count = 0u;
  transaction->discovery_directory_capacity = 0u;
  return valid;
}

static int cupidbuild_host_abandon_interfered_transaction(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  (void)cupidbuild_host_close_discovery_directories(transaction);
#if defined(_WIN32)
  for (index = 0u; index < transaction->input_count; index++) {
    if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE) {
      (void)CloseHandle(transaction->inputs[index].frozen_handle);
      transaction->inputs[index].frozen_handle = INVALID_HANDLE_VALUE;
    }
  }
  if (transaction->candidate_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->candidate_handle);
  }
  if (transaction->private_output_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->private_output_handle);
  }
  if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->tool_stdout_handle);
  }
  if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->tool_stderr_handle);
  }
  if (transaction->initial_output_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->initial_output_handle);
  }
  if (transaction->lock_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->lock_handle);
  }
  if (transaction->private_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->private_handle);
  }
  if (transaction->working_directory_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->working_directory_handle);
  }
  if (transaction->output_parent_handle != INVALID_HANDLE_VALUE &&
      transaction->output_parent_is_repository_root == 0) {
    (void)CloseHandle(transaction->output_parent_handle);
  }
  if (transaction->repository_root_handle != INVALID_HANDLE_VALUE) {
    (void)CloseHandle(transaction->repository_root_handle);
  }
#else
  for (index = 0u; index < transaction->input_count; index++) {
    cupidbuild_host_close_directory(
        transaction->inputs[index].frozen_descriptor);
    transaction->inputs[index].frozen_descriptor = -1;
  }
  cupidbuild_host_close_directory(transaction->candidate_descriptor);
  cupidbuild_host_close_directory(transaction->private_output_descriptor);
  cupidbuild_host_close_directory(transaction->tool_stdout_descriptor);
  cupidbuild_host_close_directory(transaction->tool_stderr_descriptor);
  cupidbuild_host_close_directory(
      transaction->private_reservation_descriptor);
  cupidbuild_host_close_directory(transaction->working_directory_descriptor);
  if (transaction->private_descriptor >= 0 &&
      transaction->private_descriptor !=
          transaction->repository_root_descriptor &&
      transaction->private_descriptor !=
          transaction->output_parent_descriptor) {
    cupidbuild_host_close_directory(transaction->private_descriptor);
  }
  if (transaction->output_parent_descriptor >= 0 &&
      transaction->output_parent_is_repository_root == 0) {
    cupidbuild_host_close_directory(transaction->output_parent_descriptor);
  }
  cupidbuild_host_close_directory(transaction->repository_root_descriptor);
#endif
  free(transaction->inputs);
  free(transaction);
  return 0;
}

int cupidbuild_host_transaction_close(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 1;
  }
  if (transaction->namespace_interfered != 0) {
    return cupidbuild_host_abandon_interfered_transaction(transaction);
  }
  if (transaction->runner_transaction != 0) {
    int cleanup_succeeded = 1;
    if (!cupidbuild_host_close_discovery_directories(transaction)) {
      cleanup_succeeded = 0;
    }
    if (transaction->private_created != 0 &&
        !cupidbuild_host_remove_runner_private(transaction)) {
      cleanup_succeeded = 0;
    }
#if defined(_WIN32)
    for (index = 0u; index < transaction->input_count; index++) {
      if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
          !CloseHandle(transaction->inputs[index].frozen_handle)) {
        cleanup_succeeded = 0;
      }
      transaction->inputs[index].frozen_handle = INVALID_HANDLE_VALUE;
    }
    if (transaction->private_handle != INVALID_HANDLE_VALUE) {
      if (!CloseHandle(transaction->private_handle)) {
        cleanup_succeeded = 0;
      }
    }
    if (transaction->working_directory_handle != INVALID_HANDLE_VALUE) {
      if (!CloseHandle(transaction->working_directory_handle)) {
        cleanup_succeeded = 0;
      }
    }
#else
    for (index = 0u; index < transaction->input_count; index++) {
      if (!cupidbuild_host_close_directory(
              transaction->inputs[index].frozen_descriptor)) {
        cleanup_succeeded = 0;
      }
      transaction->inputs[index].frozen_descriptor = -1;
    }
    if (!cupidbuild_host_close_directory(
            transaction->tool_stdout_descriptor)) {
      cleanup_succeeded = 0;
    }
    if (!cupidbuild_host_close_directory(
            transaction->tool_stderr_descriptor)) {
      cleanup_succeeded = 0;
    }
    if (!cupidbuild_host_close_directory(transaction->private_descriptor)) {
      cleanup_succeeded = 0;
    }
    if (!cupidbuild_host_close_directory(
            transaction->output_parent_descriptor)) {
      cleanup_succeeded = 0;
    }
#endif
    free(transaction->inputs);
    free(transaction);
    return cleanup_succeeded;
  }
  {
    int cleanup_succeeded = 1;
    int private_cleanup_succeeded =
        cupidbuild_host_private_root_is_owned(transaction);
    char name[CUPIDBUILD_HOST_PATH_BYTES];
    if (private_cleanup_succeeded == 0) {
      cleanup_succeeded = 0;
    }
    if (!cupidbuild_host_close_discovery_directories(transaction)) {
      cleanup_succeeded = 0;
    }
#if defined(_WIN32)
    if (transaction->candidate_handle != INVALID_HANDLE_VALUE) {
      if (transaction->candidate_published != 0) {
        if (!CloseHandle(transaction->candidate_handle)) {
          cleanup_succeeded = 0;
        }
      } else if (transaction->candidate_snapshot.present != 0) {
        if (!cupidbuild_host_windows_dispose_retained_at(
                transaction->private_handle, "candidate.o",
                &transaction->candidate_snapshot,
                &transaction->candidate_handle)) {
          cleanup_succeeded = 0;
          private_cleanup_succeeded = 0;
        }
      } else if (!CloseHandle(transaction->candidate_handle)) {
        cleanup_succeeded = 0;
      }
      transaction->candidate_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (transaction->candidate_descriptor >= 0) {
      if (transaction->candidate_published != 0) {
        if (!cupidbuild_host_close_retained_descriptor(
                &transaction->candidate_descriptor)) {
          cleanup_succeeded = 0;
        }
      } else if (!cupidbuild_host_cleanup_retained_regular_at(
                     transaction->private_descriptor,
                     cupidbuild_host_private_entry_name(
                         transaction, "candidate.o"),
                     &transaction->candidate_snapshot,
                     &transaction->candidate_descriptor)) {
        cleanup_succeeded = 0;
        private_cleanup_succeeded = 0;
      }
    }
#endif
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
        !cupidbuild_host_delete_owned_private_name(
            transaction, "candidate.o", &transaction->candidate_snapshot)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
        transaction->candidate_publish_created != 0 &&
        !cupidbuild_host_delete_owned_private_name(
            transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH,
            &transaction->candidate_publish_snapshot)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0 &&
        transaction->private_output_descriptor >= 0 &&
        !cupidbuild_host_close_retained_descriptor(
            &transaction->private_output_descriptor)) {
      cleanup_succeeded = 0;
    }
#endif
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
#if !defined(_WIN32)
        transaction->private_flat == 0 &&
#endif
        !cupidbuild_host_close_private_output_handle(transaction)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
        !cupidbuild_host_delete_owned_private_name(
            transaction, "candidate.map",
            &transaction->private_output_snapshot)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    if (!cupidbuild_host_close_private_stream_handles(transaction)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
        !cupidbuild_host_delete_owned_private_name(
            transaction, "tool.stdout",
            &transaction->tool_stdout_snapshot)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    if (private_cleanup_succeeded != 0 &&
        transaction->private_created != 0 &&
        !cupidbuild_host_delete_owned_private_name(
            transaction, "tool.stderr",
            &transaction->tool_stderr_snapshot)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
      if (!cupidbuild_host_copy_text(
              name, sizeof(name),
              transaction->inputs[index].frozen_name)) {
        cleanup_succeeded = 0;
        private_cleanup_succeeded = 0;
        continue;
      }
#if defined(_WIN32)
    if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
        !cupidbuild_host_windows_dispose_read_retained_at(
            transaction->private_handle, name,
            &transaction->inputs[index].frozen_snapshot,
            &transaction->inputs[index].frozen_handle)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
#else
    if (transaction->inputs[index].frozen_descriptor >= 0 &&
        !(transaction->private_flat != 0
              ? cupidbuild_host_close_retained_descriptor(
                    &transaction->inputs[index].frozen_descriptor)
              : cupidbuild_host_cleanup_retained_regular_at(
                    transaction->private_descriptor, name,
                    &transaction->inputs[index].frozen_snapshot,
                    &transaction->inputs[index].frozen_descriptor))) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
#endif
  }
#if defined(_WIN32)
    if (transaction->initial_output_handle != INVALID_HANDLE_VALUE) {
      if (transaction->initial_output_parked != 0 &&
          transaction->publication_committed == 0 &&
          !cupidbuild_host_windows_restore_initial_output(transaction)) {
        cleanup_succeeded = 0;
      }
      if (transaction->initial_output_parked != 0 &&
          transaction->publication_committed != 0) {
        cleanup_succeeded = 0;
      }
      if (transaction->initial_output_handle != INVALID_HANDLE_VALUE &&
          !CloseHandle(transaction->initial_output_handle)) {
        cleanup_succeeded = 0;
      }
      transaction->initial_output_handle = INVALID_HANDLE_VALUE;
    } else if (transaction->initial_output_parked != 0) {
      cleanup_succeeded = 0;
    }
#else
    if (transaction->initial_output_parked != 0 &&
        !cupidbuild_host_posix_dispose_parked_output(transaction)) {
      cleanup_succeeded = 0;
    }
#endif
    if (!cupidbuild_host_release_lock(transaction)) {
      cleanup_succeeded = 0;
    }
    if (private_cleanup_succeeded != 0 &&
        !cupidbuild_host_remove_owned_private_root(transaction)) {
      cleanup_succeeded = 0;
      private_cleanup_succeeded = 0;
    }
#if defined(_WIN32)
  if (transaction->private_handle != (HANDLE)0 &&
      transaction->private_handle != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(transaction->private_handle)) {
      cleanup_succeeded = 0;
    }
  }
  if (transaction->output_parent_handle != (HANDLE)0 &&
      transaction->output_parent_handle != INVALID_HANDLE_VALUE &&
      transaction->output_parent_is_repository_root == 0) {
    if (!CloseHandle(transaction->output_parent_handle)) {
      cleanup_succeeded = 0;
    }
  }
  if (transaction->repository_root_handle != (HANDLE)0 &&
      transaction->repository_root_handle != INVALID_HANDLE_VALUE) {
    if (!CloseHandle(transaction->repository_root_handle)) {
      cleanup_succeeded = 0;
    }
  }
#else
  if (transaction->private_reservation_descriptor >= 0 &&
      !cupidbuild_host_close_retained_descriptor(
          &transaction->private_reservation_descriptor)) {
    cleanup_succeeded = 0;
  }
  if (!cupidbuild_host_close_directory(
          transaction->working_directory_descriptor)) {
    cleanup_succeeded = 0;
  }
  if (transaction->private_flat == 0) {
    if (!cupidbuild_host_close_directory(transaction->private_descriptor)) {
      cleanup_succeeded = 0;
    }
  }
  if (transaction->output_parent_is_repository_root == 0) {
    if (!cupidbuild_host_close_directory(
            transaction->output_parent_descriptor)) {
      cleanup_succeeded = 0;
    }
  }
  if (!cupidbuild_host_close_directory(
          transaction->repository_root_descriptor)) {
    cleanup_succeeded = 0;
  }
#endif
  free(transaction->inputs);
  free(transaction);
    return cleanup_succeeded;
  }
}

int cupidbuild_host_publication_committed(
    const cupidbuild_host_transaction_t *transaction) {
  return transaction != (const cupidbuild_host_transaction_t *)0 &&
         transaction->publication_committed != 0;
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

static const cupidbuild_host_input_t *cupidbuild_host_frozen_tool_input(
    const cupidbuild_host_transaction_t *transaction, const char *tool) {
  unsigned int index;
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      tool == (const char *)0) {
    return (const cupidbuild_host_input_t *)0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if (strcmp(transaction->inputs[index].frozen_path, tool) == 0) {
      return &transaction->inputs[index];
    }
  }
  return (const cupidbuild_host_input_t *)0;
}

int cupidbuild_host_run_captured(cupidbuild_host_transaction_t *transaction,
                                 const char *tool,
                                 const char *const *arguments,
                                 unsigned int timeout_milliseconds) {
  const cupidbuild_host_input_t *tool_input;
  cupidbuild_host_snapshot_t stdout_snapshot;
  cupidbuild_host_snapshot_t stderr_snapshot;
#if defined(_WIN32)
  cupidbuild_host_snapshot_t working_handle_snapshot;
  cupidbuild_host_snapshot_t working_path_snapshot;
#endif
  int result;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction == 0 || tool == (const char *)0 ||
      arguments == (const char *const *)0 || timeout_milliseconds == 0u ||
      transaction->captured != 0 ||
      !cupidbuild_host_require_frozen_inputs(transaction)) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      cupidbuild_host_set_error(transaction,
                                "checked tool runner request is invalid");
    }
    return -1;
  }
  tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
  if (tool_input == (const cupidbuild_host_input_t *)0) {
    cupidbuild_host_set_error(transaction,
                              "checked tool is not a frozen input");
    return -1;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_directory_handle_snapshot(
          transaction->working_directory_handle,
          &working_handle_snapshot) ||
      !cupidbuild_host_directory_snapshot(transaction->repository_root,
                                          &working_path_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &working_handle_snapshot,
          &transaction->repository_root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &working_path_snapshot,
          &transaction->repository_root_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool working directory changed");
    return -1;
  }
#endif
  result = cupidbuild_host_run_process(
      tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
      tool_input->frozen_handle,
#else
      tool_input->frozen_descriptor,
#endif
      arguments, transaction->tool_stdout, transaction->tool_stderr,
#if defined(_WIN32)
      transaction->tool_stdout_handle,
      transaction->tool_stderr_handle,
#else
      transaction->tool_stdout_descriptor,
      transaction->tool_stderr_descriptor,
#endif
      transaction->repository_root,
#if defined(_WIN32)
      -1,
#else
      transaction->output_parent_descriptor,
#endif
      (const cupidbuild_host_transaction_t *)0,
      &transaction->tool_stdout_snapshot,
      &transaction->tool_stderr_snapshot, timeout_milliseconds);
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    cupidbuild_host_set_error(
        transaction, "private checked seed changed while checked tool ran");
    return -1;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "tool.stdout",
          &transaction->tool_stdout_snapshot,
          &transaction->tool_stdout_handle, 1, 0, 1) ||
      !cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "tool.stderr",
          &transaction->tool_stderr_snapshot,
          &transaction->tool_stderr_handle, 1, 0, 1)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool output could not be sealed");
    return -1;
  }
  transaction->tool_stdout_sealed = 1;
  transaction->tool_stderr_sealed = 1;
#endif
#if !defined(_WIN32)
  if (!cupidbuild_host_seal_anonymous(
          transaction->tool_stdout_descriptor) ||
      !cupidbuild_host_seal_anonymous(
          transaction->tool_stderr_descriptor)) {
    cupidbuild_host_set_error(transaction,
                              "checked tool output could not be sealed");
    return -1;
  }
  if (!cupidbuild_host_read_open_file(
          transaction->tool_stdout_descriptor, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_snapshot, (unsigned char **)0) ||
      !cupidbuild_host_read_open_file(
          transaction->tool_stderr_descriptor, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_snapshot, (unsigned char **)0)) {
#else
  {
    cupidbuild_host_snapshot_t stdout_named;
    cupidbuild_host_snapshot_t stderr_named;
  if (!cupidbuild_host_windows_read_open_regular(
          transaction->tool_stdout_handle, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_snapshot, (unsigned char **)0) ||
      !cupidbuild_host_windows_read_open_regular(
          transaction->tool_stderr_handle, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_snapshot, (unsigned char **)0) ||
      !cupidbuild_host_read_regular_limit(
          transaction->tool_stdout, 0, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_named, (unsigned char **)0) ||
      !cupidbuild_host_read_regular_limit(
          transaction->tool_stderr, 0, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_named, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&stdout_snapshot, &stdout_named) ||
      !cupidbuild_host_snapshot_equal(&stderr_snapshot, &stderr_named)) {
#endif
    cupidbuild_host_set_error(transaction,
                              "checked tool output could not be captured");
    return -1;
  }
#if defined(_WIN32)
  }
#endif
  if (!cupidbuild_host_snapshot_identity_equal(
          &stdout_snapshot, &transaction->tool_stdout_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &stderr_snapshot, &transaction->tool_stderr_snapshot)) {
    cupidbuild_host_set_error(
        transaction,
        "checked tool output identity changed while tool ran");
    return -1;
  }
  transaction->tool_stdout_snapshot = stdout_snapshot;
  transaction->tool_stderr_snapshot = stderr_snapshot;
  if (result < 0 && result != -2) {
    cupidbuild_host_set_error(transaction,
                              "checked tool could not be started or captured");
    return -1;
  }
  if (stdout_snapshot.present == 0 || stderr_snapshot.present == 0) {
    cupidbuild_host_set_error(transaction,
                              "checked tool could not be started or captured");
    return -1;
  }
  if (result == -2) {
    cupidbuild_host_set_error(transaction, "checked tool timed out");
    return -2;
  }
  transaction->captured = 1;
  return result;
}

int cupidbuild_host_forward_captured(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t stdout_snapshot;
  cupidbuild_host_snapshot_t stderr_snapshot;
  unsigned char *stdout_bytes = (unsigned char *)0;
  unsigned char *stderr_bytes = (unsigned char *)0;
  int success;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction == 0 || transaction->captured == 0) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      cupidbuild_host_set_error(transaction,
                                "checked tool output is not available");
    }
    return 0;
  }
#if !defined(_WIN32)
  if (!cupidbuild_host_read_open_file(
          transaction->tool_stdout_descriptor, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_snapshot, &stdout_bytes) ||
      !cupidbuild_host_read_open_file(
          transaction->tool_stderr_descriptor, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_snapshot, &stderr_bytes) ||
#else
  {
    cupidbuild_host_snapshot_t stdout_named;
    cupidbuild_host_snapshot_t stderr_named;
  if (!cupidbuild_host_windows_read_open_regular(
          transaction->tool_stdout_handle, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_snapshot, &stdout_bytes) ||
      !cupidbuild_host_windows_read_open_regular(
          transaction->tool_stderr_handle, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_snapshot, &stderr_bytes) ||
      !cupidbuild_host_read_regular_limit(
          transaction->tool_stdout, 0, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stdout_named, (unsigned char **)0) ||
      !cupidbuild_host_read_regular_limit(
          transaction->tool_stderr, 0, CUPIDBUILD_HOST_STREAM_LIMIT,
          &stderr_named, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&stdout_snapshot, &stdout_named) ||
      !cupidbuild_host_snapshot_equal(&stderr_snapshot, &stderr_named) ||
#endif
      !cupidbuild_host_snapshot_equal(
          &stdout_snapshot, &transaction->tool_stdout_snapshot) ||
      !cupidbuild_host_snapshot_equal(
          &stderr_snapshot, &transaction->tool_stderr_snapshot)) {
    free(stdout_bytes);
    free(stderr_bytes);
    cupidbuild_host_set_error(transaction,
                              "checked tool output cannot be read");
    return 0;
  }
#if defined(_WIN32)
  }
#endif
#if defined(_WIN32) && !defined(CUPID_HOSTED_I386_WINDOWS_H)
  {
    int stdout_mode = -1;
    int stderr_mode = -1;
    success = fflush(stdout) == 0 && ferror(stdout) == 0 &&
              fflush(stderr) == 0 && ferror(stderr) == 0;
    if (success != 0) {
      stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
      stderr_mode = _setmode(_fileno(stderr), _O_BINARY);
      success = stdout_mode >= 0 && stderr_mode >= 0 &&
                fwrite(stdout_bytes, 1u, stdout_snapshot.size, stdout) ==
                    stdout_snapshot.size &&
                fflush(stdout) == 0 && ferror(stdout) == 0 &&
                fwrite(stderr_bytes, 1u, stderr_snapshot.size, stderr) ==
                    stderr_snapshot.size &&
                fflush(stderr) == 0 && ferror(stderr) == 0;
    }
    if (stdout_mode >= 0 && _setmode(_fileno(stdout), stdout_mode) < 0) {
      success = 0;
    }
    if (stderr_mode >= 0 && _setmode(_fileno(stderr), stderr_mode) < 0) {
      success = 0;
    }
  }
#else
  success =
      fwrite(stdout_bytes, 1u, stdout_snapshot.size, stdout) ==
          stdout_snapshot.size &&
      fflush(stdout) == 0 && ferror(stdout) == 0 &&
      fwrite(stderr_bytes, 1u, stderr_snapshot.size, stderr) ==
          stderr_snapshot.size &&
      fflush(stderr) == 0 && ferror(stderr) == 0;
#endif
  free(stdout_bytes);
  free(stderr_bytes);
  if (success == 0) {
    cupidbuild_host_set_error(transaction,
                              "checked tool output cannot be forwarded");
  }
  return success;
}

static int cupidbuild_host_run_at(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds,
    int private_working_directory) {
  const cupidbuild_host_input_t *tool_input;
  cupidbuild_host_snapshot_t stdout_snapshot;
  cupidbuild_host_snapshot_t stderr_snapshot;
  unsigned char *stdout_bytes = (unsigned char *)0;
  unsigned char *stderr_bytes = (unsigned char *)0;
  int result;
  (void)memset(&stdout_snapshot, 0, sizeof(stdout_snapshot));
  (void)memset(&stderr_snapshot, 0, sizeof(stderr_snapshot));
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      tool == (const char *)0 || arguments == (const char *const *)0 ||
      transaction->runner_transaction != 0 || timeout_milliseconds == 0u ||
      !cupidbuild_host_require_frozen_inputs(transaction)) {
    return -1;
  }
  tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
  if (tool_input == (const cupidbuild_host_input_t *)0) {
    cupidbuild_host_set_error(transaction,
                              "checked tool is not a frozen input");
    return -1;
  }
  if (!cupidbuild_host_close_private_stream_handles(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stdout",
          &transaction->tool_stdout_snapshot) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stderr",
          &transaction->tool_stderr_snapshot) ||
      !cupidbuild_host_prepare_private_streams(transaction) ||
      !cupidbuild_host_prepare_private_candidate(transaction) ||
      !cupidbuild_host_prepare_private_output_path(transaction)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool entries cannot be prepared");
    return -1;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_transition_retained_at(
           transaction->private_handle, "candidate.o",
           &transaction->candidate_snapshot,
           &transaction->candidate_handle, 0, 1, 1) ||
      !cupidbuild_host_windows_transition_retained_at(
           transaction->private_handle, "candidate.map",
           &transaction->private_output_snapshot,
           &transaction->private_output_handle, 0, 1, 1)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool entries cannot be reopened");
    return -1;
  }
  transaction->candidate_sealed = 0;
  transaction->private_output_sealed = 0;
#endif
  result = cupidbuild_host_run_process(
      tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
      tool_input->frozen_handle,
#else
      tool_input->frozen_descriptor,
#endif
      arguments, transaction->tool_stdout, transaction->tool_stderr,
#if defined(_WIN32)
      transaction->tool_stdout_handle,
      transaction->tool_stderr_handle,
#else
      transaction->tool_stdout_descriptor,
      transaction->tool_stderr_descriptor,
#endif
      private_working_directory != 0
#if !defined(_WIN32)
              ? (transaction->private_flat != 0 ? "/proc"
                                                : transaction->private_root)
#else
              ? transaction->private_root
#endif
              : (const char *)0,
#if defined(_WIN32)
      -1,
#else
      private_working_directory != 0
          ? (transaction->private_flat != 0
                 ? transaction->working_directory_descriptor
                                            : transaction->private_descriptor)
          : -1,
#endif
#if defined(_WIN32)
      (const cupidbuild_host_transaction_t *)0,
#else
      transaction->private_flat != 0
          ? transaction
          : (const cupidbuild_host_transaction_t *)0,
#endif
      (cupidbuild_host_snapshot_t *)0,
      (cupidbuild_host_snapshot_t *)0, timeout_milliseconds);
  if (result == -2) {
    result = 124;
  }
#if defined(_WIN32)
  if (transaction->tool_stderr_sealed == 0 &&
      !cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "tool.stderr",
          &transaction->tool_stderr_snapshot,
          &transaction->tool_stderr_handle, 1, 0, 1)) {
    result = -1;
  } else {
    transaction->tool_stderr_sealed = 1;
  }
#endif
  if (!cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_capture_private_streams(transaction) ||
      !cupidbuild_host_capture_retained_candidate(transaction) ||
      !cupidbuild_host_capture_retained_private_output(transaction)) {
    result = -1;
  }
  if (result >= 0 &&
      (!cupidbuild_host_read_retained_private_regular(
           transaction, "tool.stdout", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
           transaction->tool_stdout_handle,
#else
           transaction->tool_stdout_descriptor,
#endif
           &stdout_snapshot, &stdout_bytes) ||
       !cupidbuild_host_read_retained_private_regular(
           transaction, "tool.stderr", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
           transaction->tool_stderr_handle,
#else
           transaction->tool_stderr_descriptor,
#endif
           &stderr_snapshot, &stderr_bytes) ||
       !cupidbuild_host_snapshot_equal(
           &stdout_snapshot, &transaction->tool_stdout_snapshot) ||
       !cupidbuild_host_snapshot_equal(
           &stderr_snapshot, &transaction->tool_stderr_snapshot))) {
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
  if (!cupidbuild_host_close_private_stream_handles(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stdout",
          &transaction->tool_stdout_snapshot) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stderr",
          &transaction->tool_stderr_snapshot)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool stream entry changed");
    result = -1;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    return -1;
  }
  if (result < 0) {
    cupidbuild_host_set_error(transaction, "checked tool could not be started");
  } else if (result == 124) {
    cupidbuild_host_set_error(transaction, "checked tool timed out");
  }
  return result;
}

int cupidbuild_host_run(cupidbuild_host_transaction_t *transaction,
                        const char *tool, const char *const *arguments,
                        unsigned int timeout_milliseconds) {
  return cupidbuild_host_run_at(transaction, tool, arguments,
                                timeout_milliseconds, 0);
}

int cupidbuild_host_run_in_private(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds) {
  return cupidbuild_host_run_at(transaction, tool, arguments,
                                timeout_milliseconds, 1);
}

int cupidbuild_host_run_to_private_output(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds) {
  const cupidbuild_host_input_t *tool_input;
  cupidbuild_host_snapshot_t output_snapshot;
  cupidbuild_host_snapshot_t stderr_snapshot;
  unsigned char *stderr_bytes = (unsigned char *)0;
  int result;
  (void)memset(&output_snapshot, 0, sizeof(output_snapshot));
  (void)memset(&stderr_snapshot, 0, sizeof(stderr_snapshot));
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->runner_transaction != 0 || tool == (const char *)0 ||
      arguments == (const char *const *)0 || timeout_milliseconds == 0u ||
      !cupidbuild_host_require_frozen_inputs(transaction)) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
      cupidbuild_host_set_error(
          transaction, "checked private-output tool request is invalid");
    }
    return -1;
  }
  tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
  if (tool_input == (const cupidbuild_host_input_t *)0) {
    cupidbuild_host_set_error(transaction,
                              "checked tool is not a frozen input");
    return -1;
  }
  if (!cupidbuild_host_close_private_output_handle(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "candidate.map",
          &transaction->private_output_snapshot) ||
      !cupidbuild_host_close_private_stream_handles(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stderr",
          &transaction->tool_stderr_snapshot) ||
      !cupidbuild_host_prepare_private_output_stream(transaction) ||
      !cupidbuild_host_prepare_private_stderr(transaction)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool output cannot be prepared");
    return -1;
  }
  transaction->private_output_captured = 0;
  result = cupidbuild_host_run_process(
      tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
      tool_input->frozen_handle,
#else
      tool_input->frozen_descriptor,
#endif
      arguments, transaction->private_output, transaction->tool_stderr,
 #if defined(_WIN32)
      transaction->private_output_handle,
      transaction->tool_stderr_handle,
 #else
      transaction->private_output_descriptor,
      transaction->tool_stderr_descriptor,
 #endif
      (const char *)0, -1,
#if defined(_WIN32)
      (const cupidbuild_host_transaction_t *)0,
#else
      transaction->private_flat != 0
          ? transaction
          : (const cupidbuild_host_transaction_t *)0,
#endif
      (cupidbuild_host_snapshot_t *)0, (cupidbuild_host_snapshot_t *)0,
      timeout_milliseconds);
  if (result == -2) {
    result = 124;
  }
#if defined(_WIN32)
  if (!cupidbuild_host_windows_transition_retained_at(
          transaction->private_handle, "tool.stderr",
          &transaction->tool_stderr_snapshot,
          &transaction->tool_stderr_handle, 1, 0, 1)) {
    result = -1;
  } else {
    transaction->tool_stderr_sealed = 1;
  }
#endif
#if !defined(_WIN32)
  if (transaction->private_flat != 0 &&
      transaction->tool_stderr_sealed == 0) {
    if (!cupidbuild_host_seal_anonymous(
            transaction->tool_stderr_descriptor)) {
      result = -1;
    } else {
      transaction->tool_stderr_sealed = 1;
    }
  }
#endif
  if (!cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_capture_retained_private_output(transaction) ||
      !cupidbuild_host_read_retained_private_regular(
          transaction, "tool.stderr", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
          transaction->tool_stderr_handle,
#else
          transaction->tool_stderr_descriptor,
#endif
          &stderr_snapshot, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_identity_equal(
          &stderr_snapshot, &transaction->tool_stderr_snapshot)) {
    result = -1;
  } else {
    transaction->tool_stderr_snapshot = stderr_snapshot;
  }
  if (result >= 0 &&
      (!cupidbuild_host_read_retained_private_regular(
           transaction, "candidate.map", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
           transaction->private_output_handle,
#else
           transaction->private_output_descriptor,
#endif
           &output_snapshot, (unsigned char **)0) ||
       !cupidbuild_host_read_retained_private_regular(
           transaction, "tool.stderr", CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
           transaction->tool_stderr_handle,
#else
           transaction->tool_stderr_descriptor,
#endif
           &stderr_snapshot, &stderr_bytes) ||
       !cupidbuild_host_snapshot_equal(
           &output_snapshot, &transaction->private_output_snapshot) ||
       !cupidbuild_host_snapshot_equal(
           &stderr_snapshot, &transaction->tool_stderr_snapshot))) {
    result = -1;
  }
  if (result != 0 && stderr_bytes != (unsigned char *)0 &&
      stderr_snapshot.size != 0u) {
    (void)fwrite(stderr_bytes, 1u, stderr_snapshot.size, stderr);
  }
  if (result == 0 && stderr_snapshot.size != 0u) {
    cupidbuild_host_set_error(transaction,
                              "checked tool wrote unexpected standard error");
    result = 125;
  }
  free(stderr_bytes);
  if (!cupidbuild_host_close_private_stream_handles(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "tool.stderr",
          &transaction->tool_stderr_snapshot)) {
    cupidbuild_host_set_error(
        transaction, "private checked-tool stream entry changed");
    result = -1;
  }
  if (!cupidbuild_host_require_frozen_inputs(transaction)) {
    result = -1;
  }
  if (result < 0) {
    cupidbuild_host_set_error(transaction, "checked tool could not be started");
  } else if (result == 124) {
    cupidbuild_host_set_error(transaction, "checked tool timed out");
  }
  return result;
}

static int cupidbuild_host_read_candidate(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot, unsigned char **bytes_out) {
#if defined(_WIN32)
  if (transaction->candidate_handle == INVALID_HANDLE_VALUE) {
    transaction->candidate_handle =
        cupidbuild_host_windows_open_relative_access(
            transaction->private_handle, "candidate.o", 0, 1, DELETE);
  }
  return cupidbuild_host_windows_read_open_regular(
      transaction->candidate_handle, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
      bytes_out);
#else
  const char *candidate_name = cupidbuild_host_private_entry_name(
      transaction, "candidate.o");
  if (transaction->candidate_descriptor < 0) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    transaction->candidate_descriptor = cupidbuild_linux_open_relative(
        transaction->private_descriptor, candidate_name, 0);
#else
    transaction->candidate_descriptor = cupidbuild_native_open_relative(
        transaction->private_descriptor, candidate_name, 0);
#endif
  }
  return transaction->candidate_descriptor >= 0 &&
         cupidbuild_host_read_open_file(
             transaction->candidate_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
             snapshot, bytes_out);
#endif
}

static int cupidbuild_host_candidate_identity_allowed(
    const cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *candidate) {
  unsigned int index;
  if (candidate == (const cupidbuild_host_snapshot_t *)0 ||
      candidate->present == 0 ||
      (transaction->initial_output_snapshot.present != 0 &&
       cupidbuild_host_snapshot_identity_equal(
           candidate, &transaction->initial_output_snapshot)) ||
      (transaction->lock_snapshot.present != 0 &&
       cupidbuild_host_snapshot_identity_equal(
           candidate, &transaction->lock_snapshot))) {
    return 0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    if ((transaction->inputs[index].snapshot.present != 0 &&
         cupidbuild_host_snapshot_identity_equal(
             candidate, &transaction->inputs[index].snapshot)) ||
        (transaction->inputs[index].frozen_snapshot.present != 0 &&
         cupidbuild_host_snapshot_identity_equal(
             candidate, &transaction->inputs[index].frozen_snapshot))) {
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_host_prepare_candidate_publication(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *candidate) {
#if defined(_WIN32)
  transaction->candidate_publish_snapshot = *candidate;
  return 1;
#else
  cupidbuild_host_snapshot_t alias;
  const char *publish_name = cupidbuild_host_private_entry_name(
      transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
  if (transaction->candidate_publish_created == 0) {
    if (!cupidbuild_host_link_open_file_at(
            transaction->candidate_descriptor,
            transaction->private_descriptor,
            publish_name)) {
      return 0;
    }
    transaction->candidate_publish_created = 1;
    transaction->candidate_publish_snapshot = *candidate;
  }
  if (!cupidbuild_host_regular_snapshot_at(
          transaction->private_descriptor,
          publish_name, &alias)) {
    return 0;
  }
  if (!cupidbuild_host_snapshot_equal(&alias, candidate)) {
    return 0;
  }
  return 1;
#endif
}

static int cupidbuild_host_candidate_ready_for_publication(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t retained;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->candidate_captured == 0 ||
      !cupidbuild_host_read_candidate(
          transaction, &retained, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(
          &retained, &transaction->candidate_snapshot) ||
      !cupidbuild_host_candidate_identity_allowed(transaction, &retained)) {
    return 0;
  }
#if !defined(_WIN32)
  const char *publish_name = cupidbuild_host_private_entry_name(
      transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
  if (transaction->candidate_publish_created == 0 ||
      !cupidbuild_host_regular_snapshot_at(
          transaction->private_descriptor,
          publish_name, &retained) ||
      !cupidbuild_host_snapshot_equal(
          &retained, &transaction->candidate_publish_snapshot)) {
    return 0;
  }
#endif
  return 1;
}

static int cupidbuild_host_published_candidate_matches(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t retained;
  cupidbuild_host_snapshot_t published;
  return cupidbuild_host_read_candidate(
             transaction, &retained, (unsigned char **)0) &&
         cupidbuild_host_snapshot_equal(
             &retained, &transaction->candidate_snapshot) &&
         cupidbuild_host_read_output(
             transaction, 0, &published, (unsigned char **)0) &&
         cupidbuild_host_snapshot_equal(
             &published, &transaction->candidate_snapshot);
}

#if !defined(_WIN32)
static int cupidbuild_host_rename_exchange_at(
    int left_directory, const char *left_name,
    int right_directory, const char *right_name) {
  int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
  do {
    result = cupid_linux_syscall5(
        CUPIDBUILD_LINUX_SYS_RENAMEAT2, (unsigned int)left_directory,
        (unsigned int)left_name, (unsigned int)right_directory,
        (unsigned int)right_name, CUPIDBUILD_LINUX_RENAME_EXCHANGE);
  } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
  do {
    result = renameat2(left_directory, left_name, right_directory,
                       right_name, RENAME_EXCHANGE);
  } while (result != 0 && errno == EINTR);
#endif
  return result == 0;
}

static int cupidbuild_host_posix_park_initial_output(
    cupidbuild_host_transaction_t *transaction) {
  cupidbuild_host_snapshot_t parked;
  cupidbuild_host_snapshot_t current;
  unsigned int attempt;
  if (transaction->initial_output_snapshot.present == 0 ||
      transaction->initial_output_parked != 0) {
    return 0;
  }
  for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
    int written = snprintf(
        transaction->initial_output_backup_name,
        sizeof(transaction->initial_output_backup_name),
        ".cupidbuild-old-%08x-%08x", cupidbuild_host_process_id(), attempt);
    if (written < 0 ||
        (size_t)written >= sizeof(transaction->initial_output_backup_name)) {
      return 0;
    }
    if (cupidbuild_host_link_entry_at(
            transaction->output_parent_descriptor, transaction->output_name,
            transaction->output_parent_descriptor,
            transaction->initial_output_backup_name)) {
      transaction->initial_output_parked = 1;
      break;
    }
  }
  if (transaction->initial_output_parked == 0 ||
      !cupidbuild_host_regular_snapshot_at(
          transaction->output_parent_descriptor,
          transaction->initial_output_backup_name, &parked) ||
      !cupidbuild_host_read_output(
          transaction, 0, &current, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(
          &parked, &transaction->initial_output_snapshot) ||
      !cupidbuild_host_snapshot_equal(
          &current, &transaction->initial_output_snapshot)) {
    if (transaction->initial_output_parked != 0 &&
        cupidbuild_host_delete_owned_regular_at(
            transaction->output_parent_descriptor,
            transaction->initial_output_backup_name,
            &transaction->initial_output_snapshot)) {
      transaction->initial_output_parked = 0;
      transaction->initial_output_backup_name[0] = '\0';
    }
    return 0;
  }
  return 1;
}

static int cupidbuild_host_posix_dispose_parked_output(
    cupidbuild_host_transaction_t *transaction) {
  if (transaction->initial_output_parked == 0) {
    return 1;
  }
  if (transaction->initial_output_backup_name[0] == '\0' ||
      !cupidbuild_host_delete_owned_regular_at(
          transaction->output_parent_descriptor,
          transaction->initial_output_backup_name,
          &transaction->initial_output_snapshot)) {
    return 0;
  }
  transaction->initial_output_parked = 0;
  transaction->initial_output_backup_name[0] = '\0';
  return 1;
}

static int cupidbuild_host_posix_atomic_replace(
    cupidbuild_host_transaction_t *transaction) {
  const char *publish_name = cupidbuild_host_private_entry_name(
      transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
  cupidbuild_host_snapshot_t current_output;
  cupidbuild_host_snapshot_t published;
  cupidbuild_host_snapshot_t swapped;
  (void)memset(&current_output, 0, sizeof(current_output));
  (void)memset(&published, 0, sizeof(published));
  (void)memset(&swapped, 0, sizeof(swapped));
  if (!cupidbuild_host_candidate_ready_for_publication(transaction)) {
    cupidbuild_host_set_error(
        transaction, "checked output is no longer ready for publication");
    return 0;
  }
  if (!cupidbuild_host_require_public_binding(
          transaction, &transaction->initial_output_snapshot, 1)) {
    return 0;
  }
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
  if (!cupidbuild_host_publication_test_pause("before-mutation")) {
    cupidbuild_host_set_error(
        transaction, "publication test checkpoint could not resume");
    return 0;
  }
#endif
  if (!cupidbuild_host_read_output(
          transaction, 1, &current_output, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(
          &current_output, &transaction->initial_output_snapshot)) {
    cupidbuild_host_set_error(
        transaction, "output changed immediately before publication");
    return 0;
  }
  if (transaction->initial_output_snapshot.present != 0) {
    int candidate_verified;
    int public_binding;
    int used_exchange = cupidbuild_host_rename_exchange_at(
        transaction->private_descriptor, publish_name,
        transaction->output_parent_descriptor, transaction->output_name);
    if (!used_exchange) {
      if (!cupidbuild_host_posix_park_initial_output(transaction) ||
          !cupidbuild_host_rename_entry_at(
              transaction->private_descriptor, publish_name,
              transaction->output_parent_descriptor,
              transaction->output_name)) {
        (void)cupidbuild_host_posix_dispose_parked_output(transaction);
        cupidbuild_host_set_error(
            transaction, "checked output could not replace the current output");
        return 0;
      }
      transaction->candidate_publish_created = 0;
    }
    transaction->candidate_published = 1;
    {
      int published_read = cupidbuild_host_read_output(
          transaction, 0, &published, (unsigned char **)0);
      int swapped_read = used_exchange
                             ? cupidbuild_host_regular_snapshot_at(
                                   transaction->private_descriptor,
                                   publish_name, &swapped)
                             : cupidbuild_host_regular_snapshot_at(
                                   transaction->output_parent_descriptor,
                                   transaction->initial_output_backup_name,
                                   &swapped);
      candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
                           cupidbuild_host_publication_test_pause(
                               "after-install") &&
#endif
                           published_read && swapped_read &&
                           cupidbuild_host_snapshot_equal(
                               &published,
                               &transaction->candidate_snapshot) &&
                           cupidbuild_host_snapshot_equal(
                               &swapped,
                               &transaction->initial_output_snapshot) &&
                           cupidbuild_host_published_candidate_matches(
                               transaction);
      public_binding = candidate_verified &&
                       cupidbuild_host_require_public_binding(
                           transaction,
                           &transaction->candidate_snapshot, 1);
      if (!public_binding) {
        cupidbuild_host_snapshot_t before_restore;
        cupidbuild_host_snapshot_t before_alias_restore;
        cupidbuild_host_snapshot_t restored_alias;
        int restored =
            candidate_verified &&
            cupidbuild_host_snapshot_equal(
                &published, &transaction->candidate_snapshot) &&
            cupidbuild_host_snapshot_equal(
                &swapped, &transaction->initial_output_snapshot) &&
            cupidbuild_host_read_output(
                transaction, 0, &before_restore, (unsigned char **)0) &&
            cupidbuild_host_snapshot_equal(
                &before_restore, &transaction->candidate_snapshot) &&
            (used_exchange
                 ? (cupidbuild_host_regular_snapshot_at(
                        transaction->private_descriptor, publish_name,
                        &before_alias_restore) &&
                    cupidbuild_host_snapshot_equal(
                        &before_alias_restore,
                        &transaction->initial_output_snapshot) &&
                    cupidbuild_host_rename_exchange_at(
                        transaction->private_descriptor, publish_name,
                        transaction->output_parent_descriptor,
                        transaction->output_name))
                 : (cupidbuild_host_regular_snapshot_at(
                        transaction->output_parent_descriptor,
                        transaction->initial_output_backup_name,
                        &before_alias_restore) &&
                    cupidbuild_host_snapshot_equal(
                        &before_alias_restore,
                        &transaction->initial_output_snapshot) &&
                    cupidbuild_host_rename_entry_at(
                        transaction->output_parent_descriptor,
                        transaction->initial_output_backup_name,
                        transaction->output_parent_descriptor,
                        transaction->output_name)));
        cupidbuild_host_snapshot_t restored_output;
        if (!restored ||
            !cupidbuild_host_read_output(
                transaction, 0, &restored_output,
                (unsigned char **)0) ||
            !(used_exchange
                  ? cupidbuild_host_regular_snapshot_at(
                        transaction->private_descriptor, publish_name,
                        &restored_alias)
                  : cupidbuild_host_regular_snapshot_at(
                        transaction->private_descriptor,
                        cupidbuild_host_private_entry_name(
                            transaction, "candidate.o"),
                        &restored_alias)) ||
            !cupidbuild_host_snapshot_equal(
                &restored_output,
                &transaction->initial_output_snapshot) ||
            !cupidbuild_host_snapshot_equal(
                &restored_alias, &transaction->candidate_snapshot)) {
          transaction->namespace_interfered = 1;
          return 0;
        }
        transaction->candidate_published = 0;
        transaction->candidate_snapshot = restored_alias;
        if (used_exchange) {
          transaction->candidate_publish_created = 1;
          transaction->candidate_publish_snapshot = restored_alias;
        } else {
          transaction->initial_output_parked = 0;
          transaction->initial_output_backup_name[0] = '\0';
          transaction->candidate_publish_created = 0;
          transaction->candidate_publish_snapshot.present = 0;
        }
        if (!cupidbuild_host_require_public_binding(
                transaction, &transaction->initial_output_snapshot, 0)) {
          transaction->namespace_interfered = 1;
        }
        return 0;
      }
    }
    if (used_exchange) {
      transaction->candidate_publish_snapshot = swapped;
    } else {
      transaction->candidate_publish_snapshot.present = 0;
    }
  } else {
    int candidate_verified;
    int public_binding;
    int installation = cupidbuild_host_rename_entry_noreplace_status_at(
        transaction->private_descriptor, publish_name,
        transaction->output_parent_descriptor,
        transaction->output_name);
    if (installation != CUPIDBUILD_HOST_NOREPLACE_COMPLETE) {
      if (installation ==
          CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED) {
        transaction->namespace_interfered = 1;
      }
      cupidbuild_host_set_error(
          transaction, "checked output could not claim the missing output");
      return 0;
    }
    transaction->candidate_publish_created = 0;
    transaction->candidate_published = 1;
    candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && \
    !defined(CUPIDBUILD_CUSTOM_LINUX)
                             cupidbuild_host_publication_test_pause(
                                 "after-install") &&
#endif
                         cupidbuild_host_read_output(
                             transaction, 0, &published,
                             (unsigned char **)0) &&
                         cupidbuild_host_snapshot_equal(
                             &published,
                             &transaction->candidate_snapshot) &&
                         cupidbuild_host_published_candidate_matches(
                             transaction);
    public_binding = candidate_verified &&
                     cupidbuild_host_require_public_binding(
                         transaction,
                         &transaction->candidate_snapshot, 1);
    if (!public_binding) {
      cupidbuild_host_snapshot_t restored_alias;
      cupidbuild_host_snapshot_t restored_missing;
      int restored = candidate_verified &&
                     cupidbuild_host_rename_entry_noreplace_at(
                         transaction->output_parent_descriptor,
                         transaction->output_name,
                         transaction->private_descriptor,
                         publish_name);
      if (!restored ||
          !cupidbuild_host_regular_snapshot_at(
              transaction->private_descriptor,
              publish_name, &restored_alias) ||
          !cupidbuild_host_snapshot_equal(
              &restored_alias, &transaction->candidate_snapshot) ||
          !cupidbuild_host_read_output(
              transaction, 1, &restored_missing,
              (unsigned char **)0) ||
          restored_missing.present != 0) {
        transaction->namespace_interfered = 1;
        return 0;
      }
      transaction->candidate_published = 0;
      transaction->candidate_publish_created = 1;
      transaction->candidate_snapshot = restored_alias;
      transaction->candidate_publish_snapshot = restored_alias;
      if (!cupidbuild_host_require_public_binding(
              transaction, &transaction->initial_output_snapshot, 0)) {
        transaction->namespace_interfered = 1;
      }
      return 0;
    }
    transaction->candidate_publish_snapshot.present = 0;
  }
  transaction->candidate_published = 1;
  transaction->publication_committed = 1;
  if (transaction->initial_output_parked != 0) {
    (void)cupidbuild_host_posix_dispose_parked_output(transaction);
  }
  return 1;
}
#endif

int cupidbuild_host_capture_candidate(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out, unsigned char **bytes_out) {
  cupidbuild_host_snapshot_t candidate;
  unsigned char *bytes = (unsigned char *)0;
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = (unsigned char *)0;
  }
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      transaction->candidate_captured != 0 ||
      !cupidbuild_host_read_candidate(
          transaction, &candidate,
          bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0) ||
      (transaction->candidate_snapshot.present != 0 &&
       !cupidbuild_host_snapshot_equal(
           &candidate, &transaction->candidate_snapshot)) ||
      !cupidbuild_host_candidate_identity_allowed(transaction, &candidate) ||
      !cupidbuild_host_prepare_candidate_publication(
          transaction, &candidate)) {
    free(bytes);
    cupidbuild_host_set_error(transaction, "checked output cannot be pinned");
    return 0;
  }
  transaction->candidate_snapshot = candidate;
  transaction->candidate_captured = 1;
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  }
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
      !cupidbuild_host_read_candidate(
          transaction, &current, (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(&current, expected) ||
      !cupidbuild_host_candidate_identity_allowed(transaction, &current) ||
      !cupidbuild_host_candidate_ready_for_publication(transaction)) {
    cupidbuild_host_set_error(transaction,
                              "checked output changed while validation ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_capture_private_output(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out, unsigned char **bytes_out) {
  cupidbuild_host_snapshot_t captured;
  unsigned char *bytes = (unsigned char *)0;
  int read_captured;
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = (unsigned char *)0;
  }
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    cupidbuild_host_set_error(transaction,
                              "checked private output cannot be pinned");
    return 0;
  }
#if defined(_WIN32)
  read_captured = transaction->private_output_handle != INVALID_HANDLE_VALUE
                      ? cupidbuild_host_read_retained_private_regular(
                            transaction, "candidate.map",
                            CUPIDBUILD_HOST_FILE_LIMIT,
                            transaction->private_output_handle, &captured,
                            bytes_out != (unsigned char **)0
                                ? &bytes
                                : (unsigned char **)0)
                      : cupidbuild_host_read_private_regular(
                            transaction, "candidate.map", 0,
                            CUPIDBUILD_HOST_FILE_LIMIT, &captured,
                            bytes_out != (unsigned char **)0
                                ? &bytes
                                : (unsigned char **)0);
#else
  read_captured = transaction->private_output_descriptor >= 0
                      ? cupidbuild_host_read_retained_private_regular(
                            transaction, "candidate.map",
                            CUPIDBUILD_HOST_FILE_LIMIT,
                            transaction->private_output_descriptor, &captured,
                            bytes_out != (unsigned char **)0
                                ? &bytes
                                : (unsigned char **)0)
                      : cupidbuild_host_read_private_regular(
                            transaction, "candidate.map", 0,
                            CUPIDBUILD_HOST_FILE_LIMIT, &captured,
                            bytes_out != (unsigned char **)0
                                ? &bytes
                                : (unsigned char **)0);
#endif
  if (!read_captured ||
      (transaction->private_output_snapshot.present != 0 &&
       !cupidbuild_host_snapshot_equal(
           &captured, &transaction->private_output_snapshot))) {
    free(bytes);
    cupidbuild_host_set_error(transaction,
                              "checked private output cannot be pinned");
    return 0;
  }
  transaction->private_output_snapshot = captured;
  transaction->private_output_captured = 1;
  if (bytes_out != (unsigned char **)0) {
    *bytes_out = bytes;
  }
  if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
    *snapshot_out = transaction->private_output_snapshot;
  }
  return 1;
}

int cupidbuild_host_require_private_output(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected) {
  cupidbuild_host_snapshot_t current;
  int read_current = 0;
  if (transaction != (cupidbuild_host_transaction_t *)0 &&
      transaction->private_output_captured != 0 &&
      expected != (const cupidbuild_host_snapshot_t *)0) {
#if defined(_WIN32)
    read_current = transaction->private_output_handle != INVALID_HANDLE_VALUE
                       ? cupidbuild_host_read_retained_private_regular(
                             transaction, "candidate.map",
                             CUPIDBUILD_HOST_FILE_LIMIT,
                             transaction->private_output_handle, &current,
                             (unsigned char **)0)
                       : cupidbuild_host_read_private_regular(
                             transaction, "candidate.map", 0,
                             CUPIDBUILD_HOST_FILE_LIMIT, &current,
                             (unsigned char **)0);
#else
    read_current = transaction->private_output_descriptor >= 0
                       ? cupidbuild_host_read_retained_private_regular(
                             transaction, "candidate.map",
                             CUPIDBUILD_HOST_FILE_LIMIT,
                             transaction->private_output_descriptor, &current,
                             (unsigned char **)0)
                       : cupidbuild_host_read_private_regular(
                             transaction, "candidate.map", 0,
                             CUPIDBUILD_HOST_FILE_LIMIT, &current,
                             (unsigned char **)0);
#endif
  }
  if (!read_current ||
      !cupidbuild_host_snapshot_equal(&current, expected)) {
    cupidbuild_host_set_error(
        transaction,
        "checked private output changed while validation ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_write_private_output(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size) {
  cupidbuild_host_snapshot_t snapshot;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      bytes == (const unsigned char *)0 || size > CUPIDBUILD_HOST_FILE_LIMIT ||
      !cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_close_private_output_handle(transaction) ||
      !cupidbuild_host_delete_owned_private_name(
          transaction, "candidate.map",
          &transaction->private_output_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "private checked input cannot be written");
    return 0;
  }
#if !defined(_WIN32)
  if (transaction->private_flat != 0) {
    if (!cupidbuild_host_write_anonymous(
            "cupidbuild-output", bytes, size, transaction->private_output,
            sizeof(transaction->private_output), &snapshot,
            &transaction->private_output_descriptor) ||
        !cupidbuild_host_promote_retained_descriptor(
            &transaction->private_output_descriptor) ||
        !cupidbuild_host_private_entry_path(
            transaction, transaction->private_output,
            sizeof(transaction->private_output), "candidate.map")) {
      cupidbuild_host_close_directory(
          transaction->private_output_descriptor);
      transaction->private_output_descriptor = -1;
      cupidbuild_host_set_error(transaction,
                                "private checked input cannot be written");
      return 0;
    }
    transaction->private_output_sealed = 1;
  } else
#endif
  if (!cupidbuild_host_write_private_exclusive(
          transaction, "candidate.map", bytes, size, 0, &snapshot,
#if defined(_WIN32)
          &transaction->private_output_handle
#else
          &transaction->private_output_descriptor
#endif
          ) ||
      !cupidbuild_host_private_entry_path(
          transaction, transaction->private_output,
          sizeof(transaction->private_output), "candidate.map")) {
    cupidbuild_host_set_error(transaction,
                              "private checked input cannot be written");
    return 0;
  }
  transaction->private_output_snapshot = snapshot;
  transaction->private_output_captured = 0;
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
    if (!cupidbuild_host_read_transaction_input(
            transaction, transaction->inputs[index].live_path,
            CUPIDBUILD_HOST_FILE_LIMIT, &current,
            (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(
            &current, &transaction->inputs[index].snapshot)) {
      cupidbuild_host_set_error(
          transaction,
          index == 0u && transaction->runner_transaction == 0
              ? "source changed while checked tools ran"
              : "checked seed inputs changed while checked tools ran");
      return 0;
    }
  }
  return 1;
}

int cupidbuild_host_require_frozen_inputs(
    cupidbuild_host_transaction_t *transaction) {
  unsigned int index;
  if (transaction == (cupidbuild_host_transaction_t *)0) {
    return 0;
  }
  if (transaction->private_created != 0 &&
      !cupidbuild_host_private_root_is_owned(transaction)) {
    cupidbuild_host_set_error(
        transaction,
        "private checked-tool directory changed while tool ran");
    return 0;
  }
  for (index = 0u; index < transaction->input_count; index++) {
    cupidbuild_host_snapshot_t current;
    int valid;
    if (transaction->runner_transaction != 0) {
#if defined(_WIN32)
      {
        cupidbuild_host_snapshot_t named;
        valid = transaction->inputs[index].frozen_handle !=
                    INVALID_HANDLE_VALUE &&
                cupidbuild_host_windows_read_open_regular(
                    transaction->inputs[index].frozen_handle,
                    CUPIDBUILD_HOST_FILE_LIMIT, &current,
                    (unsigned char **)0) &&
                cupidbuild_host_read_runner_snapshot(
                    transaction, transaction->inputs[index].frozen_path, 0,
                    &named) &&
                cupidbuild_host_snapshot_equal(&current, &named);
      }
#else
      valid = transaction->inputs[index].frozen_descriptor >= 0 &&
              cupidbuild_host_read_open_file(
                  transaction->inputs[index].frozen_descriptor,
                  CUPIDBUILD_HOST_FILE_LIMIT, &current,
                  (unsigned char **)0);
#endif
    } else {
#if defined(_WIN32)
      valid = transaction->inputs[index].frozen_handle !=
                  INVALID_HANDLE_VALUE &&
              cupidbuild_host_read_retained_private_regular(
                  transaction, transaction->inputs[index].frozen_name,
                  CUPIDBUILD_HOST_FILE_LIMIT,
                  transaction->inputs[index].frozen_handle, &current,
                  (unsigned char **)0);
#else
      valid = transaction->inputs[index].frozen_descriptor >= 0 &&
              cupidbuild_host_read_retained_private_regular(
                  transaction, transaction->inputs[index].frozen_name,
                  CUPIDBUILD_HOST_FILE_LIMIT,
                  transaction->inputs[index].frozen_descriptor, &current,
                  (unsigned char **)0);
#endif
    }
    if (!valid ||
        !cupidbuild_host_snapshot_equal(
            &current, &transaction->inputs[index].frozen_snapshot)) {
      if (transaction->runner_transaction != 0) {
        cupidbuild_host_set_error(
            transaction,
            "private checked seed changed while checked tool ran");
      } else {
        (void)snprintf(transaction->error, sizeof(transaction->error),
                       "private checked input %u changed while tool ran",
                       index);
      }
      return 0;
    }
  }
  return 1;
}

static int cupidbuild_host_require_public_binding(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected_output,
    int require_discovery) {
  cupidbuild_host_snapshot_t root;
  cupidbuild_host_snapshot_t pinned_root;
  cupidbuild_host_snapshot_t parent;
  cupidbuild_host_snapshot_t pinned_parent;
  cupidbuild_host_snapshot_t output;
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      expected_output == (const cupidbuild_host_snapshot_t *)0) {
    return 0;
  }
  if (!cupidbuild_host_directory_snapshot(transaction->repository_root,
                                           &root) ||
#if defined(_WIN32)
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->repository_root_handle, &pinned_root) ||
#else
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->repository_root_descriptor, &pinned_root) ||
#endif
      !cupidbuild_host_snapshot_identity_equal(
          &root, &transaction->repository_root_snapshot) ||
      !cupidbuild_host_snapshot_identity_equal(
          &pinned_root, &transaction->repository_root_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "repository root changed while checked tools ran");
    return 0;
  }
  if (!cupidbuild_host_directory_snapshot(transaction->output_parent,
                                           &parent) ||
#if defined(_WIN32)
      !cupidbuild_host_windows_directory_handle_snapshot(
          transaction->output_parent_handle, &pinned_parent) ||
#else
      !cupidbuild_host_directory_descriptor_snapshot(
          transaction->output_parent_descriptor, &pinned_parent) ||
#endif
      memcmp(parent.identity, transaction->output_parent_snapshot.identity,
             sizeof(parent.identity)) != 0 ||
      !cupidbuild_host_snapshot_identity_equal(
          &pinned_parent, &transaction->output_parent_snapshot)) {
    cupidbuild_host_set_error(transaction,
                              "output parent changed while checked tools ran");
    return 0;
  }
  if (!cupidbuild_host_require_lock(transaction)) {
    return 0;
  }
  if (require_discovery != 0 &&
      !cupidbuild_host_require_discovery_directories(transaction)) {
    cupidbuild_host_set_error(
        transaction,
        "discovered directory closure changed while checked tools ran");
    return 0;
  }
  if (!cupidbuild_host_read_output(transaction, 1, &output,
                                   (unsigned char **)0) ||
      !cupidbuild_host_snapshot_equal(
          &output, expected_output)) {
    cupidbuild_host_set_error(transaction,
                              "output changed while checked tools ran");
    return 0;
  }
  return 1;
}

int cupidbuild_host_require_publication_boundary(
    cupidbuild_host_transaction_t *transaction) {
  return transaction != (cupidbuild_host_transaction_t *)0 &&
         cupidbuild_host_require_public_binding(
             transaction, &transaction->initial_output_snapshot, 1);
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
    if (transaction->error[0] == '\0') {
      cupidbuild_host_set_error(transaction,
                                "validated output could not be published");
    }
    return 0;
  }
  return 1;
}

int cupidbuild_host_publish_if_changed(
    cupidbuild_host_transaction_t *transaction, int *changed_out) {
  if (transaction == (cupidbuild_host_transaction_t *)0 ||
      changed_out == (int *)0 || transaction->candidate_captured == 0 ||
      !cupidbuild_host_require_inputs(transaction) ||
      !cupidbuild_host_require_candidate(transaction,
                                         &transaction->candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction)) {
    return 0;
  }
  if (transaction->initial_output_snapshot.present != 0 &&
      transaction->initial_output_snapshot.size ==
          transaction->candidate_snapshot.size &&
      memcmp(transaction->initial_output_snapshot.sha256,
             transaction->candidate_snapshot.sha256,
             sizeof(transaction->candidate_snapshot.sha256)) == 0) {
    if (!cupidbuild_host_require_public_binding(
            transaction, &transaction->initial_output_snapshot, 1)) {
      return 0;
    }
    *changed_out = 0;
    return 1;
  }
  if (!cupidbuild_host_require_lock(transaction) ||
      !cupidbuild_host_atomic_replace(transaction)) {
    if (transaction->error[0] == '\0') {
      cupidbuild_host_set_error(transaction,
                                "validated output could not be published");
    }
    return 0;
  }
  *changed_out = 1;
  return 1;
}

const char *cupidbuild_host_error(
    const cupidbuild_host_transaction_t *transaction) {
  if (transaction == (const cupidbuild_host_transaction_t *)0 ||
      transaction->error[0] == '\0') {
    return "hosted CupidBuild transaction failed";
  }
  return transaction->error;
}
