#define CUPID_NATIVE_UTF8_IMPLEMENTATION
#include "native_utf8.h"
#include <wchar.h>

int cupid_tool_utf8_main(int argc, char **argv);
int wmain(int argc, wchar_t **wide_argv);
int wmain(int argc, wchar_t **wide_argv) {
  char **argv; int index, result = 126;
  if (argc < 0) return 126;
  argv = calloc((size_t)argc + 1, sizeof(*argv)); if (argv == NULL) return 126;
  for (index = 0; index < argc; index++) {
    argv[index] = cupid_native_utf8_from_wide(wide_argv[index], wcslen(wide_argv[index]));
    if (argv[index] == NULL) goto done;
  }
  result = cupid_tool_utf8_main(argc, argv);
done:
  for (index = 0; index < argc; index++) free(argv[index]);
  free(argv);
  cupid_native_utf8_release_environment();
  return result;
}
