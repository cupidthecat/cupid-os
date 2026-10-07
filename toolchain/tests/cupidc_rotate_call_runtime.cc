static unsigned int local_right(unsigned int value, unsigned int count) {
  return (value >> count) | (value << (32u - count));
}
static unsigned int local_left(unsigned int value, unsigned int count) {
  return (value << count) | (value >> (32u - count));
}
static unsigned int local_reversed(unsigned int count, unsigned int value) {
  return (value << count) | (value >> (32u - count));
}
static unsigned long local_long(unsigned long value, unsigned long count) {
  return (value >> count) | (value << (32ul - count));
}
unsigned int external_right(unsigned int value, unsigned int count) {
  return (value >> count) | (value << (32u - count));
}
static unsigned int __attribute__((noinline)) retained_right(
    unsigned int value, unsigned int count) {
  return (value >> count) | (value << (32u - count));
}
static unsigned int qualified_right(volatile unsigned int value,
                                    unsigned int count) {
  return (value >> count) | (value << (32u - count));
}
unsigned int static_right(unsigned int value, unsigned int count) {
  return local_right(value, count);
}
unsigned int static_left(unsigned int value, unsigned int count) {
  return local_left(value, count);
}
unsigned int static_reversed(unsigned int value, unsigned int count) {
  return local_reversed(count, value);
}
unsigned long static_long(unsigned long value, unsigned long count) {
  return local_long(value, count);
}
unsigned int static_right_7(unsigned int value) {
  return local_right(value, 7u);
}
unsigned int static_left_1(unsigned int value) {
  return local_left(value, 1u);
}
unsigned int static_right_31(unsigned int value) {
  return local_right(value, 31u);
}
unsigned int static_zero(unsigned int value) {
  return local_right(value, 0u);
}
unsigned int static_32(unsigned int value) {
  return local_right(value, 32u);
}
unsigned int external_constant(unsigned int value) {
  return external_right(value, 7u);
}
unsigned int surrounding_value(unsigned int value, unsigned int count) {
  return 1u + (local_right(value, count) ^ value);
}
unsigned int external_call(unsigned int value, unsigned int count) {
  return external_right(value, count);
}
unsigned int noinline_call(unsigned int value, unsigned int count) {
  return retained_right(value, count);
}
unsigned int qualified_call(unsigned int value, unsigned int count) {
  return qualified_right(value, count);
}
unsigned int indirect_call(unsigned int (*function)(unsigned int, unsigned int),
                           unsigned int value, unsigned int count) {
  return function(value, count);
}
static volatile unsigned int value_calls;
static volatile unsigned int count_calls;
static unsigned int value_argument(unsigned int value) {
  value_calls++;
  return value;
}
static unsigned int count_argument(unsigned int count) {
  count_calls++;
  return count;
}
unsigned int evaluated_arguments(unsigned int value, unsigned int count) {
  return local_right(value_argument(value), count_argument(count));
}
int main(void) {
  static const unsigned int values[6] = {
      0u, 1u, 0xffffffffu, 0x80000000u, 0x01234567u, 0xa5a5a5a5u};
  unsigned int index;
  unsigned int count;
  for (index = 0u; index < 6u; index++) {
    for (count = 1u; count < 32u; count++) {
      unsigned int value = values[index];
      unsigned int right = (value >> count) | (value << (32u - count));
      unsigned int left = (value << count) | (value >> (32u - count));
      if (static_right(value, count) != right ||
          static_left(value, count) != left ||
          static_reversed(value, count) != left ||
          static_long((unsigned long)value, (unsigned long)count) != right ||
          static_right_7(value) != ((value >> 7u) | (value << 25u)) ||
          static_left_1(value) != ((value << 1u) | (value >> 31u)) ||
          static_right_31(value) != ((value >> 31u) | (value << 1u)) ||
          external_constant(value) != ((value >> 7u) | (value << 25u)) ||
          surrounding_value(value, count) != 1u + (right ^ value) ||
          external_call(value, count) != right ||
          noinline_call(value, count) != right ||
          qualified_call(value, count) != right ||
          indirect_call(external_right, value, count) != right ||
          evaluated_arguments(value, count) != right)
        return 1;
    }
  }
  return value_calls == 186u && count_calls == 186u ? 0 : 2;
}
