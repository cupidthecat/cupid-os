#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST)
static int allocation_failure = -1;
static unsigned int allocation_index;
static unsigned int failure_phase;
static unsigned int member_index;
static int capture_pause;
int cupidbuild_directory_snapshot_test_checkpoint(unsigned int phase) {
  unsigned char resume;
  if (phase == 1u && allocation_failure >= 0 &&
      allocation_index++ == (unsigned int)allocation_failure) return 1;
  if (phase == 2u && failure_phase == 2u) return member_index++ == 1u;
  if (phase == failure_phase) return 1;
  if (phase == 4u && capture_pause) {
    (void)printf("ready\n");
    (void)fflush(stdout);
    if (fread(&resume, 1u, 1u, stdin) != 1u || resume != 'f') return 1;
  }
  return 0;
}
#endif

static int unhex(const char *input, char *output, size_t capacity) {
  size_t count = 0u;
  while (*input != 0) {
    unsigned int value = 0u;
    unsigned int index;
    for (index = 0u; index < 2u; index++) {
      unsigned char digit = (unsigned char)*input++;
      if (digit >= '0' && digit <= '9') value = value * 16u + digit - '0';
      else if (digit >= 'a' && digit <= 'f') value = value * 16u + digit - 'a' + 10u;
      else return 0;
    }
    if (value == 0u || count + 1u >= capacity) return 0;
    output[count++] = (char)value;
  }
  output[count] = 0;
  return 1;
}

static void print_names(const char *const *names, size_t count) {
  static const char digits[] = "0123456789abcdef";
  size_t index;
  (void)printf("count %u\n", (unsigned int)count);
  for (index = 0u; index < count; index++) {
    const unsigned char *cursor = (const unsigned char *)names[index];
    while (*cursor != 0u) {
      char pair[2];
      pair[0] = digits[*cursor >> 4u];
      pair[1] = digits[*cursor & 15u];
      (void)fwrite(pair, 1u, 2u, stdout);
      cursor++;
    }
    (void)printf("\n");
  }
}

#define REQUIRE(condition) do { if (!(condition)) { \
  (void)fprintf(stderr, "snapshot contract %s failed at %d: %s\n", \
                mode, __LINE__, cupidbuild_host_observer_error(observer)); \
  free(sentinel); \
  (void)cupidbuild_host_observer_close(other); \
  (void)cupidbuild_host_observer_close(observer); return 1; \
} } while (0)

