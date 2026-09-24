#include <stdlib.h>
static unsigned int last_error;
static unsigned int fail_allocation;
static unsigned int allocation_index;
static unsigned int query_mode;
static unsigned int query_calls;
static unsigned int live_allocations;
void *runtime_calloc(size_t count, size_t size);
void *calloc(size_t count, size_t size) {
  void *result;
  allocation_index++;
  if (allocation_index == fail_allocation) return (void *)0;
  result = runtime_calloc(count, size);
  if (result != (void *)0) live_allocations++;
  return result;
}
static unsigned int calls;
static unsigned int released;
static void error_changing_free(void *pointer) {
  if (pointer != (void *)0) { released++; live_allocations--; }
  free(pointer);
  last_error = 77u;
}
#define free error_changing_free
#define cupid_windows_create_file tested_create_file
#define cupid_windows_delete_file tested_delete_file
#define cupid_windows_get_file_attributes tested_get_attributes
#define cupid_windows_get_last_error tested_get_error
#define cupid_windows_set_last_error tested_set_error
#define cupid_windows_get_full_path_name tested_full_path
#define cupid_windows_get_current_directory tested_current_directory
#define cupid_windows_create_directory tested_create_directory
#define cupid_windows_remove_directory tested_remove_directory
#define cupid_windows_move_file_ex tested_move_file
#define CUPID_WINDOWS_BUILD 1
#include "../hosted/i386-windows/windows_utf8.cc"
#undef free

