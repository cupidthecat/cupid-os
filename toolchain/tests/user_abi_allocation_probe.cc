#include <stddef.h>
#include <stdlib.h>

static unsigned int denied_call;
static unsigned int allocation_calls;
static int live_allocations;

void abi_probe_begin(unsigned int fail_at);
unsigned int abi_probe_calls(void);
int abi_probe_live(void);
void *abi_probe_malloc(size_t size);
void *abi_probe_calloc(size_t count, size_t size);
void abi_probe_free(void *pointer);

void abi_probe_begin(unsigned int fail_at) {
  denied_call = fail_at;
  allocation_calls = 0u;
}

unsigned int abi_probe_calls(void) { return allocation_calls; }
int abi_probe_live(void) { return live_allocations; }

void *abi_probe_malloc(size_t size) {
  void *pointer;
  allocation_calls++;
  if (allocation_calls == denied_call) return NULL;
  pointer = malloc(size);
  if (pointer != NULL) live_allocations++;
  return pointer;
}

void *abi_probe_calloc(size_t count, size_t size) {
  void *pointer;
  allocation_calls++;
  if (allocation_calls == denied_call) return NULL;
  pointer = calloc(count, size);
  if (pointer != NULL) live_allocations++;
  return pointer;
}

void abi_probe_free(void *pointer) {
  if (pointer != NULL) live_allocations--;
  free(pointer);
}
