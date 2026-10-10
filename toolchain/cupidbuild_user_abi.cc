#include "cupidbuild_user_abi.h"
#include "cupidbuild_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int abi_normalize_root(char *root) {
  const char *cursor = root;
  size_t written = 1u;
  if (cupidbuild_host_execution_format() == 2u) {
    size_t length = strlen(root);
    while (length > 3u && (root[length - 1u] == '/' || root[length - 1u] == '\\'))
      root[--length] = '\0';
    return 1;
  }
  if (*cursor != '/') return 0;
  while (*cursor != '\0') {
    const char *component;
    size_t length;
    while (*cursor == '/') cursor++;
    component = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    length = (size_t)(cursor - component);
    if (length == 0u || (length == 1u && component[0] == '.')) continue;
    /* Collapsing a parent component could hide a linked or missing ancestor. */
    if (length == 2u && component[0] == '.' && component[1] == '.') return 0;
    if (written > 1u) root[written++] = '/';
    (void)memmove(root + written, component, length);
    written += length;
  }
  root[written] = '\0';
  return 1;
}

int cupidbuild_verify_user_abi(const char *repository_root,
                                cupid_user_abi_report_t *result,
                                char *error, size_t error_size) {
  cupidbuild_host_observer_t *observer = NULL;
  cupid_user_abi_input_t inputs[CUPID_USER_ABI_INPUT_COUNT];
  unsigned char *owned[CUPID_USER_ABI_INPUT_COUNT];
  cupid_user_abi_report_t candidate;
  char absolute_root[8192];
  char detail[CUPID_USER_ABI_ERROR_BYTES];
  size_t index;
  int ok = 0;
  (void)memset(inputs, 0, sizeof(inputs));
  (void)memset(owned, 0, sizeof(owned));
  (void)memset(&candidate, 0, sizeof(candidate));
  detail[0] = '\0';
  if (error != NULL && error_size != 0u) error[0] = '\0';
  if (result != NULL) (void)memset(result, 0, sizeof(*result));
  if (repository_root == NULL || repository_root[0] == '\0' || result == NULL) {
    (void)snprintf(detail, sizeof(detail), "ABI repository root and result are required");
    goto done;
  }
  if (!cupidbuild_host_absolute_root(repository_root, absolute_root,
                                      sizeof(absolute_root)) ||
      !abi_normalize_root(absolute_root)) {
    (void)snprintf(detail, sizeof(detail), "cannot resolve ABI repository root");
    goto done;
  }
  if (!cupidbuild_host_observer_open(absolute_root, &observer)) {
    (void)snprintf(detail, sizeof(detail), "%s", cupidbuild_host_observer_error(observer));
    goto done;
  }
  for (index = 0u; index < CUPID_USER_ABI_INPUT_COUNT; index++) {
    unsigned char *bytes = NULL;
    uint64_t size = 0u;
    if (!cupidbuild_host_observer_file(observer, cupid_user_abi_input_path(index),
                                        CUPID_USER_ABI_SOURCE_BYTES, &bytes, &size)) {
      (void)snprintf(detail, sizeof(detail), "%s", cupidbuild_host_observer_error(observer));
      goto done;
    }
    inputs[index].bytes = bytes;
    inputs[index].size = (size_t)size;
    owned[index] = bytes;
  }
  if (!cupid_user_abi_validate(inputs, &candidate, detail, sizeof(detail)))
    goto done;
  if (!cupidbuild_host_observer_require_unchanged(observer)) {
    (void)snprintf(detail, sizeof(detail), "%s", cupidbuild_host_observer_error(observer));
    goto done;
  }
  ok = 1;
done:
  for (index = 0u; index < CUPID_USER_ABI_INPUT_COUNT; index++)
    free(owned[index]);
  if (!cupidbuild_host_observer_close(observer)) {
    if (ok || detail[0] == '\0')
      (void)snprintf(detail, sizeof(detail), "cannot close ABI input observations");
    ok = 0;
  }
  if (ok) *result = candidate;
  else if (error != NULL && error_size != 0u)
    (void)snprintf(error, error_size, "%s", detail[0] != '\0'
                      ? detail : "ABI input observation failed");
  return ok;
}
