typedef unsigned int u32;
typedef unsigned long long u64;
static u32 shared_value;

int signed_parameter(int value) { return value; }
u32 unsigned_parameter(u32 value) { return value; }
u32 volatile_parameter(volatile u32 value) { return value; }
int narrow_signed(signed char value) { return value; }
u32 narrow_unsigned(unsigned char value) { return value; }
u64 wide_parameter(u64 value) { return value ^ 0x123456789abcdef0ull; }
u32 pointer_parameter(const u32 *value) { return *value; }

u32 local_alias(u32 value) {
  u32 local = value;
  u32 *alias = &local;
  *alias += 5u;
  return local + *alias;
}

u32 large_frame(u32 value) {
  u32 storage[64];
  u32 tail = value;
  storage[0] = tail;
  storage[63] = value ^ 0xabcdef01u;
  return tail + storage[0] + storage[63];
}

u32 global_load(u32 value) { shared_value = value; return shared_value; }

u32 control_flow(u32 value) {
  u32 local = value & 31u;
  goto start;
again:
  local += 7u;
start:
  if (local < 50u) goto again;
  return local;
}

int main(void) {
  static const u32 values[] = {0u, 1u, 0x80000000u, 0xffffffffu,
                              0x12345678u, 0xabcdef01u};
  unsigned int index;
  for (index = 0u; index < sizeof(values) / sizeof(values[0]); index++) {
    u32 value = values[index];
    u32 expected = value & 31u;
    while (expected < 50u) expected += 7u;
    if (unsigned_parameter(value) != value ||
        volatile_parameter(value) != value ||
        pointer_parameter(&value) != value ||
        local_alias(value) != (value + 5u) * 2u ||
        large_frame(value) != value * 2u + (value ^ 0xabcdef01u) ||
        global_load(value) != value || control_flow(value) != expected)
      return 1;
  }
  if (signed_parameter(-2147483647) != -2147483647 ||
      narrow_signed((signed char)0x80) != -128 ||
      narrow_unsigned((unsigned char)0xff) != 255u ||
      wide_parameter(0xabcdef0134567890ull) !=
          (0xabcdef0134567890ull ^ 0x123456789abcdef0ull)) return 2;
  return 0;
}