int main(int argc, char **argv) {
  cupidbuild_host_observer_t *observer = NULL;
  cupidbuild_host_observer_t *other = NULL;
  const cupidbuild_host_directory_snapshot_t *snapshot = NULL;
  const cupidbuild_host_directory_snapshot_t *second = NULL;
  const char *const *names = NULL;
  const char *const *first_names = NULL;
  const char *mode;
  char root[8192];
  char logical[8192];
  size_t count = 0u;
  size_t first_count = 0u;
  size_t index;
  int rejected = 0;
  int observed;
  unsigned char resume;
  unsigned char *payload = NULL;
  void *sentinel = NULL;
  uint64_t size = 0u;
  if (argc != 4) return 2;
  mode = argv[2];
  REQUIRE(unhex(argv[1], root, sizeof(root)) &&
          unhex(argv[3], logical, sizeof(logical)));
  REQUIRE(cupidbuild_host_observer_open(root, &observer));
  sentinel = malloc(1u);
  REQUIRE(sentinel != NULL);
  snapshot = (const cupidbuild_host_directory_snapshot_t *)sentinel;
  if (strncmp(mode, "fault-", 6u) == 0) {
#if defined(CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST)
    if (strncmp(mode, "fault-alloc-", 12u) == 0) {
      const char *text = mode + 12u;
      char *end = NULL;
      unsigned long long requested;
      REQUIRE(text[0] >= '0' && text[0] <= '9');
      requested = strtoull(text, &end, 10);
      REQUIRE(end != NULL && end != text && *end == 0 && requested <= 2147483647u);
      allocation_failure = (int)requested;
    } else if (strcmp(mode, "fault-enumerate") == 0) failure_phase = 2u;
    else if (strcmp(mode, "fault-complete") == 0) failure_phase = 3u;
    else if (strcmp(mode, "fault-bound") == 0) failure_phase = 4u;
    else if (strcmp(mode, "fault-capture-wait") == 0) capture_pause = 1;
    else REQUIRE(0);
    rejected = 1;
#else
    (void)fprintf(stderr, "fault mode requires the separate test adapter\n");
    free(sentinel);
    (void)cupidbuild_host_observer_close(observer);
    return 2;
#endif
  } else if (strcmp(mode, "expected-first") == 0) {
    const char *expected[2] = {"b", "a"};
    REQUIRE(cupidbuild_host_observer_directory(observer, logical, expected, 2u));
  } else if (strcmp(mode, "membership-limit") == 0) {
    const char *expected[1] = {"nested"};
    REQUIRE(cupidbuild_host_observer_directory(observer, "", expected, 1u));
    rejected = 1;
  } else if (strcmp(mode, "prepoison") == 0) {
    REQUIRE(!cupidbuild_host_observer_file(observer, "absent", 1u, &payload, &size));
    rejected = 1;
  } else if (strcmp(mode, "null-observer") == 0) {
    REQUIRE(!cupidbuild_host_observer_directory_snapshot(NULL, logical, &snapshot));
    REQUIRE(snapshot == NULL && cupidbuild_host_observer_require_unchanged(observer));
    names = (const char *const *)sentinel;
    count = 99u;
    REQUIRE(!cupidbuild_host_observer_directory_snapshot_names(
        NULL, NULL, &names, &count));
    REQUIRE(names == NULL && count == 0u);
    free(sentinel);
    sentinel = NULL;
    return cupidbuild_host_observer_close(observer) ? 0 : 3;
  } else if (strcmp(mode, "reject") == 0 || strcmp(mode, "null-out") == 0 ||
             strcmp(mode, "null-path") == 0) rejected = 1;
  observed = cupidbuild_host_observer_directory_snapshot(observer,
      strcmp(mode, "null-path") == 0 ? NULL : logical,
      strcmp(mode, "null-out") == 0 ? NULL : &snapshot);
  free(sentinel);
  sentinel = NULL;
  if (rejected) {
    REQUIRE(!observed && (strcmp(mode, "null-out") == 0 || snapshot == NULL));
    snapshot = NULL;
  } else {
    REQUIRE(observed && snapshot != NULL);
    REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
        observer, snapshot, &names, &count));
    REQUIRE((count == 0u) == (names == NULL));
    for (index = 1u; index < count; index++)
      REQUIRE(strcmp(names[index - 1u], names[index]) < 0);
    first_names = names;
    first_count = count;
    if (strcmp(mode, "null-token") == 0 || strcmp(mode, "foreign") == 0 ||
        strcmp(mode, "unknown-token") == 0 || strcmp(mode, "null-names") == 0 ||
        strcmp(mode, "null-count") == 0) {
      const cupidbuild_host_directory_snapshot_t *selected = snapshot;
      void *unknown = NULL;
      if (strcmp(mode, "foreign") == 0) {
        REQUIRE(cupidbuild_host_observer_open(root, &other));
        REQUIRE(cupidbuild_host_observer_directory_snapshot(other, logical, &second));
        selected = second;
      } else if (strcmp(mode, "null-token") == 0) selected = NULL;
      else if (strcmp(mode, "unknown-token") == 0) {
        unknown = malloc(1u);
        REQUIRE(unknown != NULL);
        selected = (const cupidbuild_host_directory_snapshot_t *)unknown;
      }
      names = first_names;
      count = 99u;
      observed = cupidbuild_host_observer_directory_snapshot_names(observer, selected,
          strcmp(mode, "null-names") == 0 ? NULL : &names,
          strcmp(mode, "null-count") == 0 ? NULL : &count);
      free(unknown);
      REQUIRE(!observed);
      REQUIRE(strcmp(mode, "null-names") == 0 || names == NULL);
      REQUIRE(strcmp(mode, "null-count") == 0 || count == 0u);
      if (other != NULL) {
        REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
            other, second, &names, &count));
        REQUIRE(cupidbuild_host_observer_close(other));
        other = NULL;
      }
      rejected = 1;
    } else if (strcmp(mode, "repeat-root") == 0) {
      REQUIRE(cupidbuild_host_observer_directory(observer, "", names, count));
      REQUIRE(cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(second != snapshot);
      REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
          observer, snapshot, &names, &count));
      REQUIRE(names == first_names && count == first_count);
      print_names(names, count);
    } else if (strcmp(mode, "token-limit") == 0) {
      REQUIRE(count == 0u);
      for (index = 1u; index < 4096u; index++)
        REQUIRE(cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
          observer, snapshot, &names, &count));
      REQUIRE(!cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(second == NULL);
      rejected = 1;
    } else if (strcmp(mode, "single-copy-limit") == 0) {
      REQUIRE(count == 1u);
      for (index = 1u; index < 4096u; index++)
        REQUIRE(cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
          observer, snapshot, &names, &count));
      REQUIRE(names == first_names && count == 1u);
      REQUIRE(!cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(second == NULL);
      rejected = 1;
    } else if (strcmp(mode, "copy-limit") == 0) {
      REQUIRE(count == 2048u);
      REQUIRE(cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
          observer, snapshot, &names, &count));
      REQUIRE(names == first_names && count == 2048u);
      REQUIRE(!cupidbuild_host_observer_directory_snapshot(observer, "", &second));
      REQUIRE(second == NULL);
      rejected = 1;
    } else if (strncmp(mode, "wait", 4u) == 0) {
      if (strcmp(mode, "wait-payload") == 0) {
        REQUIRE(cupidbuild_host_observer_file(observer, "nested/payload", 3u,
                                             &payload, &size));
        observed = size == 3u && memcmp(payload, "one", 3u) == 0;
        free(payload);
        payload = NULL;
        REQUIRE(observed);
      }
      REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
      (void)printf("ready\n");
      (void)fflush(stdout);
      REQUIRE(fread(&resume, 1u, 1u, stdin) == 1u && (resume == 'p' || resume == 'f'));
      if (strcmp(mode, "wait-recapture") == 0) {
        observed = cupidbuild_host_observer_directory_snapshot(observer, logical, &second);
        REQUIRE((observed != 0) == (resume == 'p'));
        REQUIRE(observed || second == NULL);
      } else if (strcmp(mode, "wait-expected") == 0) {
        observed = cupidbuild_host_observer_directory(observer, logical, names, count);
        REQUIRE((observed != 0) == (resume == 'p'));
      }
      names = first_names;
      count = 99u;
      observed = cupidbuild_host_observer_directory_snapshot_names(
          observer, snapshot, &names, &count);
      REQUIRE((observed != 0) == (resume == 'p'));
      if (observed) REQUIRE(names == first_names && count == first_count);
      else REQUIRE(names == NULL && count == 0u);
      rejected = resume == 'f';
    } else {
      REQUIRE(strcmp(mode, "list") == 0 || strcmp(mode, "expected-first") == 0);
      print_names(names, count);
    }
  }
