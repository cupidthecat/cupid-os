/**
 * @file cupidbuild_host.cc
 * @brief Retain hosted build inputs and publish validated outputs.
 */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(_WIN32) &&                                                                             \
    (defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) ||                                            \
     defined(CUPIDBUILD_PUBLICATION_RACE_TEST) ||                                                  \
     defined(CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST)) &&                                         \
    !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cupidbuild_host.h"

unsigned int cupidbuild_host_execution_format(void) {
#ifdef _WIN32
    return 2u;
#else
    return 1u;
#endif
}

#include "path_encoding.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
#include <cupid_runtime.h>
#endif

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
#define CUPIDBUILD_LINUX_SYS_PREAD64 180
#define CUPIDBUILD_LINUX_SYS_PWRITE64 181
#define CUPIDBUILD_LINUX_SYS_FTRUNCATE64 194
#define CUPIDBUILD_LINUX_EINTR 4
int cupid_linux_syscall0(int number);
int cupid_linux_syscall1(int number, unsigned int first);
int cupid_linux_syscall2(int number, unsigned int first, unsigned int second);
int cupid_linux_syscall3(int number, unsigned int first, unsigned int second, unsigned int third);
int cupid_linux_syscall4(int number, unsigned int first, unsigned int second, unsigned int third,
                         unsigned int fourth);
int cupid_linux_syscall5(int number, unsigned int first, unsigned int second, unsigned int third,
                         unsigned int fourth, unsigned int fifth);
#endif
#endif

#ifdef CUPID_NATIVE_UTF8_ENABLE
#include "native_utf8.h"
#endif

#define CUPIDBUILD_HOST_PATH_BYTES 8192u
#define CUPIDBUILD_HOST_ERROR_BYTES 512u
#define CUPIDBUILD_HOST_INPUTS 528u
#define CUPIDBUILD_HOST_DISK_INPUTS 4112u
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
    char *live_path;
    char *frozen_path;
    char *frozen_name;
    cupidbuild_host_snapshot_t snapshot;
    cupidbuild_host_snapshot_t frozen_snapshot;
    unsigned long long limit;
    int previous_output;
    int external_input;
    int frozen_tool_output;
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

typedef struct {
    cupidbuild_host_path_list_t roots;
    cupidbuild_host_path_list_t suffixes;
    cupidbuild_host_path_list_t expected;
    int skip_hidden_files;
    int reject_matching_nonfiles;
} cupidbuild_host_discovery_query_t;

typedef enum {
    CUPIDBUILD_CANDIDATE_STORE_CLOSED,
    CUPIDBUILD_CANDIDATE_STORE_WRITING,
    CUPIDBUILD_CANDIDATE_STORE_FINISHED
} cupidbuild_candidate_store_state_t;

typedef struct {
    cupidbuild_host_observer_t *observer;
    int root_identity_only;
} cupidbuild_host_external_observer_t;

