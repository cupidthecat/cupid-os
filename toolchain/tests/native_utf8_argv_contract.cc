#include <stdio.h>
int cupid_tool_utf8_main(int argc, char **argv);
int cupid_tool_utf8_main(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) puts(argv[i]);
  return 17;
}
