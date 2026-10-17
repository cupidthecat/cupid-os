typedef unsigned int u32;

u32 rotate_right(u32 value, u32 count) {
  return (value >> count) | (value << (32u - count));
}

u32 rotate_left(u32 value, u32 count) {
  return (value << count) | (value >> (32u - count));
}

u32 rotate_reversed(u32 count, u32 value) {
  return (value >> count) | (value << (32u - count));
}

unsigned long rotate_long(unsigned long value, unsigned long count) {
  return (value >> count) | (value << (32ul - count));
}

u32 volatile_count(u32 value, volatile u32 count) {
  return (value >> count) | (value << (32u - count));
}

unsigned long mixed_types(unsigned long value, unsigned int count) {
  return (value >> count) | (value << (32ul - count));
}

u32 different_values(u32 value, u32 count) {
  return (value >> count) | (count << (32u - count));
}

u32 different_complement(u32 value, u32 count) {
  return (value >> count) | (value << (31u - count));
}

u32 volatile_value(volatile u32 value, u32 count) {
  return (value >> count) | (value << (32u - count));
}

int signed_value(int value, unsigned int count) {
  return (value >> count) | (value << (32u - count));
}

u32 signed_count(u32 value, int count) {
  return (value >> count) | (value << (32u - count));
}

unsigned long long wide_value(unsigned long long value, unsigned int count) {
  return (value >> count) | (value << (64u - count));
}

int main(void) {
  static const u32 values[] = {0u, 1u, 0x80000000u, 0xffffffffu,
                               0x12345678u, 0xabcdef01u};
  unsigned int value;
  unsigned int count;
  for (value = 0u; value < sizeof(values) / sizeof(values[0]); value++) {
    for (count = 1u; count < 32u; count++) {
      u32 right = values[value] >> count | values[value] << (32u - count);
      u32 left = values[value] << count | values[value] >> (32u - count);
      if (rotate_right(values[value], count) != right ||
          rotate_left(values[value], count) != left ||
          rotate_reversed(count, values[value]) != right ||
          rotate_long(values[value], count) != right ||
          mixed_types(values[value], count) != right ||
          volatile_value(values[value], count) != right ||
          volatile_count(values[value], count) != right ||
          signed_value(0, count) != 0 ||
          signed_count(values[value], (int)count) != right) return 1;
      if (different_values(values[value], count) !=
          (values[value] >> count | count << (32u - count))) return 2;
      if (different_complement(values[value], count) !=
          (values[value] >> count | values[value] << (31u - count))) return 3;
    }
  }
  if (wide_value(0x123456789abcdef0ull, 17u) !=
      (0x123456789abcdef0ull >> 17u | 0x123456789abcdef0ull << 47u)) return 4;
  return 0;
}
