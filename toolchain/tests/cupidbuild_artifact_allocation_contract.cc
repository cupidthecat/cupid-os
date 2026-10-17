/* Test-only allocation injection around the unchanged verifier translation unit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int allocation_calls;
static unsigned int fail_call;
static void *retained[16];
static unsigned int retained_count;

static void remember(void *pointer) {
  if (pointer == NULL) return;
  if (retained_count == 16u) abort();
  retained[retained_count++] = pointer;
}
static void forget(void *pointer) {
  unsigned int i;
  for (i = 0u; i < retained_count; i++) {
    if (retained[i] == pointer) {
      retained[i] = retained[--retained_count];
      return;
    }
  }
}
static void *fault_malloc(size_t size) {
  void *pointer;
  allocation_calls++;
  if (allocation_calls == fail_call) return NULL;
  pointer = malloc(size);
  remember(pointer);
  return pointer;
}
static void *fault_realloc(void *previous, size_t size) {
  void *pointer;
  allocation_calls++;
  if (allocation_calls == fail_call) return NULL;
  /* Forget before realloc so no comparison uses a released pointer value. */
  forget(previous);
  pointer = realloc(previous, size);
  if (pointer == NULL) remember(previous);
  else remember(pointer);
  return pointer;
}
static void fault_free(void *pointer) {
  forget(pointer);
  free(pointer);
}
#define malloc fault_malloc
#define realloc fault_realloc
#define free fault_free
#include "cupidbuild_artifacts.cc"
#undef malloc
#undef realloc
#undef free

static int invoke(const cupidbuild_artifact_request_t *request,
                  unsigned int fault, size_t capacity, int expected,
                  unsigned int *calls) {
  artifact_size_policy_result_t result;
  unsigned char guard[80];
  unsigned int i;
  int ok;
  if (retained_count != 0u || capacity > 64u) return 1;
  memset(guard, 0x5a, sizeof(guard));
  memset(&result, 0xff, sizeof(result));
  allocation_calls = 0u;
  fail_call = fault;
  ok = cupidbuild_verify_artifact_sizes(request, &result,
                                       capacity ? (char *)guard : NULL, capacity);
  *calls = allocation_calls;
  if (ok != expected || retained_count != 0u) return 2;
  if (expected) {
    if (result.artifact_count != 16u || result.total_exact_bytes == 0u) return 3;
    if (capacity && guard[0] != 0u) return 4;
  } else {
    if (result.artifact_count != 0u || result.total_exact_bytes != 0u) return 5;
    if (allocation_calls != fault) return 6;
    if (capacity && memchr(guard, 0, capacity) == NULL) return 7;
    if (capacity == 64u && strstr((const char *)guard, "allocate") == NULL) return 8;
  }
  for (i = (unsigned int)capacity; i < sizeof(guard); i++)
    if (guard[i] != 0x5a) return 9;
  return 0;
}
int main(int argc, char **argv) {
  const size_t capacities[5] = {0u, 1u, 2u, 9u, 64u};
  cupidbuild_artifact_request_t request;
  unsigned int count, calls, fault, i, tested = 0u;
  int result;
  if (argc != 2) return 90;
  request.repository_root = argv[1];
  request.policy_path = "bootstrap/artifact-size-policy.json";
  request.linux_manifest_path = "bootstrap/seeds/i386-linux/manifest.json";
  result = invoke(&request, 0u, 64u, 1, &count);
  if (result || count < 3u) return 91;
  for (fault = 1u; fault <= count; fault++) {
    for (i = 0u; i < 5u; i++) {
      result = invoke(&request, fault, capacities[i], 0, &calls);
      if (result) {
        fprintf(stderr, "fault=%u capacity=%u check=%d\n", fault, (unsigned int)capacities[i], result);
        return 92;
      }
      result = invoke(&request, 0u, capacities[i], 1, &calls);
      if (result || calls != count) return 93;
      tested++;
    }
  }
  printf("allocations=%u failure_recovery_pairs=%u\n", count, tested);
  return 0;
}