struct cupidbuild_host_transaction {
    cupidbuild_host_output_parent_t *prepared_output_parent;
    cupidbuild_host_observer_t *borrowed_observer;
    int observer_binding_state;
    int observer_publication_started;
    cupidbuild_host_external_observer_t external_observers[CUPIDBUILD_HOST_INPUTS];
    size_t external_observer_count;
    int external_binding_failed;
    const cupidbuild_host_source_request_set_t *source_requests;
    int source_request_state;
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
    unsigned int input_limit;
    size_t discovery_directory_count;
    size_t discovery_directory_capacity;
    cupidbuild_host_snapshot_t output_parent_snapshot;
    cupidbuild_host_snapshot_t initial_output_snapshot;
    cupidbuild_host_snapshot_t candidate_snapshot;
    cupidbuild_host_snapshot_t candidate_publish_snapshot;
    cupidbuild_host_snapshot_t private_output_snapshot;
    size_t private_output_limit;
    unsigned long long candidate_limit;
    cupidbuild_host_snapshot_t lock_snapshot;
    cupidbuild_host_snapshot_t private_root_snapshot;
    cupidbuild_host_snapshot_t private_reservation_snapshot;
    cupidbuild_host_snapshot_t repository_root_snapshot;
    cupidbuild_host_snapshot_t tool_stdout_snapshot;
    cupidbuild_host_snapshot_t tool_stderr_snapshot;
    unsigned int candidate_store_lock;
    cupidbuild_candidate_store_state_t candidate_store_state;
    int candidate_store_failed;
    int candidate_captured;
    int candidate_sealed;
    int candidate_publish_created;
    int candidate_published;
    int private_output_captured;
    int private_output_ready;
    int private_output_transferred;
    int private_output_sealed;
    int tool_stdout_sealed;
    int tool_stderr_sealed;
    int lock_held;
    int private_created;
    int private_flat;
    int output_parent_is_repository_root;
    int initial_output_parked;
    int previous_output_state;
    int runner_transaction;
    int captured;
    int discovery_sealed;
    int compile_discovery;
    cupidbuild_host_discovery_query_t discovery_queries[16];
    size_t discovery_query_count;
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
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
    int initial_output_descriptor;
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
static int cupidbuild_host_windows_read_open_regular(HANDLE handle, unsigned long long limit,
                                                     cupidbuild_host_snapshot_t *snapshot,
                                                     unsigned char **bytes_out);
static int cupidbuild_host_windows_dispose_retained_at(HANDLE parent, const char *name,
                                                       cupidbuild_host_snapshot_t *expected,
                                                       HANDLE *retained_handle);
#endif
static int
cupidbuild_host_candidate_ready_for_publication(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_published_candidate_matches(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_require_borrowed_observer(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_require_external_observers(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_require_source_requests(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_read_external_input(cupidbuild_host_observer_t *observer,
                                              const char *logical, unsigned long long capacity,
                                              cupidbuild_host_snapshot_t *snapshot);
static int cupidbuild_host_require_public_binding(cupidbuild_host_transaction_t *transaction,
                                                  const cupidbuild_host_snapshot_t *expected_output,
                                                  int require_discovery);
static int cupidbuild_output_parent_attach(cupidbuild_host_output_parent_t *parent,
                                           cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_read_output(cupidbuild_host_transaction_t *transaction, int optional,
                                       cupidbuild_host_snapshot_t *snapshot,
                                       unsigned char **bytes_out);
static int cupidbuild_host_private_root_is_owned(cupidbuild_host_transaction_t *transaction);
#if !defined(_WIN32)
static int cupidbuild_host_posix_atomic_replace(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_posix_dispose_parked_output(cupidbuild_host_transaction_t *transaction);
static int cupidbuild_host_directory_entry_missing_at(int directory, const char *name);
static int cupidbuild_host_read_open_file(int descriptor, unsigned long long limit,
                                          cupidbuild_host_snapshot_t *snapshot,
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

static int cupidbuild_host_repository_logical_path(const cupidbuild_host_transaction_t *transaction,
                                                   const char *path, const char **logical_out);

typedef unsigned int cupidbuild_sha_word_t;

static const cupidbuild_sha_word_t cupidbuild_sha_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static cupidbuild_sha_word_t cupidbuild_sha_rotate(cupidbuild_sha_word_t value,
                                                   unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

static cupidbuild_sha_word_t cupidbuild_sha_load(const unsigned char *bytes) {
    return ((cupidbuild_sha_word_t)bytes[0] << 24u) | ((cupidbuild_sha_word_t)bytes[1] << 16u) |
           ((cupidbuild_sha_word_t)bytes[2] << 8u) | (cupidbuild_sha_word_t)bytes[3];
}

static void cupidbuild_sha_block(cupidbuild_sha_word_t state[8], const unsigned char block[64]) {
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
        cupidbuild_sha_word_t left = cupidbuild_sha_rotate(words[index - 15u], 7u) ^
                                     cupidbuild_sha_rotate(words[index - 15u], 18u) ^
                                     (words[index - 15u] >> 3u);
        cupidbuild_sha_word_t right = cupidbuild_sha_rotate(words[index - 2u], 17u) ^
                                      cupidbuild_sha_rotate(words[index - 2u], 19u) ^
                                      (words[index - 2u] >> 10u);
        words[index] = words[index - 16u] + left + words[index - 7u] + right;
    }
    for (index = 0u; index < 64u; index++) {
        cupidbuild_sha_word_t choose = (fifth & sixth) ^ ((~fifth) & seventh);
        cupidbuild_sha_word_t majority = (first & second) ^ (first & third) ^ (second & third);
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

typedef struct {
    cupidbuild_sha_word_t state[8];
    unsigned char partial[64];
    size_t used;
    uint64_t size;
} cupidbuild_sha_context_t;

static void cupidbuild_sha_initialize(cupidbuild_sha_context_t *context) {
    static const cupidbuild_sha_word_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                                     0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                                     0x1f83d9abu, 0x5be0cd19u};
    (void)memcpy(context->state, initial, sizeof(initial));
    context->used = 0u;
    context->size = 0u;
}

static void cupidbuild_sha_update(cupidbuild_sha_context_t *context, const unsigned char *contents,
                                  size_t size) {
    context->size += (uint64_t)size;
    if (context->used != 0u) {
        size_t count = 64u - context->used;
        if (count > size)
            count = size;
        if (count != 0u)
            (void)memcpy(context->partial + context->used, contents, count);
        context->used += count;
        contents += count;
        size -= count;
        if (context->used == 64u) {
            cupidbuild_sha_block(context->state, context->partial);
            context->used = 0u;
        }
    }
    while (size >= 64u) {
        cupidbuild_sha_block(context->state, contents);
        contents += 64u;
        size -= 64u;
    }
    if (size != 0u) {
        (void)memcpy(context->partial, contents, size);
        context->used = size;
    }
}

static void cupidbuild_sha_finish(cupidbuild_sha_context_t *context, unsigned char digest[32]) {
    unsigned char tail[128];
    size_t remaining = context->used;
    uint64_t bit_count = context->size * 8u;
    size_t index;
    for (index = 0u; index < remaining; index++) {
        tail[index] = context->partial[index];
    }
    tail[remaining++] = 0x80u;
    while ((remaining & 63u) != 56u) {
        tail[remaining++] = 0u;
    }
    for (index = 0u; index < 8u; index++) {
        tail[remaining + index] = (unsigned char)(bit_count >> ((7u - index) * 8u));
    }
    remaining += 8u;
    cupidbuild_sha_block(context->state, tail);
    if (remaining == 128u) {
        cupidbuild_sha_block(context->state, tail + 64u);
    }
    for (index = 0u; index < 8u; index++) {
        digest[index * 4u] = (unsigned char)(context->state[index] >> 24u);
        digest[index * 4u + 1u] = (unsigned char)(context->state[index] >> 16u);
        digest[index * 4u + 2u] = (unsigned char)(context->state[index] >> 8u);
        digest[index * 4u + 3u] = (unsigned char)context->state[index];
    }
}

static void cupidbuild_sha256(const unsigned char *contents, size_t size,
                              unsigned char digest[32]) {
    cupidbuild_sha_context_t context;
    cupidbuild_sha_initialize(&context);
    cupidbuild_sha_update(&context, contents, size);
    cupidbuild_sha_finish(&context, digest);
}

void cupidbuild_host_sha256_bytes(const unsigned char *contents, size_t size,
                                  unsigned char digest[32]) {
    cupidbuild_sha256(contents, size, digest);
}

static int cupidbuild_host_copy_text(char *destination, size_t capacity, const char *source) {
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

static int cupidbuild_host_join(char *destination, size_t capacity, const char *left,
                                const char *right) {
    size_t left_size = strlen(left);
    size_t right_size = strlen(right);
    int separator =
        left_size != 0u && left[left_size - 1u] != '/' && left[left_size - 1u] != '\\' ? 1 : 0;
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

/* Read the captured extent through a retained ordinary-file handle. Payload
 * storage belongs to the caller only when requested; digest-only checks use a
 * fixed block. Callers retain their existing metadata and extent checks. */
static int cupidbuild_host_read_open_contents(
#if defined(_WIN32)
    HANDLE handle,
#else
    int descriptor,
#endif
    unsigned long long size, unsigned char **bytes_out, unsigned char digest[32]) {
    unsigned char block[65536];
    unsigned char *bytes = (unsigned char *)0;
    cupidbuild_sha_context_t context;
    unsigned long long offset = 0u;
#if defined(_WIN32)
    if (SetFilePointer(handle, 0, 0, FILE_BEGIN) != 0u)
        return 0;
#endif
    if (size > CUPIDBUILD_HOST_MAX_EXTENT)
        return 0;
    if (bytes_out != (unsigned char **)0) {
        if (size >= (size_t)-1)
            return 0;
        bytes = (unsigned char *)malloc((size_t)size + 1u);
        if (bytes == (unsigned char *)0)
            return 0;
    }
    cupidbuild_sha_initialize(&context);
    while (offset < size) {
        unsigned long long remaining = size - offset;
        size_t chunk = remaining > sizeof(block) ? sizeof(block) : (size_t)remaining;
        size_t count;
        unsigned char *destination = bytes != (unsigned char *)0 ? bytes + (size_t)offset : block;
#if defined(_WIN32)
        DWORD read_bytes = 0u;
        if (!ReadFile(handle, destination, (DWORD)chunk, &read_bytes, (LPOVERLAPPED)0)) {
            free(bytes);
            return 0;
        }
        count = (size_t)read_bytes;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
        int read_bytes;
        do {
            read_bytes = cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_PREAD64,
                                              (unsigned int)descriptor, (unsigned int)destination,
                                              (unsigned int)chunk, (unsigned int)offset,
                                              (unsigned int)(offset >> 32u));
        } while (read_bytes == -CUPIDBUILD_LINUX_EINTR);
        count = read_bytes > 0 ? (size_t)read_bytes : 0u;
#else
        ssize_t read_bytes;
        do {
            read_bytes = pread(descriptor, destination, chunk, (off_t)offset);
        } while (read_bytes < 0 && errno == EINTR);
        count = read_bytes > 0 ? (size_t)read_bytes : 0u;
#endif
        if (count == 0u || count > chunk) {
            free(bytes);
            return 0;
        }
        cupidbuild_sha_update(&context, destination, count);
        offset += count;
    }
    cupidbuild_sha_finish(&context, digest);
    if (bytes_out != (unsigned char **)0) {
        bytes[(size_t)size] = 0u;
        *bytes_out = bytes;
    }
    return 1;
}

static void cupidbuild_host_set_error(cupidbuild_host_transaction_t *transaction,
                                      const char *message) {
    if (transaction != (cupidbuild_host_transaction_t *)0) {
        (void)cupidbuild_host_copy_text(transaction->error, sizeof(transaction->error), message);
    }
}

int cupidbuild_host_snapshot_equal(const cupidbuild_host_snapshot_t *left,
                                   const cupidbuild_host_snapshot_t *right) {
    return left->present == right->present && left->size == right->size &&
           memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
           memcmp(left->identity, right->identity, sizeof(left->identity)) == 0 &&
           memcmp(left->modified, right->modified, sizeof(left->modified)) == 0 &&
           memcmp(left->changed, right->changed, sizeof(left->changed)) == 0;
}

static int cupidbuild_host_snapshot_identity_equal(const cupidbuild_host_snapshot_t *left,
                                                   const cupidbuild_host_snapshot_t *right) {
    return left->present != 0 && right->present != 0 &&
           memcmp(left->identity, right->identity, sizeof(left->identity)) == 0;
}

static int cupidbuild_host_lock_snapshot_equal(const cupidbuild_host_snapshot_t *left,
                                               const cupidbuild_host_snapshot_t *right) {
    return left->present == right->present && left->size == right->size &&
           memcmp(left->sha256, right->sha256, sizeof(left->sha256)) == 0 &&
           memcmp(left->identity, right->identity, sizeof(left->identity)) == 0;
}

#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_publication_test_pause(const char *phase) {
    const char *requested = getenv("CUPIDBUILD_PUBLICATION_TEST_PHASE");
    const char *ready = getenv("CUPIDBUILD_PUBLICATION_TEST_READY");
    const char *resume = getenv("CUPIDBUILD_PUBLICATION_TEST_RESUME");
    FILE *signal;
    unsigned int attempt;
    if (strcmp(phase, "rollback-directory-bridge") == 0) {
        ready = getenv("CUPIDBUILD_PUBLICATION_TEST_BRIDGE_READY");
        resume = getenv("CUPIDBUILD_PUBLICATION_TEST_BRIDGE_RESUME");
        requested = ready != (const char *)0 ? phase : (const char *)0;
    }
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
    if (path == (const char *)0 || path[0] == '\0' || path[0] == '/' || path[0] == '\\' ||
        path[1] == ':') {
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

static int cupidbuild_host_parent(char *destination, size_t capacity, const char *path) {
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

static int cupidbuild_host_basename(char *destination, size_t capacity, const char *path) {
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

static int cupidbuild_host_name_has_suffix(const char *name, const char *suffix) {
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

static int cupidbuild_host_name_is_expected(const char *name, const char *const *expected,
                                            size_t expected_count) {
    size_t index;
    for (index = 0u; index < expected_count; index++) {
        if (strcmp(name, expected[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cupidbuild_host_discovery_suffix(const char *name, const char *const *suffixes,
                                            size_t suffix_count) {
    size_t index;
    size_t name_size = strlen(name);
    for (index = 0u; index < suffix_count; index++) {
        size_t suffix_size = strlen(suffixes[index]);
        if (suffix_size <= name_size &&
            memcmp(name + name_size - suffix_size, suffixes[index], suffix_size) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cupidbuild_host_path_list_add(cupidbuild_host_path_list_t *paths, const char *path,
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

static int cupidbuild_host_discovery_worklist_add(cupidbuild_host_discovery_worklist_t *worklist,
                                                  const char *logical,
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
        grown = (cupidbuild_host_discovery_directory_t *)realloc(worklist->directories,
                                                                 capacity * sizeof(*grown));
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

static int cupidbuild_host_discovery_snapshot_equal(cupidbuild_host_transaction_t *transaction,
                                                    const cupidbuild_host_snapshot_t *left,
                                                    const cupidbuild_host_snapshot_t *right) {
    if (transaction->compile_discovery != 0) {
        return cupidbuild_host_snapshot_identity_equal(left, right);
    }
    return cupidbuild_host_snapshot_equal(left, right);
}

static int
cupidbuild_host_bind_discovery_directory(cupidbuild_host_transaction_t *transaction,
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
            return cupidbuild_host_discovery_snapshot_equal(transaction, &expected->snapshot,
                                                            &directory->snapshot);
        }
        if (cupidbuild_host_snapshot_identity_equal(&expected->snapshot, &directory->snapshot)) {
            return 0;
        }
    }
    if (transaction->discovery_sealed != 0 ||
        transaction->discovery_directory_count >= CUPIDBUILD_HOST_RETAINED_DIRECTORIES) {
        return 0;
    }
    if (transaction->discovery_directory_count == transaction->discovery_directory_capacity) {
        capacity = transaction->discovery_directory_capacity == 0u
                       ? 64u
                       : transaction->discovery_directory_capacity * 2u;
        if (capacity > CUPIDBUILD_HOST_RETAINED_DIRECTORIES) {
            capacity = CUPIDBUILD_HOST_RETAINED_DIRECTORIES;
        }
        grown = (cupidbuild_host_discovery_directory_t *)realloc(transaction->discovery_directories,
                                                                 capacity * sizeof(*grown));
        if (grown == (cupidbuild_host_discovery_directory_t *)0) {
            return 0;
        }
        transaction->discovery_directories = grown;
        transaction->discovery_directory_capacity = capacity;
    }
    transaction->discovery_directories[transaction->discovery_directory_count++] = *directory;
    *retained_out = 1;
    return 1;
}

static int cupidbuild_host_discovery_add(cupidbuild_host_path_list_t *paths, const char *path,
                                         const cupidbuild_host_snapshot_t *snapshot) {
    return cupidbuild_host_path_list_add(paths, path, snapshot, CUPIDBUILD_HOST_DISCOVERY_FILES);
}

static void cupidbuild_host_discovery_swap(cupidbuild_host_path_list_t *paths, size_t left,
                                           size_t right) {
    char *temporary_path = paths->paths[left];
    cupidbuild_host_snapshot_t temporary_snapshot = paths->snapshots[left];
    paths->paths[left] = paths->paths[right];
    paths->snapshots[left] = paths->snapshots[right];
    paths->paths[right] = temporary_path;
    paths->snapshots[right] = temporary_snapshot;
}

static void cupidbuild_host_discovery_sift(cupidbuild_host_path_list_t *paths, size_t root,
                                           size_t count) {
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
        if (child + 1u < count && strcmp(paths->paths[selected], paths->paths[child + 1u]) < 0) {
            selected = child + 1u;
        }
        if (selected == root) {
            return;
        }
        cupidbuild_host_discovery_swap(paths, root, selected);
        root = selected;
    }
}

static int cupidbuild_host_discovery_sort(cupidbuild_host_path_list_t *paths) {
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
            if (!cupidbuild_host_snapshot_equal(&paths->snapshots[write_index - 1u],
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
#include "cupidbuild_host_windows.inc"

#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
#include "cupidbuild_host_linux.inc"

#else
#include "cupidbuild_host_posix.inc"

#endif
#endif

#include "cupidbuild_host_profile_parent.inc"

#include "cupidbuild_host_publication_lock.inc"

#include "cupidbuild_host_private_workspace.inc"

#include "cupidbuild_host_input_snapshots.inc"

#include "cupidbuild_host_transaction_lifetime.inc"

#include "cupidbuild_host_checked_process.inc"

#include "cupidbuild_host_candidate_store.inc"
#include "cupidbuild_host_frozen_inputs.inc"

#include "cupidbuild_host_publication.inc"

#include "cupidbuild_host_observer.inc"

#include "cupidbuild_host_observer_inventory.inc"

#include "cupidbuild_host_observer_workers.inc"

#include "cupidbuild_host_external_inputs.inc"

#include "cupidbuild_host_observer_binding.inc"

#include "cupidbuild_host_source_paths.inc"
