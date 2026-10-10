#include "fat16_names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned read_u32(const unsigned char *p) {
  return (unsigned)p[0] | ((unsigned)p[1] << 8u) |
         ((unsigned)p[2] << 16u) | ((unsigned)p[3] << 24u);
}

static void write_u32(unsigned char *p, unsigned value) {
  p[0] = (unsigned char)value;
  p[1] = (unsigned char)(value >> 8u);
  p[2] = (unsigned char)(value >> 16u);
  p[3] = (unsigned char)(value >> 24u);
}

int main(int argc, char **argv) {
  FILE *input, *output;
  unsigned char header[20];
  if (argc != 3) return 2;
  input = fopen(argv[1], "rb");
  if (!input) return 3;
  output = fopen(argv[2], "wb");
  if (!output) { fclose(input); return 4; }
  for (;;) {
    unsigned mode, profile, flags, size, capacity, stored, count = 91u;
    unsigned char *bytes;
    ctool_fat16_name_t *names;
    ctool_string_t text;
    ctool_status_t status;
    unsigned char report[12];
    unsigned unchanged = 1u, i;
    size_t got = fread(header, 1u, sizeof(header), input);
    if (got == 0u && !ferror(input)) break;
    if (got != sizeof(header)) return 5;
    mode = read_u32(header);
    profile = read_u32(header + 4u);
    flags = read_u32(header + 8u);
    size = read_u32(header + 12u);
    capacity = read_u32(header + 16u);
    if (mode > 1u || size > 1048576u ||
        (capacity > 4096u && capacity <= 0xffffffffu / 11u)) return 6;
    stored = mode == 0u ? 1u : (capacity > 4096u ? 1u : capacity);
    bytes = malloc(size + 1u);
    names = malloc((stored == 0u ? 1u : stored) * sizeof(*names));
    if (!bytes || !names) return 7;
    if (fread(bytes, 1u, size, input) != size) return 8;
    bytes[size] = 0u;
    memset(names, 0xa5, (stored == 0u ? 1u : stored) * sizeof(*names));
    text.data = flags & 1u ? NULL : (const char *)bytes;
    text.size = size;
    if (mode == 0u) {
      status = ctool_fat16_project_component(text,
          (ctool_fat16_name_profile_t)profile, flags & 2u ? NULL : names);
      count = 0u;
    } else {
      status = ctool_fat16_project_destination(text,
          (ctool_fat16_name_profile_t)profile, flags & 2u ? NULL : names,
          capacity, flags & 4u ? NULL : &count);
    }
    for (i = 0u; i < stored * 11u; i++) {
      if (((unsigned char *)names)[i] != 0xa5u) unchanged = 0u;
    }
    write_u32(report, (unsigned)status);
    write_u32(report + 4u, count);
    write_u32(report + 8u, unchanged);
    if (fwrite(report, 1u, sizeof(report), output) != sizeof(report) ||
        fwrite(names, 11u, stored, output) != stored) return 9;
    free(names);
    free(bytes);
  }
  if (fclose(input) || fflush(output) || fclose(output)) return 10;
  return 0;
}
