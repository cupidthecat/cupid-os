typedef unsigned long long u64;
typedef long long i64;

unsigned int word_add(unsigned int value, u64 right) { return value += right; }
unsigned int word_subtract(unsigned int value, u64 right) { return value -= right; }
unsigned int word_multiply(unsigned int value, u64 right) { return value *= right; }
unsigned int word_divide(unsigned int value, u64 right) { return value /= right; }
unsigned int word_remainder(unsigned int value, u64 right) { return value %= right; }
unsigned int word_and(unsigned int value, u64 right) { return value &= right; }
unsigned int word_or(unsigned int value, u64 right) { return value |= right; }
unsigned int word_xor(unsigned int value, u64 right) { return value ^= right; }
unsigned int word_shift(unsigned int value, unsigned int count) {
  value <<= count;
  return value >>= 3u;
}
int signed_divide(int value, i64 right) { return value /= right; }
int signed_remainder(int value, i64 right) { return value %= right; }
int mixed_signedness(int value, u64 right) { return value += right; }
unsigned char byte_add(unsigned char value, u64 right) { return value += right; }
signed char byte_divide(signed char value, i64 right) { return value /= right; }
signed char byte_remainder(signed char value, i64 right) { return value %= right; }
unsigned short halfword_add(unsigned short value, u64 right) { return value += right; }
short halfword_subtract(short value, i64 right) { return value -= right; }
unsigned short halfword_multiply(unsigned short value, u64 right) { return value *= right; }
unsigned long long_destination(unsigned long value, u64 right) { return value += right; }
_Static_assert(sizeof(unsigned long) == 4u, "the destination is an i386 word");

enum lane { lane_negative = -1, lane_zero = 0, lane_positive = 100 };
enum lane enum_add(enum lane value, i64 right) { return value += right; }

static unsigned int address_calls;
static unsigned int right_calls;
static unsigned short *select_destination(unsigned short *value) {
  address_calls++;
  return value;
}
static u64 select_right(u64 value) {
  right_calls++;
  return value;
}
unsigned short one_evaluation(unsigned short *value, u64 right) {
  return *select_destination(value) += select_right(right);
}

struct lanes { unsigned char before; volatile unsigned char value; unsigned char after; };
unsigned char volatile_byte(struct lanes *state, u64 right) {
  return state->value += right;
}
struct flags { unsigned int value : 9; unsigned int keep : 7; int signed_value : 12; unsigned int pad : 4; };
unsigned int field_add(struct flags *state, u64 right) { return state->value += right; }
unsigned int field_divide(struct flags *state, u64 right) { return state->value /= right; }
int field_remainder(struct flags *state, i64 right) { return state->signed_value %= right; }
struct whole_flag { volatile unsigned int value : 32; };
unsigned int volatile_field(struct whole_flag *state, u64 right) { return state->value ^= right; }

int main(void) {
  unsigned short selected = 65530u;
  unsigned char left = 250u;
  unsigned char right = 250u;
  volatile unsigned int observed = 0xffffffffu;
  struct lanes bytes;
  struct flags fields;
  struct whole_flag whole;
  bytes.before = 0x55u;
  bytes.value = 250u;
  bytes.after = 0xaau;
  fields.value = 510u;
  fields.keep = 85u;
  fields.signed_value = -100;
  fields.pad = 10u;
  whole.value = 0x12345678u;
  if (word_add(0xfffffff0u, 0x100000031ULL) != 0x21u ||
      word_subtract(0x20u, 0x100000041ULL) != 0xffffffdfu ||
      word_multiply(3u, 0x100000005ULL) != 15u) return 1;
  /* Truncating the divisor first changes each of these results. */
  if (word_divide(0xffffffffu, 0x100000001ULL) != 0u ||
      word_remainder(0xffffffffu, 0x100000001ULL) != 0xffffffffu ||
      signed_divide(-2147483000, 0x100000003LL) != 0 ||
      signed_remainder(-2147483000, -0x100000003LL) != -2147483000) return 2;
  if (word_and(0xa5a5f0f0u, 0xfedcba980000ffffULL) != 0xf0f0u ||
      word_or(0x10u, 0x100000003ULL) != 0x13u ||
      word_xor(0xf0u, 0xabcdef0000000055ULL) != 0xa5u ||
      word_shift(1u, 5u) != 4u) return 3;
  if (byte_add(250u, 0x100000010ULL) != 10u ||
      byte_divide(-100, 0x100000003LL) != 0 ||
      byte_remainder(-100, 0x100000003LL) != -100 ||
      halfword_add(65530u, 0x10000000aULL) != 4u ||
      halfword_subtract(-30000, 5LL) != -30005 ||
      halfword_multiply(30000u, 0x100000003ULL) != 24464u ||
      long_destination(0xfffffffeul, 0x100000004ULL) != 2ul) return 4;
  if (mixed_signedness(-5, 8ULL) != 3 ||
      enum_add(lane_negative, 2LL) != 1) return 5;
  if (one_evaluation(&selected, 0x10000000aULL) != 4u ||
      selected != 4u || address_calls != 1u || right_calls != 1u) return 6;
  if ((left += (right += 0x100000010ULL)) != 4u || left != 4u || right != 10u) return 7;
  if (volatile_byte(&bytes, 0x100000010ULL) != 10u || bytes.value != 10u ||
      bytes.before != 0x55u || bytes.after != 0xaau) return 8;
  if ((observed /= 0x100000003ULL) != 0u || observed != 0u) return 9;
  if (field_add(&fields, 0x100000005ULL) != 3u || fields.value != 3u ||
      fields.keep != 85u || fields.signed_value != -100 || fields.pad != 10u) return 10;
  fields.value = 500u;
  if (field_divide(&fields, 0x100000003ULL) != 0u || fields.value != 0u ||
      field_remainder(&fields, -0x100000003LL) != -100 ||
      fields.keep != 85u || fields.pad != 10u) return 11;
  if (volatile_field(&whole, 0x100000001ULL) != 0x12345679u ||
      whole.value != 0x12345679u) return 12;
  return 0;
}
