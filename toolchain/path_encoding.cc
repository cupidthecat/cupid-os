#include "path_encoding.h"

static int next_utf8(const char *input, size_t size, size_t *offset, unsigned int *scalar) {
  size_t index = *offset;
  unsigned int value;
  unsigned int continuation = 0u;
  unsigned int minimum = 0u;
  if (index >= size) return 0;
  value = (unsigned char)input[index++];
  if (value >= 0xc2u && value <= 0xdfu) {
    value &= 31u; continuation = 1u; minimum = 0x80u;
  } else if (value >= 0xe0u && value <= 0xefu) {
    value &= 15u; continuation = 2u; minimum = 0x800u;
  } else if (value >= 0xf0u && value <= 0xf4u) {
    value &= 7u; continuation = 3u; minimum = 0x10000u;
  } else if (value == 0u || value >= 0x80u) return 0;
  if (continuation > size - index) return 0;
  while (continuation != 0u) {
    unsigned int byte = (unsigned char)input[index++];
    if (byte < 0x80u || byte > 0xbfu) return 0;
    value = (value << 6u) | (byte & 63u);
    continuation--;
  }
  if (value < minimum || value > 0x10ffffu ||
      (value >= 0xd800u && value <= 0xdfffu)) return 0;
  *offset = index; *scalar = value;
  return 1;
}

static int next_utf16(const unsigned short *input, size_t size, size_t *offset, unsigned int *scalar) {
  size_t index = *offset;
  unsigned int value;
  if (index >= size) return 0;
  value = input[index++];
  if (value >= 0xd800u && value <= 0xdbffu) {
    unsigned int low;
    if (index >= size) return 0;
    low = input[index++];
    if (low < 0xdc00u || low > 0xdfffu) return 0;
    value = 0x10000u + ((value - 0xd800u) << 10u) + low - 0xdc00u;
  } else if (value == 0u || (value >= 0xdc00u && value <= 0xdfffu)) return 0;
  *offset = index; *scalar = value;
  return 1;
}

int cupidbuild_path_to_utf16(const char *input, size_t bytes,
    unsigned short *output, size_t capacity, size_t *units) {
  size_t offset = 0u;
  size_t needed = 0u;
  size_t used = 0u;
  unsigned int scalar;
  if (units != (size_t *)0) *units = 0u;
  if (output != (unsigned short *)0 && capacity != 0u) output[0] = 0u;
  if (units == (size_t *)0 || (input == (const char *)0 && bytes != 0u) ||
      (output == (unsigned short *)0 && capacity != 0u)) return 0;
  while (offset < bytes) {
    size_t count;
    if (!next_utf8(input, bytes, &offset, &scalar)) return 0;
    count = scalar >= 0x10000u ? 2u : 1u;
    if (needed > (size_t)-1 - count - 1u) return 0;
    needed += count;
  }
  if (output == (unsigned short *)0) { *units = needed; return 1; }
  if (needed >= capacity) return 0;
  offset = 0u;
  while (offset < bytes) {
    if (!next_utf8(input, bytes, &offset, &scalar)) return 0;
    if (scalar >= 0x10000u) {
      scalar -= 0x10000u;
      output[used++] = (unsigned short)(0xd800u | (scalar >> 10u));
      output[used++] = (unsigned short)(0xdc00u | (scalar & 1023u));
    } else output[used++] = (unsigned short)scalar;
  }
  output[used] = 0u;
  *units = used;
  return 1;
}