unsigned int tested_get_error(void) { return last_error; }
void tested_set_error(unsigned int value) { last_error = value; }
static unsigned int record_path(const unsigned short *path, unsigned int result) {
  if (path[0] != 'c' || path[1] != 'a' || path[2] != 'f' ||
      path[3] != 0xe9u || path[4] != 0u) return 0u;
  calls++;
  last_error = 5u;
  return result;
}
unsigned int cupid_windows_get_file_attributes_wide(const unsigned short *path) {
  return record_path(path, 0xffffffffu);
}
unsigned int cupid_windows_delete_file_wide(const unsigned short *path) {
  return record_path(path, 0u);
}
unsigned int cupid_windows_create_file_wide(const unsigned short *path,
    unsigned int access, unsigned int sharing, void *security,
    unsigned int creation, unsigned int attributes, unsigned int template_file) {
  (void)access; (void)sharing; (void)security; (void)creation;
  (void)attributes; (void)template_file;
  return record_path(path, 0xffffffffu);
}
static unsigned int process_calls;
unsigned int cupid_windows_create_process_wide(
    const unsigned short *a, unsigned short *b, LPSECURITY_ATTRIBUTES c,
    LPSECURITY_ATTRIBUTES d, unsigned int e, unsigned int f, void *g,
    const unsigned short *h, STARTUPINFOA *i, PROCESS_INFORMATION *j) {
  (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
  (void)g; (void)h; (void)i; (void)j;
  process_calls++; last_error = 5u;
  return 1u;
}

static unsigned int query(unsigned short *output, unsigned short **part) {
  unsigned short text[] = {'C', ':', 92, 'c', 'a', 'f', 0xe9, 92, 'f', 'i', 'l', 'e', 0};
  query_calls++;
  if (query_mode == 1u) { last_error = 5u; return 0u; }
  if (query_mode == 2u) return 32768u;
  if (query_mode == 3u) { output[0] = 0xdc00u; output[1] = 0u; return 1u; }
  if (query_mode == 4u) { output[0] = 'C'; output[1] = 0u; output[2] = 'x'; return 3u; }
  if (query_mode == 5u) { output[0] = 0xd800u; output[1] = 0xdc00u; return 1u; }
  (void)memcpy(output, text, sizeof(text));
  if (part != (unsigned short **)0) *part = output + 8u;
  return 12u;
}
unsigned int cupid_windows_get_full_path_name_wide(const unsigned short *path,
    unsigned int capacity, unsigned short *output, unsigned short **part) {
  (void)path; (void)capacity;
  return query(output, part);
}
unsigned int cupid_windows_get_current_directory_wide(unsigned int capacity, unsigned short *output) {
  (void)capacity;
  return query(output, (unsigned short **)0);
}

static unsigned int operation_calls;
static unsigned int operation_result = 1u;
static unsigned int null_destination;
unsigned int cupid_windows_create_directory_wide(const unsigned short *path, void *security) {
  (void)path; (void)security;
  operation_calls++; last_error = 5u; return operation_result;
}
unsigned int cupid_windows_remove_directory_wide(const unsigned short *path) {
  (void)path;
  operation_calls++; last_error = 5u; return operation_result;
}
unsigned int cupid_windows_move_file_ex_wide(const unsigned short *from, const unsigned short *to, unsigned int flags) {
  (void)from; (void)flags;
  null_destination = to == (const unsigned short *)0;
  operation_calls++; last_error = 5u; return operation_result;
}
static void reset(unsigned int fail) {
  fail_allocation = fail; allocation_index = 0u; operation_calls = 0u;
}
static int directory_cases(int argc, char **argv) {
  unsigned int index;
  (void)argc; (void)argv;
  reset(1u);
  if (tested_create_directory("valid", (void *)0) || last_error != 8u || operation_calls || live_allocations) return 1;
  reset(0u);
  if (!tested_create_directory("valid", (void *)0) || operation_calls != 1u || live_allocations) return 2;
  reset(1u);
  if (tested_remove_directory("valid") || last_error != 8u || operation_calls || live_allocations) return 3;
  reset(0u);
  if (!tested_remove_directory("valid") || operation_calls != 1u || live_allocations) return 4;
  for (index = 1u; index <= 2u; index++) {
    reset(index);
    if (tested_move_file("from", "to", 0u) || last_error != 8u || operation_calls || live_allocations) return 5;
    reset(0u);
    if (!tested_move_file("from", "to", 0u) || operation_calls != 1u || live_allocations) return 6;
  }
  operation_result = 0u; reset(0u);
  if (tested_create_directory("valid", (void *)0) || last_error != 5u || live_allocations) return 7;
  if (tested_remove_directory("valid") || last_error != 5u || live_allocations) return 8;
  if (tested_move_file("from", "to", 0u) || last_error != 5u || live_allocations) return 9;
  operation_result = 1u; reset(0u);
  if (tested_create_directory("\xed\xa0\x80", (void *)0) || last_error != 1113u || operation_calls || live_allocations) return 10;
  if (tested_remove_directory("\xc0\x80") || last_error != 1113u || operation_calls || live_allocations) return 11;
  if (tested_move_file("from", "\xf4\x90\x80\x80", 0u) || last_error != 1113u || operation_calls || live_allocations) return 12;
  if (tested_move_file("\xed\xa0\x80", "to", 0u) || last_error != 1113u || operation_calls || live_allocations) return 13;
  if (!tested_move_file("from", (const char *)0, 0u) || operation_calls != 1u || !null_destination || live_allocations) return 14;
  return 0;
}
static int fullpath_cases(int argc, char **argv) {
  unsigned int index;
  unsigned int before;
  char buffer[64];
  char *part;
  (void)argc; (void)argv;
  for (index = 1u; index <= 2u; index++) {
    fail_allocation = index; allocation_index = 0u; before = query_calls;
    if (tested_full_path("file", sizeof(buffer), buffer, &part) != 0u ||
        last_error != 8u || live_allocations != 0u || query_calls != before ||
        part != (char *)0 || buffer[0] != 0) return 1;
    fail_allocation = 0u; allocation_index = 0u;
    if (tested_full_path("file", sizeof(buffer), buffer, &part) != 13u ||
        part != buffer + 9u || live_allocations != 0u) return 2;
  }
  fail_allocation = 1u; allocation_index = 0u; before = query_calls;
  if (tested_current_directory(sizeof(buffer), buffer) != 0u || last_error != 8u ||
      live_allocations != 0u || query_calls != before) return 3;
  /* Output conversion no longer allocates; only wide buffers can fail. */
  fail_allocation = 3u; allocation_index = 0u;
  if (tested_full_path("file", sizeof(buffer), buffer, &part) != 13u ||
      allocation_index != 2u || live_allocations != 0u || part != buffer + 9u) return 4;
  fail_allocation = 2u; allocation_index = 0u;
  if (tested_current_directory(sizeof(buffer), buffer) != 13u ||
      allocation_index != 1u || live_allocations != 0u) return 5;
  fail_allocation = 0u;
  for (index = 1u; index <= 5u; index++) {
    unsigned int expected = index == 1u ? 5u : (index == 2u ? 122u : 1113u);
    query_mode = index;
    if (tested_full_path("file", sizeof(buffer), buffer, &part) != 0u ||
        last_error != expected || live_allocations != 0u || part != (char *)0) return 6;
    if (tested_current_directory(sizeof(buffer), buffer) != 0u ||
        last_error != expected || live_allocations != 0u) return 7;
  }
  query_mode = 0u;
  if (tested_full_path("file", sizeof(buffer), buffer, &part) != 13u || live_allocations != 0u) return 8;
  if (tested_current_directory(sizeof(buffer), buffer) != 13u || live_allocations != 0u) return 9;
  return 0;
}

int main(int argc, char **argv) {
  STARTUPINFOEXA startup; PROCESS_INFORMATION process; unsigned int index; int result;
  result = directory_cases(argc, argv); if (result) return result;
  reset(0u); result = fullpath_cases(argc, argv); if (result) return 20 + result;
  memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
  startup.StartupInfo.cb = sizeof(startup);
  for (index = 1u; index <= 3u; index++) {
    reset(index); process_calls = 0u;
    if (cupid_windows_create_process("tool", "tool command", (void *)0, (void *)0, 0u, EXTENDED_STARTUPINFO_PRESENT, (void *)0, "directory", &startup.StartupInfo, &process) || last_error != 8u || process_calls || live_allocations) return 40 + (int)index;
    reset(0u);
    if (!cupid_windows_create_process("tool", "tool command", (void *)0, (void *)0, 0u, EXTENDED_STARTUPINFO_PRESENT, (void *)0, "directory", &startup.StartupInfo, &process) || last_error != 5u || process_calls != 1u || live_allocations) return 44;
  }
  reset(0u); process_calls = 0u;
  if (cupid_windows_create_process("tool", "tool command", (void *)0, (void *)0, 0u, EXTENDED_STARTUPINFO_PRESENT, (void *)0, "\xc0\x80", &startup.StartupInfo, &process) || last_error != 1113u || process_calls || live_allocations) return 45;
  startup.StartupInfo.cb = 0u;
  if (cupid_windows_create_process("tool", "tool command", (void *)0, (void *)0, 0u, EXTENDED_STARTUPINFO_PRESENT, (void *)0, "directory", &startup.StartupInfo, &process) || last_error != 87u || process_calls || live_allocations) return 46;
  return 0;
}
