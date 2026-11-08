#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef DWORD (*thread_function_t)(void *argument);
typedef struct {
  thread_function_t function;
  void *argument;
  volatile LONG *complete;
} thread_call_t;

HANDLE cupid_windows_observer_thread_open(void *call);
void cupid_windows_observer_thread_finish(volatile LONG *complete);
LONG cupid_windows_observer_thread_finished(volatile LONG *complete);
BOOL cupid_test_get_exit_code_thread(HANDLE thread, DWORD *code);

_Static_assert(sizeof(thread_call_t) == 12u, "callback descriptor is three i386 words");
_Static_assert(WAIT_FAILED == (DWORD)-1, "Windows wait failure is a DWORD of ones");

typedef struct {
  DWORD left;
  DWORD seed;
  DWORD data[64];
  DWORD checksum;
  volatile LONG complete;
  volatile LONG release;
  DWORD right;
} worker_data_t;

static DWORD worker(void *opaque) {
  worker_data_t *data = (worker_data_t *)opaque;
  DWORD index, checksum = 0u;
  if (!data || data->left != 0x1234abcdu || data->right != 0x9876fedcu) return 77u;
  for (index = 0u; index < 64u; index++) {
    data->data[index] = data->seed ^ (index * 2654435761u);
    checksum ^= data->data[index];
  }
  data->checksum = checksum;
  cupid_windows_observer_thread_finish(&data->complete);
  while (!cupid_windows_observer_thread_finished(&data->release)) {}
  return data->seed ^ 0xa5a50000u;
}

static DWORD null_argument(void *argument) {
  return argument ? 99u : 0x87654321u;
}

int main(int argc, char **argv) {
  worker_data_t data[8];
  thread_call_t calls[8], invalid;
  HANDLE threads[8];
  volatile LONG completed[8];
  DWORD round, index, element, code;
  if (WaitForSingleObject((HANDLE)0, 0u) != WAIT_FAILED) return 10;
  invalid.function = (thread_function_t)0;
  invalid.argument = (void *)0;
  invalid.complete = &completed[0];
  if (argc != 1 && !(argc == 2 && strcmp(argv[1], "completion-only") == 0)) return 90;
  if (argc == 1) {
    if (cupid_windows_observer_thread_open((void *)0) != (HANDLE)0 ||
        cupid_windows_observer_thread_open(&invalid) != (HANDLE)0) return 1;
    invalid.function = null_argument;
    invalid.complete = (volatile LONG *)0;
    if (cupid_windows_observer_thread_open(&invalid) != (HANDLE)0) return 1;
  }
  for (round = 0u; round < 64u; round++) {
    (void)memset(data, 0, sizeof(data));
    for (index = 0u; index < 8u; index++) {
      data[index].left = 0x1234abcdu;
      data[index].right = 0x9876fedcu;
      data[index].seed = round * 17u + index + 1u;
      calls[index].function = worker;
      calls[index].argument = &data[index];
      completed[index] = 0;
      calls[index].complete = &completed[index];
      threads[index] = cupid_windows_observer_thread_open(&calls[index]);
      if (threads[index] == (HANDLE)0) return 2;
    }
    for (index = 0u; index < 8u; index++) {
      DWORD checksum = 0u;
      while (!cupid_windows_observer_thread_finished(&data[index].complete)) {
        if (WaitForSingleObject(threads[index], 1u) != WAIT_TIMEOUT) return 3;
      }
      for (element = 0u; element < 64u; element++) {
        DWORD expected = data[index].seed ^ (element * 2654435761u);
        if (data[index].data[element] != expected) return 4;
        checksum ^= expected;
      }
      if (data[index].checksum != checksum || data[index].left != 0x1234abcdu ||
          data[index].right != 0x9876fedcu) return 5;
      if (cupid_windows_observer_thread_finished(&completed[index])) return 9;
      cupid_windows_observer_thread_finish(&data[index].release);
      if (WaitForSingleObject(threads[index], 10000u) != WAIT_OBJECT_0 ||
          !cupid_test_get_exit_code_thread(threads[index], &code) ||
          code != (data[index].seed ^ 0xa5a50000u) || !CloseHandle(threads[index])) return 6;
      if (!cupid_windows_observer_thread_finished(&completed[index])) return 8;
    }
  }
  calls[0].function = null_argument;
  calls[0].argument = (void *)0;
  completed[0] = 0;
  calls[0].complete = &completed[0];
  threads[0] = cupid_windows_observer_thread_open(&calls[0]);
  if (threads[0] == (HANDLE)0 || WaitForSingleObject(threads[0], 10000u) != WAIT_OBJECT_0 ||
      !cupid_test_get_exit_code_thread(threads[0], &code) || code != 0x87654321u ||
      !CloseHandle(threads[0]) || !cupid_windows_observer_thread_finished(&completed[0])) return 7;
  puts("pass thread_adapter callbacks=513 rejected=3 completion=after_callback");
  return 0;
}
