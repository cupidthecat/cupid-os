#include "path_encoding.h"
#include <string.h>

int main(int argc, char **argv) {
  const char *valid[] = {"\x01", "\x7f", "\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80",
      "\xed\x9f\xbf", "\xee\x80\x80", "\xef\xbf\xbf", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"};
  unsigned int scalars[] = {1u, 0x7fu, 0x80u, 0x7ffu, 0x800u, 0xd7ffu, 0xe000u, 0xffffu, 0x10000u, 0x10ffffu};
  const char *invalid[] = {"\x80", "\xc0\x80", "\xc1\xbf", "\xc2", "\xe0\x80\x80", "\xe2", "\xe2\x82",
      "\xed\xa0\x80", "\xf0\x80\x80\x80", "\xf0\x9f", "\xf0\x9f\x98", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff"};
  unsigned short wide[8];
  unsigned short bad[] = {0xd800u, 'x', 0xdc00u, 0u};
  unsigned short pair[] = {0xd800u, 0xdc00u};
  char text[16];
  char embedded[] = {'a', 0, 'b'};
  size_t units;
  size_t bytes;
  size_t count;
  unsigned int index;
  (void)argc; (void)argv;
  for (index = 0u; index < 10u; index++) {
    size_t size = strlen(valid[index]);
    if (!cupidbuild_path_to_utf16(valid[index], size, (unsigned short *)0, 0u, &units)) return 1;
    if (units != (scalars[index] >= 0x10000u ? 2u : 1u)) return 2;
    if (!cupidbuild_path_to_utf16(valid[index], size, wide, units + 1u, &count) || count != units || wide[units] != 0u) return 3;
    if (units == 1u && wide[0] != scalars[index]) return 4;
    if (!cupidbuild_path_to_utf8(wide, units, (char *)0, 0u, &bytes) || bytes != size) return 5;
    if (!cupidbuild_path_to_utf8(wide, units, text, bytes + 1u, &count) || count != size || strcmp(text, valid[index])) return 6;
    (void)memset(text, 'x', sizeof(text));
    if (cupidbuild_path_to_utf8(wide, units, text, bytes, &count) || count || text[0] || text[1] != 'x') return 7;
    wide[0] = 7u; wide[1] = 7u;
    if (cupidbuild_path_to_utf16(valid[index], size, wide, units, &count) || count || wide[0] || wide[1] != 7u) return 8;
  }
  for (index = 0u; index < 14u; index++) {
    if (cupidbuild_path_to_utf16(invalid[index], strlen(invalid[index]), wide, 8u, &units) || units || wide[0]) return 9;
  }
  if (cupidbuild_path_to_utf16(embedded, 3u, wide, 8u, &units) || units) return 10;
  if (cupidbuild_path_to_utf16("\xc3\xa9", 1u, wide, 8u, &units) || units) return 11;
  if (cupidbuild_path_to_utf8(pair, 1u, text, sizeof(text), &bytes) || bytes) return 12;
  if (cupidbuild_path_to_utf8(bad, 2u, text, sizeof(text), &bytes) || bytes) return 13;
  if (cupidbuild_path_to_utf8(bad + 2u, 1u, text, sizeof(text), &bytes) || bytes) return 14;
  if (cupidbuild_path_to_utf8(bad + 3u, 1u, text, sizeof(text), &bytes) || bytes) return 15;
  if (!cupidbuild_path_to_utf16((const char *)0, 0u, wide, 1u, &units) || units || wide[0]) return 16;
  if (!cupidbuild_path_to_utf8((const unsigned short *)0, 0u, text, 1u, &bytes) || bytes || text[0]) return 17;
  if (cupidbuild_path_to_utf16("a", 1u, (unsigned short *)0, 1u, &units) || units) return 18;
  if (cupidbuild_path_to_utf8(pair, 2u, (char *)0, 1u, &bytes) || bytes) return 19;
  return 0;
}
