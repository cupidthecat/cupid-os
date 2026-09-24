/* Checked-host UTF-8 boundary. Role selection changes
 * imports, not encoding semantics. All string lengths are UTF-8 bytes. */
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../path_encoding.h"
#if defined(CUPID_WINDOWS_BUILD) && defined(CUPID_WINDOWS_PUBLICATION)
#error Choose one checked Windows adapter role
#endif
void cupid_windows_set_last_error(unsigned int);
unsigned int cupid_windows_create_file_wide(const unsigned short *, unsigned int, unsigned int, void *, unsigned int, unsigned int, unsigned int);
unsigned int cupid_windows_get_current_directory_wide(unsigned int, unsigned short *);
#if defined(CUPID_WINDOWS_BUILD) || defined(CUPID_WINDOWS_PUBLICATION)
unsigned int cupid_windows_delete_file_wide(const unsigned short *);
unsigned int cupid_windows_move_file_ex_wide(const unsigned short *, const unsigned short *, unsigned int);
unsigned int cupid_windows_get_full_path_name_wide(const unsigned short *, unsigned int, unsigned short *, unsigned short **);
#endif
#if defined(CUPID_WINDOWS_BUILD)
unsigned int cupid_windows_get_file_attributes_wide(const unsigned short *);
unsigned int cupid_windows_create_directory_wide(const unsigned short *, void *);
unsigned int cupid_windows_remove_directory_wide(const unsigned short *);
unsigned int cupid_windows_create_process_wide(const unsigned short *, unsigned short *, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, unsigned int, unsigned int, void *, const unsigned short *, STARTUPINFOA *, PROCESS_INFORMATION *);
#endif
static unsigned short *path_utf16(const char *text) {
  size_t bytes;
  size_t units;
  unsigned short *out;
  cupid_windows_set_last_error(1113u);
  if (text == (const char *)0) return (unsigned short *)0;
  bytes = strlen(text);
  if (!cupidbuild_path_to_utf16(text, bytes, (unsigned short *)0, 0u, &units) ||
      units > 32766u) return (unsigned short *)0;
  out = (unsigned short *)calloc(units + 1u, sizeof(unsigned short));
  if (out == (unsigned short *)0) { cupid_windows_set_last_error(8u); return out; }
  if (!cupidbuild_path_to_utf16(text, bytes, out, units + 1u, &units)) {
    free(out);
    return (unsigned short *)0;
  }
  return out;
}

