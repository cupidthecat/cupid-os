#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *cupid_windows_command_line_utf8(const unsigned short *wide);

int main(int argc, char **argv) {
  unsigned short invalid[] = {0xd800u, 0u};
  unsigned short empty[] = {0u};
  char *converted;
  FILE *file;
  char data[4];
  if (argc != 6 || strcmp(argv[1], "caf\xc3\xa9") != 0 ||
      strcmp(argv[2], "\xf0\x9f\x98\xba space") != 0 ||
      strcmp(argv[3], "a \"quoted\" path\\") != 0) return 1;
  if (cupid_windows_command_line_utf8(invalid) != (char *)0 ||
      cupid_windows_command_line_utf8((const unsigned short *)0) != (char *)0) return 2;
  converted = cupid_windows_command_line_utf8(empty);
  if (converted == (char *)0 || converted[0] != 0) return 3;
  free(converted);
  file = fopen(argv[4], "rb");
  if (file == (FILE *)0) return 4;
  if (fread(data, 1u, sizeof(data), file) != sizeof(data) || fclose(file) != 0 ||
      memcmp(data, "data", sizeof(data)) != 0) return 5;
  file = fopen(argv[5], "wb");
  if (file == (FILE *)0) return 6;
  if (fwrite(data, 1u, sizeof(data), file) != sizeof(data) || fclose(file) != 0) return 7;
  return 0;
}
