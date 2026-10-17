/* Checked adapter failures must preserve API errors and release each buffer. */
#include <stdlib.h>
#include <string.h>
static unsigned int last_error, fail_at, allocations, live, calls, query_mode, invalid;
void *runtime_calloc(size_t, size_t);
void *calloc(size_t count, size_t size) {
  void *result;
  allocations++;
  if (allocations == fail_at) return (void *)0;
  result = runtime_calloc(count, size);
  if (result != (void *)0) live++;
  return result;
}
static void changing_free(void *pointer) {
  if (pointer != (void *)0) { if (!live) invalid = 1u; else live--; }
  free(pointer); last_error = 77u;
}
#define free changing_free
#define cupid_windows_create_file tested_create_file
#define cupid_windows_get_last_error tested_last_error
#define cupid_windows_set_last_error tested_set_error
#define cupid_windows_get_current_directory tested_current_directory
#define CUPID_WINDOWS_LONG_PATHS 1
#define CUPID_WINDOWS_BUILD 1
#include "../hosted/i386-windows/windows_utf8.cc"
#undef free
unsigned int tested_last_error(void) { return last_error; }
void tested_set_error(unsigned int value) { last_error = value; }
unsigned int cupid_windows_get_full_path_name_wide(const unsigned short *path,
    unsigned int capacity, unsigned short *output, unsigned short **part) {
  unsigned int index;
  (void)path; (void)part;
  if (capacity != 32768u) { invalid = 1u; return 0u; }
  last_error = 5u;
  if (query_mode == 1u) return 0u;
  if (query_mode == 2u) return 32768u;
  if (query_mode == 4u) {
    static const unsigned short device[] = {'\\', '\\', '.', '\\', 'N', 'U', 'L', 0u};
    memcpy(output, device, sizeof(device)); return 7u;
  }
  output[0] = 'C'; output[1] = ':'; output[2] = '\\';
  for (index = 3u; index < 300u; index++) output[index] = 'x';
  output[300] = 0u;
  if (query_mode == 3u) output[299] = 0xd800u;
  return 300u;
}
unsigned int cupid_windows_get_current_directory_wide(unsigned int capacity, unsigned short *output) {
  (void)capacity; (void)output; return 0u;
}
static void require_extended(const unsigned short *path) {
  unsigned int index;
  if (path[0] != '\\' || path[1] != '\\' || path[2] != '?' || path[3] != '\\' ||
      path[4] != 'C' || path[5] != ':' || path[6] != '\\') invalid = 1u;
  for (index = 7u; index < 304u; index++) if (path[index] != 'x') invalid = 1u;
  if (path[304]) invalid = 1u;
}
unsigned int cupid_windows_create_file_wide(const unsigned short *path,
    unsigned int access, unsigned int sharing, void *security,
    unsigned int creation, unsigned int attributes, unsigned int template_file) {
  (void)access; (void)sharing; (void)security; (void)creation;
  (void)attributes; (void)template_file;
  if (query_mode == 4u) {
    static const unsigned short device[] = {'\\', '\\', '.', '\\', 'N', 'U', 'L', 0u};
    if (memcmp(path, device, sizeof(device))) invalid = 1u;
  } else require_extended(path);
  calls++; last_error = 5u; return 123u;
}
unsigned int cupid_windows_delete_file_wide(const unsigned short *path) {
  require_extended(path); calls++; last_error = 5u; return 1u;
}
unsigned int cupid_windows_get_file_attributes_wide(const unsigned short *path) {
  require_extended(path); calls++; last_error = 5u; return 32u;
}
unsigned int cupid_windows_create_directory_wide(const unsigned short *path, void *security) {
  (void)security; require_extended(path); calls++; last_error = 5u; return 1u;
}
unsigned int cupid_windows_remove_directory_wide(const unsigned short *path) {
  require_extended(path); calls++; last_error = 5u; return 1u;
}
unsigned int cupid_windows_move_file_ex_wide(const unsigned short *from, const unsigned short *to, unsigned int flags) {
  (void)flags; require_extended(from);
  if (to != (unsigned short *)0) require_extended(to);
  calls++; last_error = 5u; return 1u;
}
unsigned int cupid_windows_create_process_wide(const unsigned short *app, unsigned short *command,
    LPSECURITY_ATTRIBUTES a, LPSECURITY_ATTRIBUTES b, unsigned int inherit, unsigned int flags,
    void *environment, const unsigned short *directory, STARTUPINFOA *startup, PROCESS_INFORMATION *process) {
  size_t index;
  static const char expected[] = "literal command";
  (void)a; (void)b; (void)inherit; (void)flags; (void)environment; (void)startup; (void)process;
  require_extended(app); require_extended(directory);
  for (index = 0u; index < sizeof(expected); index++)
    if (command[index] != (unsigned short)expected[index]) invalid = 1u;
  calls++; last_error = 5u; return 1u;
}
static unsigned int invoke_path(unsigned int kind) {
  if (kind == 0u) return cupid_windows_delete_file("relative");
  if (kind == 1u) return cupid_windows_get_file_attributes("relative") == 32u;
  if (kind == 2u) return cupid_windows_create_directory("relative", (void *)0);
  return cupid_windows_remove_directory("relative");
}
int main(int argc, char **argv) {
  unsigned int index;
  (void)argc; (void)argv;
  for (index = 1u; index <= 3u; index++) {
    allocations = 0u; calls = 0u; fail_at = index;
    if (tested_create_file("relative", 0u, 0u, (void *)0, 0u, 0u, 0u) != 0xffffffffu ||
        last_error != 8u || calls || live) return 1;
    allocations = 0u; fail_at = 0u;
    if (tested_create_file("relative", 0u, 0u, (void *)0, 0u, 0u, 0u) != 123u ||
        last_error != 5u || calls != 1u || live) return 2;
  }
  for (index = 1u; index <= 3u; index++) {
    unsigned int error = index == 1u ? 5u : 206u;
    calls = 0u; query_mode = index;
    if (tested_create_file("relative", 0u, 0u, (void *)0, 0u, 0u, 0u) != 0xffffffffu ||
        last_error != error || calls || live) return 3;
  }
  query_mode = 0u; calls = 0u;
  if (tested_create_file("\xed\xa0\x80", 0u, 0u, (void *)0, 0u, 0u, 0u) != 0xffffffffu ||
      last_error != 1113u || calls || live) return 4;
  if (tested_create_file("relative", 0u, 0u, (void *)0, 0u, 0u, 0u) != 123u ||
      calls != 1u || last_error != 5u || live) return 5;
  {
    unsigned int kind;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    for (kind = 0u; kind < 4u; kind++) for (index = 1u; index <= 3u; index++) {
      fail_at = index; allocations = 0u; calls = 0u;
      if (invoke_path(kind) || calls || live || last_error != 8u) return 7;
      fail_at = 0u; allocations = 0u;
      if (!invoke_path(kind) || calls != 1u || live || last_error != 5u) return 8;
    }
    for (index = 1u; index <= 6u; index++) {
      fail_at = index; allocations = 0u; calls = 0u;
      if (cupid_windows_move_file_ex("from", "to", 0u) || calls || live || last_error != 8u) return 9;
      fail_at = 0u; allocations = 0u;
      if (!cupid_windows_move_file_ex("from", "to", 0u) || calls != 1u || live || last_error != 5u) return 10;
    }
    for (index = 1u; index <= 7u; index++) {
      fail_at = index; allocations = 0u; calls = 0u;
      if (cupid_windows_create_process("tool", "literal command", (void *)0, (void *)0, 0u,
          0u, (void *)0, "directory", &startup, &process) || calls || live || last_error != 8u) return 11;
      fail_at = 0u; allocations = 0u;
      if (!cupid_windows_create_process("tool", "literal command", (void *)0, (void *)0, 0u,
          0u, (void *)0, "directory", &startup, &process) || calls != 1u || live || last_error != 5u) return 12;
    }
  }
  {
    char long_name[300];
    memset(long_name, 'x', sizeof(long_name)); long_name[sizeof(long_name) - 1u] = 0;
    fail_at = 0u; allocations = 0u; calls = 0u; query_mode = 4u;
    if (tested_create_file(long_name, 0u, 0u, (void *)0, 0u, 0u, 0u) != 123u ||
        calls != 1u || live || last_error != 5u) return 13;
  }
  return invalid ? 6 : 0;
}