unsigned int cupid_windows_create_file(const char *path,
    unsigned int access, unsigned int sharing, void *security,
    unsigned int creation, unsigned int attributes, unsigned int template_file) {
  unsigned short *wide = path_utf16(path);
  unsigned int result;
  unsigned int error;
  if (wide == (unsigned short *)0) return 0xffffffffu;
  result = cupid_windows_create_file_wide(wide, access, sharing, security,
                                         creation, attributes, template_file);
  error = cupid_windows_get_last_error();
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

static unsigned int publish_utf8_path(const unsigned short *wide, size_t units,
    unsigned int capacity, char *output, char **part, int has_part) {
  size_t length;
  size_t written;
  size_t index;
  if (!cupidbuild_path_to_utf8(wide, units, (char *)0, 0u, &length)) {
    cupid_windows_set_last_error(1113u);
    return 0u;
  }
  if (capacity <= length) {
    cupid_windows_set_last_error(122u);
    return (unsigned int)length + 1u;
  }
  if (output == (char *)0) {
    cupid_windows_set_last_error(87u);
    return 0u;
  }
  if (!cupidbuild_path_to_utf8(wide, units, output, capacity, &written)) {
    cupid_windows_set_last_error(1113u);
    return 0u;
  }
  if (part != (char **)0 && has_part != 0) {
    *part = output;
    for (index = 0u; index < written; index++) {
      if (output[index] == '/' || output[index] == '\\') *part = output + index + 1u;
    }
  }
  cupid_windows_set_last_error(0u);
  return (unsigned int)written;
}

unsigned int cupid_windows_get_current_directory(unsigned int capacity, char *output) {
  unsigned short *wide;
  unsigned int length;
  unsigned int result = 0u;
  unsigned int error;
  if (output != (char *)0 && capacity != 0u) output[0] = 0;
  wide = (unsigned short *)calloc(32768u, sizeof(unsigned short));
  if (wide == (unsigned short *)0) {
    cupid_windows_set_last_error(8u);
    return 0u;
  }
  length = cupid_windows_get_current_directory_wide(32768u, wide);
  error = cupid_windows_get_last_error();
  if (length != 0u && length < 32768u) {
    result = publish_utf8_path(wide, length, capacity, output, (char **)0, 0);
    error = cupid_windows_get_last_error();
  } else if (length >= 32768u) error = 122u;
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}


#if defined(CUPID_WINDOWS_BUILD) || defined(CUPID_WINDOWS_PUBLICATION)
unsigned int cupid_windows_delete_file(const char *path) {
  unsigned short *wide = path_utf16(path);
  unsigned int result;
  unsigned int error;
  if (wide == (unsigned short *)0) return 0u;
  result = cupid_windows_delete_file_wide(wide);
  error = cupid_windows_get_last_error();
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

unsigned int cupid_windows_get_full_path_name(const char *path,
    unsigned int capacity, char *output, char **part) {
  unsigned short *input;
  unsigned short *wide;
  unsigned short *wide_part = (unsigned short *)0;
  unsigned int length;
  unsigned int result = 0u;
  unsigned int error;
  if (part != (char **)0) *part = (char *)0;
  if (output != (char *)0 && capacity != 0u) output[0] = 0;
  input = path_utf16(path);
  if (input == (unsigned short *)0) return 0u;
  wide = (unsigned short *)calloc(32768u, sizeof(unsigned short));
  if (wide == (unsigned short *)0) {
    free(input);
    cupid_windows_set_last_error(8u);
    return 0u;
  }
  length = cupid_windows_get_full_path_name_wide(input, 32768u, wide, &wide_part);
  error = cupid_windows_get_last_error();
  if (length != 0u && length < 32768u) {
    result = publish_utf8_path(wide, length, capacity, output, part,
                              wide_part != (unsigned short *)0);
    error = cupid_windows_get_last_error();
  } else if (length >= 32768u) error = 122u;
  free(input);
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

unsigned int cupid_windows_move_file_ex(const char *from, const char *to, unsigned int flags) {
  unsigned short *wide_from = path_utf16(from);
  unsigned short *wide_to = (unsigned short *)0;
  unsigned int result;
  unsigned int error;
  if (wide_from == (unsigned short *)0) return 0u;
  if (to != (const char *)0) {
    wide_to = path_utf16(to);
    if (wide_to == (unsigned short *)0) {
      error = cupid_windows_get_last_error();
      free(wide_from);
      cupid_windows_set_last_error(error);
      return 0u;
    }
  }
  result = cupid_windows_move_file_ex_wide(wide_from, wide_to, flags);
  error = cupid_windows_get_last_error();
  free(wide_from);
  free(wide_to);
  cupid_windows_set_last_error(error);
  return result;
}


#endif
#if defined(CUPID_WINDOWS_BUILD)
unsigned int cupid_windows_get_file_attributes(const char *path) {
  unsigned short *wide = path_utf16(path);
  unsigned int result;
  unsigned int error;
  if (wide == (unsigned short *)0) return 0xffffffffu;
  result = cupid_windows_get_file_attributes_wide(wide);
  error = cupid_windows_get_last_error();
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

unsigned int cupid_windows_create_directory(const char *path, void *security) {
  unsigned short *wide = path_utf16(path);
  unsigned int result;
  unsigned int error;
  if (wide == (unsigned short *)0) return 0u;
  result = cupid_windows_create_directory_wide(wide, security);
  error = cupid_windows_get_last_error();
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

unsigned int cupid_windows_remove_directory(const char *path) {
  unsigned short *wide = path_utf16(path);
  unsigned int result;
  unsigned int error;
  if (wide == (unsigned short *)0) return 0u;
  result = cupid_windows_remove_directory_wide(wide);
  error = cupid_windows_get_last_error();
  free(wide);
  cupid_windows_set_last_error(error);
  return result;
}

unsigned int cupid_windows_create_process(
    const char *application, char *command, LPSECURITY_ATTRIBUTES process_security,
    LPSECURITY_ATTRIBUTES thread_security, unsigned int inherit,
    unsigned int flags, void *environment, const char *directory,
    STARTUPINFOA *startup, PROCESS_INFORMATION *process) {
  unsigned short *wide_application = (unsigned short *)0;
  unsigned short *wide_command = (unsigned short *)0;
  unsigned short *wide_directory = (unsigned short *)0;
  unsigned int result = 0u;
  unsigned int error;
  if (startup == (STARTUPINFOA *)0 || environment != (void *)0 ||
      startup->lpReserved != (char *)0 || startup->lpDesktop != (char *)0 ||
      startup->lpTitle != (char *)0 ||
      startup->cb != ((flags & EXTENDED_STARTUPINFO_PRESENT) != 0u ?
        sizeof(STARTUPINFOEXA) : sizeof(STARTUPINFOA))) {
    cupid_windows_set_last_error(87u); return 0u;
  }
  if (application != (const char *)0 &&
      (wide_application = path_utf16(application)) == (unsigned short *)0) goto done;
  if (command != (char *)0 &&
      (wide_command = path_utf16(command)) == (unsigned short *)0) goto done;
  if (directory != (const char *)0 &&
      (wide_directory = path_utf16(directory)) == (unsigned short *)0) goto done;
  result = cupid_windows_create_process_wide(wide_application, wide_command,
      process_security, thread_security, inherit, flags, environment,
      wide_directory, startup, process);
done:
  error = cupid_windows_get_last_error();
  free(wide_application); free(wide_command); free(wide_directory);
  cupid_windows_set_last_error(error);
  return result;
}
#endif
