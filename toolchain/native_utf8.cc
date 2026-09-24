/* UTF-8 file, environment and process adapters for the native Windows host. */
#define CUPID_NATIVE_UTF8_IMPLEMENTATION
#include "native_utf8.h"
#include "path_encoding.h"
#include <errno.h>
#include <string.h>
#include <wchar.h>

static wchar_t *to_wide(const char *text) {
  size_t units, bytes;
  wchar_t *out;
  if (text == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
  bytes = strlen(text);
  if (!cupidbuild_path_to_utf16(text, bytes, NULL, 0, &units) || units > 32766u) {
    SetLastError(ERROR_NO_UNICODE_TRANSLATION); return NULL;
  }
  out = calloc(units + 1, sizeof(*out));
  if (out == NULL) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
  if (!cupidbuild_path_to_utf16(text, bytes, (unsigned short *)out, units + 1, &units)) {
    free(out); SetLastError(ERROR_NO_UNICODE_TRANSLATION); return NULL;
  }
  return out;
}
char *cupid_native_utf8_from_wide(const wchar_t *text, size_t units) {
  size_t bytes;
  char *out;
  if (!cupidbuild_path_to_utf8((const unsigned short *)text, units, NULL, 0, &bytes)) return NULL;
  out = malloc(bytes + 1);
  if (out == NULL) return NULL;
  if (!cupidbuild_path_to_utf8((const unsigned short *)text, units, out, bytes + 1, &bytes)) { free(out); return NULL; }
  return out;
}
DWORD cupid_native_utf8_attributes(const char *path) {
  wchar_t *wide = to_wide(path); DWORD result, error;
  if (wide == NULL) return INVALID_FILE_ATTRIBUTES;
  result = GetFileAttributesW(wide); error = GetLastError(); free(wide); SetLastError(error); return result;
}
HANDLE cupid_native_utf8_open(const char *path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
  wchar_t *wide = to_wide(path); HANDLE result; DWORD error;
  if (wide == NULL) return INVALID_HANDLE_VALUE;
  result = CreateFileW(wide, access, sharing, security, creation, flags, template_file);
  error = GetLastError(); free(wide); SetLastError(error); return result;
}
BOOL cupid_native_utf8_delete(const char *path) {
  wchar_t *wide = to_wide(path); BOOL result; DWORD error;
  if (wide == NULL) return FALSE;
  result = DeleteFileW(wide); error = GetLastError(); free(wide); SetLastError(error); return result;
}
BOOL cupid_native_utf8_move(const char *source, const char *target, DWORD flags) {
  wchar_t *left = to_wide(source), *right; BOOL result; DWORD error;
  if (left == NULL) return FALSE;
  right = target == NULL ? NULL : to_wide(target);
  if (target != NULL && right == NULL) { error = GetLastError(); free(left); SetLastError(error); return FALSE; }
  result = MoveFileExW(left, right, flags); error = GetLastError(); free(left); free(right); SetLastError(error); return result;
}
BOOL cupid_native_utf8_process(const char *application, char *command, LPSECURITY_ATTRIBUTES ps, LPSECURITY_ATTRIBUTES ts, BOOL inherit, DWORD flags, LPVOID environment, const char *directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION process) {
  wchar_t *app = NULL, *cmd = NULL, *cwd = NULL; STARTUPINFOW wide_startup; BOOL result = FALSE; DWORD error;
  if (startup == NULL || environment != NULL || startup->lpReserved != NULL || startup->lpDesktop != NULL || startup->lpTitle != NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (startup->cb != ((flags & EXTENDED_STARTUPINFO_PRESENT) ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW))) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (application != NULL && (app = to_wide(application)) == NULL) goto done;
  if (command != NULL && (cmd = to_wide(command)) == NULL) goto done;
  if (directory != NULL && (cwd = to_wide(directory)) == NULL) goto done;
  /* STARTUPINFOEX follows the base layout; preserve its attribute list. */
  if (flags & EXTENDED_STARTUPINFO_PRESENT) {
    STARTUPINFOEXW extended;
    memcpy(&extended, startup, sizeof(extended));
    result = CreateProcessW(app, cmd, ps, ts, inherit, flags, NULL, cwd, &extended.StartupInfo, process);
  } else {
    memcpy(&wide_startup, startup, sizeof(wide_startup));
    result = CreateProcessW(app, cmd, ps, ts, inherit, flags, NULL, cwd, &wide_startup, process);
  }
done:
  error = GetLastError(); free(app); free(cmd); free(cwd); SetLastError(error); return result;
}
char *cupid_native_utf8_fullpath(char *destination, const char *path, size_t capacity) {
  wchar_t *wide, *absolute; DWORD units; size_t bytes; char *allocated = NULL;
  wide = to_wide(path == NULL || path[0] == 0 ? "." : path);
  if (wide == NULL) { errno = GetLastError() == ERROR_NOT_ENOUGH_MEMORY ? ENOMEM : EINVAL; return NULL; }
  absolute = calloc(32768u, sizeof(*absolute));
  if (absolute == NULL) { free(wide); errno = ENOMEM; return NULL; }
  units = GetFullPathNameW(wide, 32768u, absolute, NULL); free(wide);
  if (units == 0 || units >= 32768u || !cupidbuild_path_to_utf8((const unsigned short *)absolute, units, NULL, 0, &bytes)) { free(absolute); errno = EINVAL; return NULL; }
  if (destination == NULL) {
    if (capacity != 0 && capacity <= bytes) { free(absolute); errno = ERANGE; return NULL; }
    capacity = bytes + 1; allocated = malloc(capacity); destination = allocated;
  }
  if (destination == NULL) { free(absolute); errno = ENOMEM; return NULL; }
  if (!cupidbuild_path_to_utf8((const unsigned short *)absolute, units, destination, capacity, &bytes)) { free(absolute); free(allocated); errno = ERANGE; return NULL; }
  free(absolute); return destination;
}
char *cupid_native_utf8_getcwd(char *destination, int capacity) {
  if (capacity < 0 || (destination != NULL && capacity == 0)) {
    errno = EINVAL;
    return NULL;
  }
  return cupid_native_utf8_fullpath(destination, ".", (size_t)capacity);
}
errno_t cupid_native_utf8_fopen_s(FILE **stream, const char *path, const char *mode) {
  wchar_t *wide_path, *wide_mode; errno_t result;
  if (stream == NULL) return EINVAL;
  *stream = NULL;
  wide_path = to_wide(path); if (wide_path == NULL) return GetLastError() == ERROR_NOT_ENOUGH_MEMORY ? ENOMEM : EINVAL;
  wide_mode = to_wide(mode); if (wide_mode == NULL) { result = GetLastError() == ERROR_NOT_ENOUGH_MEMORY ? ENOMEM : EINVAL; free(wide_path); return result; }
  result = _wfopen_s(stream, wide_path, wide_mode); free(wide_path); free(wide_mode); return result;
}
FILE *cupid_native_utf8_fopen(const char *path, const char *mode) {
  FILE *stream = NULL; errno_t result = cupid_native_utf8_fopen_s(&stream, path, mode);
  if (result != 0) errno = result;
  return stream;
}
typedef struct env_entry { char *name; char *value; struct env_entry *next; } env_entry;
static env_entry *environment_cache;
char *cupid_native_utf8_getenv(const char *name) {
  wchar_t *wide_name = to_wide(name), *value; size_t units = 0; char *utf8; env_entry *entry;
  if (wide_name == NULL) return NULL;
  if (_wgetenv_s(&units, NULL, 0, wide_name) != 0 || units == 0) { free(wide_name); return NULL; }
  value = calloc(units, sizeof(*value));
  if (value == NULL) { free(wide_name); return NULL; }
  if (_wgetenv_s(&units, value, units, wide_name) != 0 || units == 0) { free(wide_name); free(value); return NULL; }
  free(wide_name); utf8 = cupid_native_utf8_from_wide(value, units - 1); free(value); if (utf8 == NULL) return NULL;
  for (entry = environment_cache; entry != NULL; entry = entry->next) if (strcmp(entry->name, name) == 0) break;
  if (entry == NULL) {
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) { free(utf8); return NULL; }
    entry->name = malloc(strlen(name) + 1);
    if (entry->name == NULL) { free(entry); free(utf8); return NULL; }
    memcpy(entry->name, name, strlen(name) + 1); entry->next = environment_cache; environment_cache = entry;
  }
  if (entry->value != NULL && strcmp(entry->value, utf8) == 0) free(utf8);
  else { free(entry->value); entry->value = utf8; }
  return entry->value;
}

void cupid_native_utf8_release_environment(void) {
  env_entry *entry;
  while ((entry = environment_cache) != NULL) { environment_cache = entry->next; free(entry->name); free(entry->value); free(entry); }
}
