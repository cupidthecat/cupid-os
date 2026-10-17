#include <direct.h>
#include <errno.h>
#include <stdlib.h>
#include <windows.h>

#define CUPID_WINDOWS_ERROR_FILE_NOT_FOUND 2u
#define CUPID_WINDOWS_ERROR_PATH_NOT_FOUND 3u
#define CUPID_WINDOWS_ERROR_NOT_ENOUGH_MEMORY 8u
#define CUPID_WINDOWS_ERROR_OUTOFMEMORY 14u

static void cupid_windows_fullpath_error(void) {
  unsigned int error = GetLastError();
  if (error == CUPID_WINDOWS_ERROR_FILE_NOT_FOUND ||
      error == CUPID_WINDOWS_ERROR_PATH_NOT_FOUND) {
    errno = ENOENT;
  } else if (error == CUPID_WINDOWS_ERROR_NOT_ENOUGH_MEMORY ||
             error == CUPID_WINDOWS_ERROR_OUTOFMEMORY) {
    errno = ENOMEM;
  } else {
    errno = EINVAL;
  }
}

char *_fullpath(char *destination, const char *path, size_t capacity) {
  unsigned int result;
  if (path == (const char *)0 || path[0] == '\0') {
    errno = EINVAL;
    return (char *)0;
  }
  if (destination == (char *)0) {
    unsigned int required =
        GetFullPathNameA(path, 0u, (char *)0, (char **)0);
    if (required == 0u) {
      cupid_windows_fullpath_error();
      return (char *)0;
    }
    destination = (char *)malloc((size_t)required);
    if (destination == (char *)0) {
      errno = ENOMEM;
      return (char *)0;
    }
    result = GetFullPathNameA(path, required, destination, (char **)0);
    if (result == 0u || result >= required) {
      free(destination);
      if (result == 0u) {
        cupid_windows_fullpath_error();
      } else {
        errno = ERANGE;
      }
      return (char *)0;
    }
    return destination;
  }
  if (capacity == 0u) {
    errno = EINVAL;
    return (char *)0;
  }
  result = GetFullPathNameA(path, (unsigned int)capacity, destination,
                            (char **)0);
  if (result == 0u) {
    cupid_windows_fullpath_error();
    return (char *)0;
  }
  if ((size_t)result >= capacity) {
    destination[0] = '\0';
    errno = ERANGE;
    return (char *)0;
  }
  return destination;
}