#if defined(CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST)
  allocation_failure = -1;
  failure_phase = 0u;
  capture_pause = 0;
#endif
  if (rejected) {
    REQUIRE(cupidbuild_host_observer_error(observer)[0] != 0);
    REQUIRE(!cupidbuild_host_observer_require_unchanged(observer));
    REQUIRE(!cupidbuild_host_observer_directory_snapshot(observer, "", &second));
    REQUIRE(second == NULL);
    names = first_names;
    count = 99u;
    REQUIRE(!cupidbuild_host_observer_directory_snapshot_names(
        observer, snapshot, &names, &count));
    REQUIRE(names == NULL && count == 0u);
    REQUIRE(cupidbuild_host_observer_close(observer));
    observer = NULL;
    /* Fresh lifetime recovery uses metadata only, so a deliberately unsafe
     * child name in the failed membership does not poison the new observer. */
    REQUIRE(cupidbuild_host_observer_open(root, &observer));
    REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
    if (strncmp(mode, "fault-", 6u) == 0) {
      REQUIRE(cupidbuild_host_observer_directory_snapshot(observer, logical, &second));
      REQUIRE(cupidbuild_host_observer_directory_snapshot_names(
          observer, second, &names, &count));
    }
  } else REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
  return cupidbuild_host_observer_close(observer) ? 0 : 3;
}