int cupidbuild_path_to_utf8(const unsigned short *input, size_t units,
    char *output, size_t capacity, size_t *bytes) {
  size_t offset = 0u;
  size_t needed = 0u;
  size_t used = 0u;
  unsigned int scalar;
  if (bytes != (size_t *)0) *bytes = 0u;
  if (output != (char *)0 && capacity != 0u) output[0] = 0;
  if (bytes == (size_t *)0 || (input == (const unsigned short *)0 && units != 0u) ||
      (output == (char *)0 && capacity != 0u)) return 0;
  while (offset < units) {
    size_t count;
    if (!next_utf16(input, units, &offset, &scalar)) return 0;
    count = scalar < 0x80u ? 1u : (scalar < 0x800u ? 2u : (scalar < 0x10000u ? 3u : 4u));
    if (needed > (size_t)-1 - count - 1u) return 0;
    needed += count;
  }
  if (output == (char *)0) { *bytes = needed; return 1; }
  if (needed >= capacity) return 0;
  offset = 0u;
  while (offset < units) {
    if (!next_utf16(input, units, &offset, &scalar)) return 0;
    if (scalar < 0x80u) output[used++] = (char)scalar;
    else {
      if (scalar < 0x800u) output[used++] = (char)(0xc0u | (scalar >> 6u));
      else if (scalar < 0x10000u) output[used++] = (char)(0xe0u | (scalar >> 12u));
      else output[used++] = (char)(0xf0u | (scalar >> 18u));
      if (scalar >= 0x10000u) output[used++] = (char)(0x80u | ((scalar >> 12u) & 63u));
      if (scalar >= 0x800u) output[used++] = (char)(0x80u | ((scalar >> 6u) & 63u));
      output[used++] = (char)(0x80u | (scalar & 63u));
    }
  }
  output[used] = 0;
  *bytes = used;
  return 1;
}

int cupidbuild_path_windows_extended(const unsigned short *input, size_t units,
    unsigned short *output, size_t capacity, size_t *written) {
  size_t offset = 0u, anchor, extra, index, start, result;
  unsigned int scalar;
  int unc;
  if (written != (size_t *)0) *written = 0u;
  if (output != (unsigned short *)0 && capacity != 0u) output[0] = 0u;
  if (written == (size_t *)0 || input == (const unsigned short *)0 ||
      units < 3u || units > 32766u ||
      (output == (unsigned short *)0 && capacity != 0u)) return 0;
  while (offset < units) if (!next_utf16(input, units, &offset, &scalar)) return 0;
  unc = input[0] == '\\' && input[1] == '\\';
  if (unc) {
    size_t server = 2u, share;
    if ((units >= 4u && (input[2] == '?' || input[2] == '.') && input[3] == '\\') ||
        input[2] == '\\') return 0;
    while (server < units && input[server] != '\\') server++;
    share = server + 1u;
    if (share >= units || input[share] == '\\') return 0;
    while (share < units && input[share] != '\\') share++;
    anchor = 2u;
    extra = 6u;
  } else {
    if (!((input[0] >= 'A' && input[0] <= 'Z') ||
          (input[0] >= 'a' && input[0] <= 'z')) ||
        input[1] != ':' || input[2] != '\\') return 0;
    anchor = 3u;
    extra = 4u;
  }
  if (units > 32766u - extra) return 0;
  for (index = 0u; index < units; index++) if (input[index] == '/') return 0;
  start = anchor;
  for (index = anchor; index <= units; index++) {
    if (index == units || input[index] == '\\') {
      size_t length = index - start;
      if ((length == 1u && input[start] == '.') ||
          (length == 2u && input[start] == '.' && input[start + 1u] == '.') ||
          (length == 0u && index != units)) return 0;
      start = index + 1u;
    }
  }
  result = units + extra;
  if (output == (unsigned short *)0) { *written = result; return 1; }
  if (capacity <= result) return 0;
  output[0] = '\\'; output[1] = '\\'; output[2] = '?'; output[3] = '\\';
  if (unc) {
    output[4] = 'U'; output[5] = 'N'; output[6] = 'C'; output[7] = '\\';
    for (index = 2u; index < units; index++) output[index + 6u] = input[index];
  } else for (index = 0u; index < units; index++) output[index + 4u] = input[index];
  output[result] = 0u;
  *written = result;
  return 1;
}
