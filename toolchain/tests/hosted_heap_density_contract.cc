#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *allocations[65536];

static int stress(void) {
  unsigned char *slots[256] = {0};
  unsigned int sizes[256] = {0};
  unsigned char values[256] = {0};
  unsigned int step, slot, byte, size, retained, state = 0x71a30b5du;
  unsigned char *replacement;
  for (step = 0u; step < 20000u; step++) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    slot = state & 255u;
    for (byte = 0u; byte < sizes[slot]; byte++) {
      if (slots[slot][byte] != values[slot]) return 40;
    }
    if ((state & 1024u) != 0u) {
      free(slots[slot]);
      slots[slot] = NULL;
      sizes[slot] = 0u;
    } else {
      size = ((state >> 12) & 8191u) + 1u;
      replacement = (unsigned char *)realloc(slots[slot], size);
      if (replacement == NULL || ((uintptr_t)replacement & 15u) != 0u) return 41;
      retained = size < sizes[slot] ? size : sizes[slot];
      for (byte = 0u; byte < retained; byte++) {
        if (replacement[byte] != values[slot]) return 42;
      }
      slots[slot] = replacement;
      sizes[slot] = size;
      values[slot] = (unsigned char)(state >> 24);
      memset(replacement, values[slot], size);
    }
    if (step % 97u == 0u) {
      for (slot = 0u; slot < 256u; slot++) {
        for (byte = 0u; byte < sizes[slot]; byte++) {
          if (slots[slot][byte] != values[slot]) return 43;
        }
      }
    }
  }
  for (slot = 0u; slot < 256u; slot++) {
    for (byte = 0u; byte < sizes[slot]; byte++) {
      if (slots[slot][byte] != values[slot]) return 44;
    }
    free(slots[slot]);
  }
  puts("{\"operations\":20000,\"live_slots\":256,\"preserved_data\":1,\"alignment\":16}");
  return 0;
}

static int density(unsigned int count, unsigned int size) {
  unsigned int index, byte, allocated = 0u;
  for (index = 0u; index < count; index++) {
    allocations[index] = (unsigned char *)malloc(size);
    if (allocations[index] == NULL) break;
    if (((uintptr_t)allocations[index] & 15u) != 0u) return 2;
    allocations[index][0] = (unsigned char)index;
    if (size > 1u) allocations[index][size - 1u] = (unsigned char)(index + 1u);
    allocated++;
  }
  if (allocated != count) {
    int failed_errno = errno;
    for (index = 0u; index < allocated; index++) free(allocations[index]);
    printf("{\"requested\":%u,\"allocated\":%u,\"errno\":%d}\n", count, allocated, failed_errno);
    return 1;
  }
  for (index = 0u; index < count; index++) {
    if (allocations[index][0] != (unsigned char)index ||
        (size > 1u && allocations[index][size - 1u] != (unsigned char)(index + 1u))) return 3;
  }
  for (index = 0u; index < count; index += 2u) {
    free(allocations[index]);
    allocations[index] = NULL;
  }
  for (index = 0u; index < count; index += 2u) {
    allocations[index] = (unsigned char *)calloc(size, 1u);
    if (allocations[index] == NULL || ((uintptr_t)allocations[index] & 15u) != 0u) return 4;
    for (byte = 0u; byte < size; byte++) if (allocations[index][byte] != 0u) return 5;
  }
  for (index = 1u; index < count; index += 2u) {
    if (allocations[index][0] != (unsigned char)index ||
        (size > 1u && allocations[index][size - 1u] != (unsigned char)(index + 1u))) return 6;
    free(allocations[index]);
    allocations[index] = NULL;
  }
  for (index = count; index != 0u; index--) {
    free(allocations[index - 1u]);
    allocations[index - 1u] = NULL;
  }
  printf("{\"requested\":%u,\"allocated\":%u,\"reuse\":1,\"alignment\":16}\n", count, count);
  return 0;
}

static int contracts(void) {
  static const unsigned int sizes[] = {0u, 1u, 15u, 16u, 17u, 31u, 32u,
      63u, 64u, 65u, 4095u, 4096u, 4097u, 65503u, 65504u, 65505u,
      65535u, 65536u, 65537u, 131073u};
  unsigned char *first, *middle, *last, *replacement;
  unsigned int index, size;
  for (index = 0u; index < sizeof(sizes) / sizeof(sizes[0]); index++) {
    size = sizes[index];
    first = (unsigned char *)malloc(size);
    if (first == NULL || ((uintptr_t)first & 15u) != 0u) return 10;
    memset(first, 0x5a, size);
    replacement = (unsigned char *)realloc(first, size + 17u);
    if (replacement == NULL || ((uintptr_t)replacement & 15u) != 0u) return 11;
    for (size = 0u; size < sizes[index]; size++) if (replacement[size] != 0x5au) return 12;
    first = (unsigned char *)realloc(replacement, 1u);
    if (first == NULL) return 13;
    if (sizes[index] != 0u && first[0] != 0x5au) return 14;
    free(first);
  }
  first = (unsigned char *)malloc(64u);
  middle = (unsigned char *)malloc(64u);
  last = (unsigned char *)malloc(64u);
  if (first == NULL || middle == NULL || last == NULL) return 15;
  memset(first, 0x37, 64u);
  memset(last, 0x93, 64u);
  free(middle);
  replacement = (unsigned char *)realloc(first, 128u);
  if (replacement == NULL) return 16;
  for (index = 0u; index < 64u; index++) {
    if (replacement[index] != 0x37u || last[index] != 0x93u) return 17;
  }
  errno = 0;
  if (realloc(replacement, SIZE_MAX) != NULL || errno != ENOMEM) return 18;
  for (index = 0u; index < 64u; index++) if (replacement[index] != 0x37u) return 19;
  errno = 0;
  if (realloc(replacement, 0xfffff000u) != NULL || errno != ENOMEM) return 20;
  for (index = 0u; index < 64u; index++) if (replacement[index] != 0x37u) return 21;
  free(last);
  if (realloc(replacement, 0u) != NULL) return 22;
  first = (unsigned char *)realloc(NULL, 0u);
  if (first == NULL) return 23;
  free(first);
  free(NULL);
  errno = 0;
  if (malloc(SIZE_MAX) != NULL || errno != ENOMEM) return 24;
  errno = 0;
  if (malloc(SIZE_MAX - 31u) != NULL || errno != ENOMEM) return 25;
  errno = 0;
  if (calloc(SIZE_MAX, 2u) != NULL || errno != ENOMEM) return 26;
  printf("{\"contracts\":1,\"overflow\":1,\"failed_realloc_preserves_data\":1,\"zero_size\":1}\n");
  return 0;
}

int main(int argc, char **argv) {
  unsigned int count, size;
  if (argc == 2 && strcmp(argv[1], "stress") == 0) return stress();
  if (argc == 2 && strcmp(argv[1], "contracts") == 0) return contracts();
  if (argc != 3) return 30;
  count = (unsigned int)strtoull(argv[1], NULL, 10);
  size = (unsigned int)strtoull(argv[2], NULL, 10);
  if (count == 0u || count > 65536u || size == 0u || size > 4096u) return 31;
  return density(count, size);
}
