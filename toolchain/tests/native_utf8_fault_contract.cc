#define CUPID_NATIVE_UTF8_IMPLEMENTATION
#include "native_utf8.h"
#include <string.h>
static size_t allocation_attempt, fail_at, live_allocations;
static void *checked_malloc(size_t size) {
  void *result;
  allocation_attempt++;
  if (allocation_attempt == fail_at) return NULL;
  result = malloc(size); if (result != NULL) live_allocations++; return result;
}
static void *checked_calloc(size_t count, size_t size) {
  void *result;
  allocation_attempt++;
  if (allocation_attempt == fail_at) return NULL;
  result = calloc(count, size); if (result != NULL) live_allocations++; return result;
}
static void checked_free(void *pointer) {
  if (pointer != NULL) { if (live_allocations == 0) abort(); live_allocations--; }
  free(pointer);
}
#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
#include "native_utf8.cc"
#define wmain private_wmain
#include "native_utf8_entry.cc"
#undef wmain
#undef malloc
#undef calloc
#undef free

int cupid_tool_utf8_main(int argc, char **argv) {
  size_t baseline = live_allocations, i;
  STARTUPINFOA startup; PROCESS_INFORMATION process; FILE *stream;
  (void)argc; (void)argv;
  memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
  startup.cb = sizeof(startup);
  for (i=1; i<=3; i++) {
    allocation_attempt=0;fail_at=i;
    if (cupid_native_utf8_process("never.exe", "never.exe", NULL, NULL, FALSE, 0, NULL, ".", &startup, &process) || GetLastError()!=ERROR_NOT_ENOUGH_MEMORY || live_allocations!=baseline) return 10+(int)i;
  }
  for (i=1; i<=3; i++) {
    allocation_attempt=0;fail_at=i;
    if (cupid_native_utf8_fullpath(NULL,".",0)!=NULL || errno!=ENOMEM || live_allocations!=baseline) return 20+(int)i;
  }
  for (i=1; i<=3; i++) {
    allocation_attempt=0;fail_at=i;
    if (cupid_native_utf8_getcwd(NULL,0)!=NULL || errno!=ENOMEM || live_allocations!=baseline) return 80+(int)i;
  }
  allocation_attempt=0;fail_at=0;
  {
    char tiny[1];
    if (cupid_native_utf8_getcwd(tiny,1)!=NULL || errno!=ERANGE || live_allocations!=baseline) return 84;
    if (cupid_native_utf8_getcwd(tiny,0)!=NULL || errno!=EINVAL || live_allocations!=baseline) return 85;
    if (cupid_native_utf8_getcwd(NULL,-1)!=NULL || errno!=EINVAL || live_allocations!=baseline) return 86;
  }
  for (i=1; i<=2; i++) {
    allocation_attempt=0;fail_at=i;stream=NULL;
    if (cupid_native_utf8_fopen_s(&stream,"unused","rb")!=ENOMEM || stream!=NULL || live_allocations!=baseline) return 30+(int)i;
  }
  if (_wputenv_s(L"PRIVATE_FAULT_ENV",L"\x6771\x4eac")) return 40;
  for (i=1; i<=5; i++) {
    allocation_attempt=0;fail_at=i;
    if (cupid_native_utf8_getenv("PRIVATE_FAULT_ENV")!=NULL || live_allocations!=baseline || environment_cache!=NULL) return 40+(int)i;
  }
  allocation_attempt=0;fail_at=0;
  startup.cb=0;
  if (cupid_native_utf8_process(NULL,NULL,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process) || GetLastError()!=ERROR_INVALID_PARAMETER || allocation_attempt!=0) return 50;
  startup.cb=sizeof(startup);
  if (cupid_native_utf8_process(NULL,NULL,NULL,NULL,FALSE,EXTENDED_STARTUPINFO_PRESENT,NULL,NULL,&startup,&process) || GetLastError()!=ERROR_INVALID_PARAMETER || allocation_attempt!=0) return 51;
  if (cupid_native_utf8_getenv("PRIVATE_FAULT_ENV")==NULL) return 52;
  puts("native UTF-8 fault contract: ok"); return 0;
}

int wmain(int argc, wchar_t **argv);
int wmain(int argc, wchar_t **argv) {
  int result, fault;
  wchar_t first[] = L"contract";
  wchar_t second[] = L"argument";
  wchar_t invalid[] = {0xd800, 0};
  wchar_t *values[] = {first, second, NULL};
  result = private_wmain(argc, argv);
  if (result != 0 || live_allocations != 0 || environment_cache != NULL) return 70;
  for (fault = 1; fault <= 3; fault++) {
    allocation_attempt = 0; fail_at = (size_t)fault;
    if (private_wmain(2, values) != 126 || live_allocations != 0) return 70 + fault;
  }
  fail_at = 0; allocation_attempt = 0; values[1] = invalid;
  if (private_wmain(2, values) != 126 || live_allocations != 0) return 74;
  if (private_wmain(-1, values) != 126 || live_allocations != 0) return 75;
  puts("packaged native entry cleanup and argv faults: ok");
  return 0;
}
