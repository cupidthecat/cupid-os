unsigned int binary_words(unsigned int left, unsigned int right) {
  return left ^ right;
}
unsigned int unary_word(unsigned int value) {
  return ~value;
}
unsigned int literal_tail(unsigned int value) {
  return value ^ 0x50000000u;
}
unsigned int through_pointer(const volatile unsigned int *value,
                             unsigned int other) {
  return *value ^ other;
}
unsigned long long full_width(unsigned long long left,
                              unsigned long long right) {
  return left ^ right;
}
unsigned int loop_words(unsigned int value, unsigned int count) {
  while (count != 0u) {
    value ^= count;
    count--;
  }
  return value;
}
unsigned int right_binary_result(unsigned int left, unsigned int right,
                                 unsigned int third) {
  return left ^ (right + third);
}
unsigned int unary_binary_result(unsigned int left, unsigned int right) {
  return ~(left ^ right);
}
unsigned int right_unary_result(unsigned int left, unsigned int right) {
  return left ^ ~right;
}
unsigned int unary_unary_result(unsigned int value) {
  return ~(-value);
}
void stored_sum(volatile unsigned int *output, unsigned int left, unsigned int right) {
  *output = left + right;
}
void narrow_store(volatile unsigned char *output, unsigned int value) {
  *output = (unsigned char)value;
}
int main(void) {
  static const unsigned int values[6] = {
      0u, 1u, 0xffffffffu, 0x80000000u, 0x01234567u, 0xa5a5a5a5u};
  unsigned int left;
  unsigned int right;
  for (left = 0u; left < 6u; left++) {
    volatile unsigned int observed = values[left];
    unsigned int expected = observed;
    unsigned int count;
    for (right = 0u; right < 6u; right++) {
      unsigned int other = values[right];
      volatile unsigned int sum = 0u;
      volatile unsigned char narrow = 0u;
      stored_sum(&sum, observed, other);
      narrow_store(&narrow, observed);
      if (sum != observed + other || narrow != (unsigned char)observed) return 3;
      unsigned long long wide_left = ((unsigned long long)observed << 32u) | other;
      unsigned long long wide_right = ((unsigned long long)other << 32u) | observed;
      if (binary_words(observed, other) != (observed ^ other) ||
          unary_word(observed) != ~observed ||
          literal_tail(observed) != (observed ^ 0x50000000u) ||
          through_pointer(&observed, other) != (observed ^ other) ||
          full_width(wide_left, wide_right) != (wide_left ^ wide_right) ||
          right_binary_result(observed, other, 0x80000001u) != (observed ^ (other + 0x80000001u)) ||
          unary_binary_result(observed, other) != ~(observed ^ other) ||
          right_unary_result(observed, other) != (observed ^ ~other) ||
          unary_unary_result(observed) != ~(-observed))
        return 1;
    }
    for (count = 31u; count != 0u; count--) expected ^= count;
    if (loop_words(observed, 31u) != expected) return 2;
  }
  return 0;
}
