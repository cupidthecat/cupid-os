#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cupid_runtime.h>

typedef DWORD (*thread_function_t)(void *);
typedef struct {
  thread_function_t function;
  void *argument;
  volatile LONG *complete;
} thread_call_t;
HANDLE cupid_windows_observer_thread_open(void *call);
void cupid_windows_observer_thread_finish(volatile LONG *flag);
LONG cupid_windows_observer_thread_finished(volatile LONG *flag);
BOOL cupid_test_get_exit_code_thread(HANDLE thread, DWORD *code);
void cupid_test_exit_process(DWORD code);

static unsigned int probe_enabled, probe_active, probe_entered, probe_release, probe_overlap;
static unsigned int start;

/* Test-only hooks run after entering and before leaving the public heap
 * wrapper. The reference mode reports overlap before touching heap metadata. */
void cupid_test_heap_enter(void) {
  if (!__atomic_load_n(&probe_enabled, __ATOMIC_ACQUIRE)) return;
  if (__atomic_fetch_add(&probe_active, 1u, __ATOMIC_ACQ_REL)) {
    __atomic_store_n(&probe_overlap, 1u, __ATOMIC_RELEASE);
    for (;;) {}
  }
  __atomic_store_n(&probe_entered, 1u, __ATOMIC_RELEASE);
  while (!__atomic_load_n(&probe_release, __ATOMIC_ACQUIRE)) {}
}

void cupid_test_heap_leave(void) {
  if (__atomic_load_n(&probe_enabled, __ATOMIC_ACQUIRE))
    __atomic_store_n(&probe_active, 0u, __ATOMIC_RELEASE);
}

static unsigned char pattern(unsigned int id, unsigned int slot, size_t index) {
  return (unsigned char)(id * 37u + slot * 19u + (unsigned int)index);
}

static int valid(const unsigned char *data, size_t length, unsigned int id, unsigned int slot) {
  size_t index;
  for (index = 0u; index < length; index++)
    if (data[index] != pattern(id, slot, index)) return 0;
  return 1;
}

static void fill(unsigned char *data, size_t length, unsigned int id, unsigned int slot) {
  size_t index;
  for (index = 0u; index < length; index++) data[index] = pattern(id, slot, index);
}

static DWORD worker(void *opaque) {
  unsigned int id = *(unsigned int *)opaque, iteration, slot, other;
  unsigned char *data[16];
  size_t lengths[16];
  (void)memset(data, 0, sizeof(data));
  (void)memset(lengths, 0, sizeof(lengths));
  while (!__atomic_load_n(&start, __ATOMIC_ACQUIRE)) {}
  for (iteration = 0u; iteration < 4000u; iteration++) {
    size_t length = (size_t)((iteration * 97u + id * 23u) % 4096u + 1u);
    unsigned char *next;
    slot = (iteration * 7u + id) % 16u;
    if (data[slot]) {
      if (!valid(data[slot], lengths[slot], id, slot)) return 10u;
      if (iteration % 17u == 0u) {
        next = (unsigned char *)realloc(data[slot], (size_t)-1);
        if (next || !valid(data[slot], lengths[slot], id, slot)) return 11u;
      }
      if (iteration % 3u == 0u) {
        free(data[slot]); data[slot] = (unsigned char *)0; lengths[slot] = 0u;
      } else {
        next = (unsigned char *)realloc(data[slot], length);
        if (!next || ((uintptr_t)next & 15u) ||
            !valid(next, length < lengths[slot] ? length : lengths[slot], id, slot)) return 12u;
        data[slot] = next; lengths[slot] = length;
        fill(next, length, id, slot);
      }
    } else {
      next = (unsigned char *)(iteration % 2u ? malloc(length) : calloc(length, 1u));
      if (!next || ((uintptr_t)next & 15u)) return 13u;
      if (iteration % 2u == 0u) {
        size_t index;
        for (index = 0u; index < length; index++) if (next[index]) return 14u;
      }
      data[slot] = next; lengths[slot] = length;
      fill(next, length, id, slot);
    }
    for (other = 0u; other < 16u; other++)
      if (data[other] && !valid(data[other], lengths[other], id, other)) return 15u;
  }
  for (slot = 0u; slot < 16u; slot++) free(data[slot]);
  if (calloc((size_t)-1, 2u) != (void *)0) return 16u;
  free((void *)0);
  return 0u;
}

int main(int argc, char **argv) {
  unsigned int ids[8], index;
  thread_call_t calls[8];
  HANDLE threads[8];
  volatile LONG completed[8];
  DWORD code;
  int hooks = argc == 3 && strcmp(argv[2], "hooks") == 0;
  if (argc < 2 || argc > 3) return 1;
  if (strcmp(argv[1], "on") == 0) {
    cupid_runtime_heap_enable_threads();
    cupid_runtime_heap_enable_threads();
  } else if (strcmp(argv[1], "off") != 0) return 2;
  if (hooks) __atomic_store_n(&probe_enabled, 1u, __ATOMIC_RELEASE);
  for (index = 0u; index < 8u; index++) {
    ids[index] = index + 1u;
    calls[index].function = worker; calls[index].argument = &ids[index];
    completed[index] = 0;
    calls[index].complete = &completed[index];
    threads[index] = cupid_windows_observer_thread_open(&calls[index]);
    if (!threads[index]) return 3;
  }
  __atomic_store_n(&start, 1u, __ATOMIC_RELEASE);
  if (hooks) {
    while (!__atomic_load_n(&probe_entered, __ATOMIC_ACQUIRE)) {}
    if (WaitForSingleObject(threads[0], 10u) != WAIT_TIMEOUT) return 4;
    if (__atomic_load_n(&probe_overlap, __ATOMIC_ACQUIRE)) {
      cupid_test_exit_process(91u);
      return 91;
    }
    __atomic_store_n(&probe_release, 1u, __ATOMIC_RELEASE);
  }
  for (index = 0u; index < 8u; index++) {
    if (WaitForSingleObject(threads[index], 30000u) != WAIT_OBJECT_0 ||
        !cupid_test_get_exit_code_thread(threads[index], &code) || code ||
        !CloseHandle(threads[index]) || !cupid_windows_observer_thread_finished(&completed[index])) return 5;
  }
  if (hooks) __atomic_store_n(&probe_enabled, 0u, __ATOMIC_RELEASE);
  puts("pass threaded_heap workers=8 iterations=32000");
  return 0;
}
