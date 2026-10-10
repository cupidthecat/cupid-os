#include "cupidbuild_host.h"
#include <stdio.h>
#include <string.h>

static int probe_mode;
static char probe_path[8192];

int abi_probe_observer_configure(int mode, const char *path);
int abi_probe_observer_unchanged(cupidbuild_host_observer_t *observer);
int abi_probe_observer_close(cupidbuild_host_observer_t *observer);

int abi_probe_observer_configure(int mode, const char *path) {
  size_t size = path == NULL ? 0u : strlen(path);
  if (size >= sizeof(probe_path)) return 0;
  if (size != 0u) (void)memcpy(probe_path, path, size);
  probe_path[size] = '\0';
  probe_mode = mode;
  return 1;
}

int abi_probe_observer_unchanged(cupidbuild_host_observer_t *observer) {
  if (probe_mode == 1) {
    FILE *stream = fopen(probe_path, "r+b");
    if (stream == NULL) return 0;
    if (fputc('!', stream) == EOF) {
      (void)fclose(stream);
      return 0;
    }
    if (fclose(stream) != 0) return 0;
  }
  return cupidbuild_host_observer_require_unchanged(observer);
}

int abi_probe_observer_close(cupidbuild_host_observer_t *observer) {
  int ok = cupidbuild_host_observer_close(observer);
  return probe_mode == 2 ? 0 : ok;
}
