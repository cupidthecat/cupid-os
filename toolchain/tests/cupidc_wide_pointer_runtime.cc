struct triple { int first; int second; int third; };

unsigned char unsigned_index(const unsigned char *bytes, unsigned long long index) {
  return bytes[index];
}
unsigned char signed_index(const unsigned char *bytes, long long index) {
  return bytes[index];
}
unsigned char reversed_index(const unsigned char *bytes, unsigned long long index) {
  return index[bytes];
}
unsigned char computed_index(const unsigned char *bytes, unsigned long long index) {
  return bytes[index + 1ULL];
}
int *signed_add(int *pointer, long long offset) { return pointer + offset; }
int *reversed_add(int *pointer, unsigned long long offset) { return offset + pointer; }
int *signed_subtract(int *pointer, long long offset) { return pointer - offset; }
struct triple *record_add(struct triple *pointer, long long offset) { return pointer + offset; }
int array_index(int (*rows)[3], unsigned long long index) { return rows[index][2]; }

static unsigned int address_calls;
static unsigned int offset_calls;
static int **destination(int **pointer) { address_calls++; return pointer; }
static long long delta(long long value) { offset_calls++; return value; }
int *compound_add(int **pointer, long long offset) {
  return *destination(pointer) += delta(offset);
}
int *compound_subtract(int **pointer, long long offset) {
  return *destination(pointer) -= delta(offset);
}
unsigned char postfix_index(const unsigned char *bytes, unsigned long long *index) {
  return bytes[(*index)++];
}
unsigned char volatile_index(const unsigned char *bytes, const volatile long long *index) {
  return bytes[*index];
}

int main(void) {
  static const unsigned char bytes[8] = {10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u};
  static int words[8];
  static struct triple records[4];
  static int rows[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  unsigned long long index;
  int *cursor = words + 2;
  volatile long long observed = -2LL;
  for (index = 0u; index < 8u; index++) {
    if (unsigned_index(bytes, index) != bytes[(unsigned int)index] ||
        reversed_index(bytes, index) != bytes[(unsigned int)index]) return 1;
  }
  if (signed_index(bytes + 4, -3LL) != 11u ||
      computed_index(bytes, 4ULL) != 15u ||
      volatile_index(bytes + 4, &observed) != 12u) return 2;
  if (signed_add(words + 4, -3LL) != words + 1 ||
      reversed_add(words, 7ULL) != words + 7 ||
      signed_subtract(words + 1, -3LL) != words + 4 ||
      record_add(records + 3, -2LL) != records + 1 ||
      array_index(rows, 2ULL) != 9) return 3;
  if (compound_add(&cursor, 3LL) != words + 5 || cursor != words + 5 ||
      address_calls != 1u || offset_calls != 1u) return 4;
  if (compound_subtract(&cursor, -2LL) != words + 7 || cursor != words + 7 ||
      address_calls != 2u || offset_calls != 2u) return 5;
  index = 3u;
  if (postfix_index(bytes, &index) != 13u || index != 4u) return 6;
  return 0;
}
