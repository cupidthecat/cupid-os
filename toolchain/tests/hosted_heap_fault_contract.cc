#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int allocation_calls;
static unsigned int release_calls;
static int fail_allocation;
static int fail_release;

#if defined(CUPID_HEAP_WINDOWS)
void *cupid_windows_virtual_alloc(void *address, unsigned int bytes,
                                  unsigned int kind, unsigned int protection);
unsigned int cupid_windows_virtual_free(void *address, unsigned int bytes,
                                        unsigned int kind);

void *cupid_heap_contract_virtual_alloc(void *address, unsigned int bytes,
                                       unsigned int kind, unsigned int protection) {
  allocation_calls++;
  if (fail_allocation != 0) {
    fail_allocation = 0;
    return NULL;
  }
  return cupid_windows_virtual_alloc(address, bytes, kind, protection);
}

unsigned int cupid_heap_contract_virtual_free(void *address, unsigned int bytes,
                                             unsigned int kind) {
  release_calls++;
  if (fail_release != 0) {
    fail_release = 0;
    return 0u;
  }
  return cupid_windows_virtual_free(address, bytes, kind);
}
#else
int cupid_linux_syscall1(int number, unsigned int first);

int cupid_heap_contract_syscall1(int number, unsigned int first) {
  unsigned int current;
  if (number == 45 && first != 0u) {
    current = (unsigned int)cupid_linux_syscall1(45, 0u);
    if (first > current) {
      allocation_calls++;
      if (fail_allocation != 0) {
        fail_allocation = 0;
        return (int)current;
      }
    } else if (first < current) {
      release_calls++;
      if (fail_release != 0) {
        fail_release = 0;
        return (int)current;
      }
    }
  }
  return cupid_linux_syscall1(number, first);
}
#endif

static int allocation_failure(void) {
  unsigned char *allocation = (unsigned char *)malloc(64u);
  unsigned char *replacement;
  unsigned int before, index;
  if (allocation == NULL) return 1;
  memset(allocation, 0x57, 64u);
  before = allocation_calls;
  fail_allocation = 1;
  errno = 0;
  replacement = (unsigned char *)realloc(allocation, 4194304u);
  if (replacement != NULL || errno != ENOMEM || fail_allocation != 0 ||
      allocation_calls != before + 1u) return 2;
  for (index = 0u; index < 64u; index++) {
    if (allocation[index] != 0x57u) return 3;
  }
  replacement = (unsigned char *)realloc(allocation, 4194304u);
  if (replacement == NULL || ((uintptr_t)replacement & 15u) != 0u) return 4;
  for (index = 0u; index < 64u; index++) {
    if (replacement[index] != 0x57u) return 5;
  }
  free(replacement);
  puts("{\"allocation_failure\":1,\"preserved_data\":1,\"recovery\":1}");
  return 0;
}

static int release_failure(void) {
  unsigned char *allocation = (unsigned char *)malloc(131073u);
  unsigned char *replacement;
  uintptr_t address;
  unsigned int before, released;
  if (allocation == NULL) return 10;
  address = (uintptr_t)allocation;
  before = allocation_calls;
  released = release_calls;
  fail_release = 1;
  free(allocation);
  if (fail_release != 0 || release_calls != released + 1u) return 11;
  replacement = (unsigned char *)calloc(131073u, 1u);
  if (replacement == NULL || (uintptr_t)replacement != address ||
      allocation_calls != before) return 12;
  for (before = 0u; before < 131073u; before++) {
    if (replacement[before] != 0u) return 13;
  }
  free(replacement);
  if (release_calls != released + 2u) return 14;
  puts("{\"release_failure\":1,\"reused_without_growth\":1,\"recovery\":1}");
  return 0;
}

static int overflow_without_growth(void) {
  unsigned int before = allocation_calls;
  errno = 0;
  if (malloc(SIZE_MAX) != NULL || errno != ENOMEM) return 20;
  errno = 0;
  if (calloc(SIZE_MAX, 2u) != NULL || errno != ENOMEM) return 21;
  if (allocation_calls != before) return 22;
  puts("{\"overflow\":1,\"no_os_allocation\":1}");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) return 30;
  if (strcmp(argv[1], "allocation-failure") == 0) return allocation_failure();
  if (strcmp(argv[1], "release-failure") == 0) return release_failure();
  if (strcmp(argv[1], "overflow") == 0) return overflow_without_growth();
  return 31;
}
