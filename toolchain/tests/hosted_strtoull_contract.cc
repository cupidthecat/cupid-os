#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_digit(unsigned char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  return -1;
}

int main(int argc, char **argv) {
  char encoded[32770], text[16384], *end;
  int base = 0, negative = 0, null_end, index = 0;
  size_t length, offset;
  unsigned long long value;
  if (argc != 3) return 2;
  if (argv[1][0] == '-') { negative = 1; index++; }
  while (argv[1][index] != '\0') {
    if (argv[1][index] < '0' || argv[1][index] > '9' || base > 100) return 3;
    base = base * 10 + argv[1][index++] - '0';
  }
  if (negative) base = -base;
  null_end = strcmp(argv[2], "null") == 0;
  if (!null_end && strcmp(argv[2], "end") != 0) return 4;
  while (fgets(encoded, sizeof(encoded), stdin) != NULL) {
    length = strlen(encoded);
    while (length != 0u && (encoded[length - 1u] == '\n' || encoded[length - 1u] == '\r')) length--;
    if ((length & 1u) != 0u || length / 2u >= sizeof(text)) return 5;
    for (offset = 0u; offset < length / 2u; offset++) {
      int high = hex_digit((unsigned char)encoded[offset * 2u]);
      int low = hex_digit((unsigned char)encoded[offset * 2u + 1u]);
      if (high < 0 || low < 0 || (high == 0 && low == 0)) return 6;
      text[offset] = (char)(high * 16 + low);
    }
    text[length / 2u] = '\0';
    end = text + sizeof(text) - 1u;
    errno = 123;
    value = strtoull(text, null_end ? NULL : &end, base);
    if (!null_end && (end < text || end > text + length / 2u)) return 7;
    printf("%u %u %u %d\n", (unsigned int)(value >> 32u), (unsigned int)value,
           null_end ? 4294967295u : (unsigned int)(end - text),
           errno == ERANGE ? 1 : errno == EINVAL ? 2 : errno == 123 ? 0 : 3);
  }
  return ferror(stdin) || ferror(stdout) ? 8 : 0;
}
