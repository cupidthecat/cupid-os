/* Checked by the Cupid-owned i386 toolchain closure. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CUPID_LINUX_SYS_BRK 45
#define CUPID_RUNTIME_UINT_MAX 4294967295u

int cupid_linux_syscall1(int number, unsigned int first);
int runtime_contract_run(int argc, char **argv);

static int allocator_contract(void) {
  unsigned char *first;
  unsigned char *middle;
  unsigned char *last;
  unsigned char *reused;
  unsigned char *expanded;
  unsigned char *zeroed;
  void *warmup;
  void *zero_size;
  void *reallocated;
  void *overflow;
  unsigned int baseline;
  unsigned int first_address;
  unsigned int grown;
  unsigned int released;
  size_t index;

  warmup = malloc(1u);
  if (warmup == (void *)0) {
    return 101;
  }
  free(warmup);
  baseline =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);

  warmup = malloc(4096u);
  if (warmup == (void *)0) {
    return 102;
  }
  grown = (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (grown <= baseline) {
    free(warmup);
    return 103;
  }
  free(warmup);
  released =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (released != baseline) {
    return 104;
  }

  first = (unsigned char *)malloc(64u);
  middle = (unsigned char *)malloc(64u);
  last = (unsigned char *)malloc(64u);
  if (first == (unsigned char *)0 ||
      middle == (unsigned char *)0 ||
      last == (unsigned char *)0) {
    free(first);
    free(middle);
    free(last);
    return 105;
  }
  for (index = 0u; index < 64u; index++) {
    middle[index] = (unsigned char)(index + 1u);
  }
  first_address = (unsigned int)first;
  free(first);
  free(last);
  reused = (unsigned char *)malloc(32u);
  if ((unsigned int)reused != first_address) {
    free(reused);
    free(middle);
    return 106;
  }
  expanded = (unsigned char *)realloc(middle, 160u);
  if (expanded == (unsigned char *)0) {
    free(reused);
    free(middle);
    return 107;
  }
  for (index = 0u; index < 64u; index++) {
    if (expanded[index] != (unsigned char)(index + 1u)) {
      free(reused);
      free(expanded);
      return 108;
    }
  }
  free(reused);
  free(expanded);
  released =
      (unsigned int)cupid_linux_syscall1(CUPID_LINUX_SYS_BRK, 0u);
  if (released != baseline) {
    return 109;
  }

  zeroed = (unsigned char *)calloc(8u, 4u);
  if (zeroed == (unsigned char *)0) {
    return 110;
  }
  for (index = 0u; index < 32u; index++) {
    if (zeroed[index] != 0u) {
      free(zeroed);
      return 111;
    }
  }
  free(zeroed);

  errno = 0;
  overflow = calloc(CUPID_RUNTIME_UINT_MAX, 2u);
  if (overflow != (void *)0 || errno != ENOMEM) {
    free(overflow);
    return 112;
  }
  zero_size = malloc(0u);
  if (zero_size == (void *)0) {
    return 113;
  }
  free(zero_size);
  reallocated = realloc((void *)0, 24u);
  if (reallocated == (void *)0) {
    return 114;
  }
  zero_size = realloc(reallocated, 0u);
  if (zero_size != (void *)0) {
    free(zero_size);
    return 115;
  }
  free((void *)0);
  return 0;
}

static int string_contract(void) {
  static const char haystack[] = "Cupid toolchain";
  unsigned char source[12];
  unsigned char destination[12];
  char overlap[16] = "0123456789";
  size_t index;

  (void)memset(source, 0x5a, sizeof(source));
  source[11] = 0u;
  (void)memset(destination, 0, sizeof(destination));
  (void)memcpy(destination, source, sizeof(source));
  if (memcmp(destination, source, sizeof(source)) != 0 ||
      memcmp(destination, source, 0u) != 0) {
    return 201;
  }
  for (index = 0u; index < sizeof(source); index++) {
    if (destination[index] != source[index]) {
      return 202;
    }
  }
  source[4] = (unsigned char)'Y';
  if (memcmp(source, destination, sizeof(source)) >= 0 ||
      memcmp(destination, source, sizeof(source)) <= 0) {
    return 203;
  }
  source[4] = (unsigned char)'Z';
  if (strlen((const char *)destination) != 11u ||
      strcmp((const char *)destination, "ZZZZZZZZZZZ") != 0 ||
      strncmp((const char *)destination, "ZZZZ-other", 4u) != 0 ||
      strchr((const char *)destination, 'Z') !=
          (char *)destination ||
      strchr((const char *)destination, 'x') != (char *)0 ||
      strchr((const char *)destination, '\0') !=
          (char *)(destination + 11u) ||
      strstr(haystack, "tool") != haystack + 6 ||
      strstr(haystack, "") != haystack ||
      strstr(haystack, "host") != (char *)0) {
    return 204;
  }
  (void)memmove(overlap + 2, overlap, 8u);
  if (strcmp(overlap, "0101234567") != 0) {
    return 205;
  }
  (void)memmove(overlap, overlap + 2, 8u);
  overlap[8] = '\0';
  if (strcmp(overlap, "01234567") != 0) {
    return 206;
  }
  return 0;
}

static int file_contract(const char *output_path,
                         const char *missing_path) {
  static const char expected[] = "ok -12 0000002A\n";
  char contents[32];
  char extra;
  FILE *stream;
  size_t expected_size = strlen(expected);
  size_t index;

  stream = fopen(output_path, "wb");
  if (stream == (FILE *)0) {
    return 301;
  }
  if (fprintf(stream, "ok %d %08X\n", -12, 42u) !=
          (int)expected_size ||
      fflush(stream) != 0 ||
      ftell(stream) != (long)expected_size) {
    (void)fclose(stream);
    return 302;
  }
  if (fclose(stream) != 0) {
    return 302;
  }

  stream = fopen(output_path, "rb");
  if (stream == (FILE *)0) {
    return 303;
  }
  if (fseek(stream, 0L, SEEK_END) != 0 ||
      ftell(stream) != (long)expected_size ||
      fseek(stream, 0L, 0) != 0 ||
      fread(contents, 1u, expected_size, stream) != expected_size) {
    (void)fclose(stream);
    return 304;
  }
  for (index = 0u; index < expected_size; index++) {
    if (contents[index] != expected[index]) {
      (void)fclose(stream);
      return 304;
    }
  }
  if (fread(&extra, 1u, 1u, stream) != 0u ||
      ferror(stream) != 0 || fclose(stream) != 0) {
    return 304;
  }

  errno = 0;
  stream = fopen(output_path, "invalid");
  if (stream != (FILE *)0 || errno != EINVAL) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return 305;
  }
  errno = 0;
  stream = fopen(missing_path, "rb");
  if (stream != (FILE *)0 || errno != ENOENT) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return 306;
  }

  stream = fopen(output_path, "rb");
  if (stream == (FILE *)0) {
    return 307;
  }
  errno = 0;
  if (fread((void *)0, 1u, 1u, stream) != 0u ||
      errno != EINVAL || ferror(stream) == 0) {
    (void)fclose(stream);
    return 308;
  }
  if (fclose(stream) != 0) {
    return 308;
  }

  errno = 0;
  if (fprintf(stderr, "%q") != -1 || errno != EINVAL ||
      fflush((FILE *)0) != 0) {
    return 309;
  }
  return 0;
}

static int directory_contract(void) {
  char directory[512];
  char small[1];

  if (getcwd(directory, sizeof(directory)) != directory ||
      directory[0] == '\0') {
    return 401;
  }
  errno = 0;
  if (getcwd(small, sizeof(small)) != (char *)0 ||
      errno != ERANGE) {
    return 402;
  }
  errno = 0;
  if (getcwd((char *)0, sizeof(directory)) != (char *)0 ||
      errno != EINVAL) {
    return 403;
  }
  return 0;
}

static int integer_contract(void) {
  if (sizeof(int8_t) != 1u || sizeof(uint8_t) != 1u ||
      sizeof(int16_t) != 2u || sizeof(uint16_t) != 2u ||
      sizeof(int32_t) != 4u || sizeof(uint32_t) != 4u ||
      sizeof(int64_t) != 8u || sizeof(uint64_t) != 8u ||
      sizeof(intptr_t) != 4u || sizeof(uintptr_t) != 4u ||
      INT32_MAX != 2147483647 || UINT32_MAX != 4294967295u ||
      UINT64_MAX != UINT64_C(18446744073709551615)) {
    return 501;
  }
  return 0;
}

typedef __builtin_va_list long_double_va_list;
typedef long double (*long_double_result_callback)(long double);
typedef void (*long_double_open_callback)();

typedef union {
  double value;
  struct {
    unsigned int low;
    unsigned int high;
  } words;
} long_double_result_box;

typedef union {
  float value;
  unsigned int bits;
} floating_truth_float_box;

typedef union {
  long double value;
  struct {
    unsigned int significand_low;
    unsigned int significand_high;
    unsigned int sign_exponent_padding;
  } words;
} floating_truth_long_box;

static unsigned int floating_truth_side_effect_count;

static float floating_truth_side_effect(void) {
  floating_truth_side_effect_count++;
  return 1.0f;
}

static int floating_truth_contract(void) {
  floating_truth_float_box narrow_box;
  long_double_result_box wide_box;
  floating_truth_long_box long_box;
  float narrow_zero = 0.0f;
  float narrow_negative_zero = -0.0f;
  float narrow_finite = 1.5f;
  float narrow_subnormal;
  float narrow_infinity;
  float narrow_quiet_nan;
  float narrow_signaling_nan;
  double wide_zero = 0.0;
  double wide_negative_zero = -0.0;
  double wide_finite = 2.5;
  double wide_subnormal;
  double wide_infinity;
  double wide_quiet_nan;
  double wide_signaling_nan;
  long double long_subnormal;
  long double long_infinity;
  unsigned int iterations = 0u;
  unsigned int long_truth_checks;
  _Bool narrow_truth;
  _Bool wide_truth;
  _Bool long_truth;
  int selected;

  narrow_box.bits = 1u;
  narrow_subnormal = narrow_box.value;
  narrow_box.bits = 0x7f800000u;
  narrow_infinity = narrow_box.value;
  narrow_box.bits = 0x7fc00001u;
  narrow_quiet_nan = narrow_box.value;
  narrow_box.bits = 0x7f800001u;
  narrow_signaling_nan = narrow_box.value;
  wide_box.words.low = 1u;
  wide_box.words.high = 0u;
  wide_subnormal = wide_box.value;
  wide_box.words.low = 0u;
  wide_box.words.high = 0x7ff00000u;
  wide_infinity = wide_box.value;
  wide_box.words.low = 1u;
  wide_box.words.high = 0x7ff80000u;
  wide_quiet_nan = wide_box.value;
  wide_box.words.low = 1u;
  wide_box.words.high = 0x7ff00000u;
  wide_signaling_nan = wide_box.value;
  long_box.words.significand_low = 1u;
  long_box.words.significand_high = 0u;
  long_box.words.sign_exponent_padding = 0u;
  long_subnormal = long_box.value;
  long_infinity = (long double)wide_infinity;

  if (narrow_zero || narrow_negative_zero || !narrow_finite ||
      !narrow_subnormal || !narrow_infinity || !narrow_quiet_nan ||
      !narrow_signaling_nan) {
    return 651;
  }
  if (wide_zero || wide_negative_zero || !wide_finite ||
      !wide_subnormal || !wide_infinity || !wide_quiet_nan ||
      !wide_signaling_nan) {
    return 652;
  }
  if ((!narrow_zero) != 1 || (!narrow_negative_zero) != 1 ||
      (!narrow_finite) != 0 || (!narrow_quiet_nan) != 0 ||
      (!wide_zero) != 1 || (!wide_negative_zero) != 1 ||
      (!wide_finite) != 0 || (!wide_quiet_nan) != 0) {
    return 653;
  }
  narrow_truth = narrow_quiet_nan;
  wide_truth = (_Bool)wide_subnormal;
  long_truth = (_Bool)(long double)wide_quiet_nan;
  if ((_Bool)narrow_zero != 0 || (_Bool)narrow_negative_zero != 0 ||
      (_Bool)wide_zero != 0 || (_Bool)wide_negative_zero != 0 ||
      (_Bool)narrow_finite != 1 || (_Bool)narrow_subnormal != 1 ||
      (_Bool)narrow_infinity != 1 || (_Bool)narrow_signaling_nan != 1 ||
      (_Bool)wide_finite != 1 || (_Bool)wide_infinity != 1 ||
      (_Bool)wide_quiet_nan != 1 || (_Bool)wide_signaling_nan != 1 ||
      (_Bool)long_subnormal != 1 || (_Bool)long_infinity != 1 ||
      narrow_truth != 1 || wide_truth != 1 || long_truth != 1) {
    return 659;
  }
  for (long_truth_checks = 0u; long_truth_checks < 32u;
       long_truth_checks++) {
    if (long_subnormal) {
      long_truth = (_Bool)long_subnormal;
    } else {
      return 660;
    }
    if ((!long_subnormal) != 0 || long_truth != 1 ||
        (long_subnormal ? 1 : 0) != 1) {
      return 661;
    }
  }

  floating_truth_side_effect_count = 0u;
  if (narrow_zero && floating_truth_side_effect()) {
    return 654;
  }
  if (!(narrow_finite || floating_truth_side_effect()) ||
      floating_truth_side_effect_count != 0u) {
    return 655;
  }
  if (!(narrow_zero || floating_truth_side_effect()) ||
      !(wide_finite && floating_truth_side_effect()) ||
      floating_truth_side_effect_count != 2u) {
    return 656;
  }

  selected = narrow_quiet_nan ? 7 : 9;
  if (selected != 7) {
    return 657;
  }
  while (narrow_finite) {
    iterations++;
    narrow_finite = narrow_zero;
  }
  for (; wide_finite; wide_finite = wide_zero) {
    iterations++;
  }
  do {
    iterations++;
  } while (wide_zero);
  if (iterations != 3u) {
    return 658;
  }
  return 0;
}

typedef struct {
  long double first;
  unsigned int marker;
  long double second;
} long_double_zero_record;

static unsigned int long_double_capture_low;
static unsigned int long_double_capture_high;
static unsigned int long_double_capture_count;
static long double long_double_file_zero;
static long double long_double_file_explicit_zero =
    sizeof(float) - 4;
static const long double long_double_file_one = (+1.0L);
static long double long_double_file_precise =
    1.0000000000000000001L;
static long double long_double_file_negative_zero = -0.0L;
static long double long_double_file_positive_zero = +0.0L;
static long double long_double_file_array[2];
static long_double_zero_record long_double_file_record = {
    0, 0, sizeof(float) - 4};
static long double long_double_file_initialized_array[2] = {
    +1.0L, -0.0L};
static long_double_zero_record long_double_file_initialized_record = {
    1.0000000000000000001L, 7u, -1.0L};

static int long_double_payload_matches(
    long double value, unsigned int significand_low,
    unsigned int significand_high,
    unsigned int sign_exponent_padding) {
  floating_truth_long_box box;
  box.value = value;
  return box.words.significand_low == significand_low &&
                 box.words.significand_high == significand_high &&
                 box.words.sign_exponent_padding ==
                     sign_exponent_padding
             ? 1
             : 0;
}

static long double long_double_identity(long double value) {
  return value;
}

static void long_double_capture(long double value) {
  long_double_result_box box;
  box.value = (double)value;
  long_double_capture_low = box.words.low;
  long_double_capture_high = box.words.high;
  long_double_capture_count++;
}

static void long_double_open_direct();

static void long_double_open_calls(
    long double value, long_double_open_callback callback) {
  long_double_open_direct(value);
  callback(value);
}

static void long_double_open_direct(long double value) {
  long_double_capture(value);
}

static unsigned int long_double_variadic_tail(int marker, ...) {
  long_double_va_list arguments;
  long double value;
  unsigned int tail;
  (void)marker;
  __builtin_va_start(arguments, marker);
  value = __builtin_va_arg(arguments, long double);
  tail = __builtin_va_arg(arguments, unsigned int);
  __builtin_va_end(arguments);
  long_double_capture(value);
  return tail;
}

static int long_double_contract(void) {
  long_double_result_callback callback = long_double_identity;
  long_double_result_box box;
  floating_truth_long_box literal_box;
  static long double long_double_block_zero;
  static long double long_double_block_explicit_zero = 0;
  static long double long_double_block_maximum =
      18446744073709551615e0L;
  static long double long_double_block_negative_one = -1.0L;
  static long double long_double_block_array[2];
  static long_double_zero_record long_double_block_record = {
      0, 0, sizeof(float) - 4};
  static long double long_double_block_initialized_array[2] = {
      18446744073709551615e0L, +0.0L};
  static long_double_zero_record long_double_block_initialized_record = {
      -0.0L, 9u, +1.0L};
  long double initial = 1.5L;
  long double direct;
  long double indirect;
  long double lower = 1.0L;
  long double higher = 2.0L;
  long double zero = 0.0L;
  long double negative_zero = -0.0L;
  long double precise = 1.0000000000000000001L;
  long double maximum_literal = 18446744073709551615e0L;
  long double quiet_nan;
  unsigned int comparison_index;
  unsigned int tail;

  if (sizeof(long double) != 12u ||
      sizeof(long_double_file_array) != 24u ||
      sizeof(long_double_zero_record) != 28u) {
    return 701;
  }
  literal_box.value = precise;
  if (literal_box.words.significand_low != 1u ||
      literal_box.words.significand_high != 0x80000000u ||
      literal_box.words.sign_exponent_padding != 0x00003fffu ||
      !(precise > lower) || !(precise < higher)) {
    return 721;
  }
  literal_box.value = maximum_literal;
  if (literal_box.words.significand_low != 0xffffffffu ||
      literal_box.words.significand_high != 0xffffffffu ||
      literal_box.words.sign_exponent_padding != 0x0000403eu ||
      !(maximum_literal > higher)) {
    return 722;
  }
  if (!long_double_payload_matches(
          long_double_file_one, 0u, 0x80000000u, 0x00003fffu) ||
      !long_double_payload_matches(
          long_double_file_precise, 1u, 0x80000000u, 0x00003fffu)) {
    return 723;
  }
  if (!long_double_payload_matches(
          long_double_file_negative_zero, 0u, 0u, 0x00008000u) ||
      !long_double_payload_matches(
          long_double_file_positive_zero, 0u, 0u, 0u) ||
      !long_double_payload_matches(
          long_double_block_maximum, 0xffffffffu, 0xffffffffu,
          0x0000403eu) ||
      !long_double_payload_matches(
          long_double_block_negative_one, 0u, 0x80000000u,
          0x0000bfffu)) {
    return 724;
  }
  if (!long_double_payload_matches(
          long_double_file_initialized_array[0], 0u,
          0x80000000u, 0x00003fffu) ||
      !long_double_payload_matches(
          long_double_file_initialized_array[1], 0u, 0u,
          0x00008000u) ||
      !long_double_payload_matches(
          long_double_file_initialized_record.first, 1u,
          0x80000000u, 0x00003fffu) ||
      long_double_file_initialized_record.marker != 7u ||
      !long_double_payload_matches(
          long_double_file_initialized_record.second, 0u,
          0x80000000u, 0x0000bfffu)) {
    return 725;
  }
  if (!long_double_payload_matches(
          long_double_block_initialized_array[0], 0xffffffffu,
          0xffffffffu, 0x0000403eu) ||
      !long_double_payload_matches(
          long_double_block_initialized_array[1], 0u, 0u, 0u) ||
      !long_double_payload_matches(
          long_double_block_initialized_record.first, 0u, 0u,
          0x00008000u) ||
      long_double_block_initialized_record.marker != 9u ||
      !long_double_payload_matches(
          long_double_block_initialized_record.second, 0u,
          0x80000000u, 0x00003fffu)) {
    return 726;
  }
  box.value =
      (double)(long_double_file_array[0] +
               long_double_file_array[1] +
               long_double_file_record.first +
               long_double_file_record.second +
               long_double_block_array[0] +
               long_double_block_array[1] +
               long_double_block_record.first +
               long_double_block_record.second);
  if (box.words.low != 0u || box.words.high != 0u ||
      long_double_file_record.marker != 0u ||
      long_double_block_record.marker != 0u) {
    return 707;
  }
  long_double_file_array[1] = initial;
  long_double_block_array[0] = long_double_file_array[1];
  long_double_file_record.second = long_double_block_array[0];
  long_double_block_record.first = long_double_file_record.second;
  box.value = (double)long_double_block_record.first;
  if (box.words.low != 0u || box.words.high != 0x3ff80000u) {
    return 708;
  }
  long_double_block_zero = long_double_file_zero;
  long_double_block_explicit_zero =
      long_double_file_explicit_zero;
  box.value =
      (double)(long_double_block_zero +
               long_double_block_explicit_zero);
  if (box.words.low != 0u || box.words.high != 0u) {
    return 705;
  }
  long_double_file_zero = initial;
  long_double_block_zero = long_double_file_zero;
  long_double_file_explicit_zero = long_double_block_zero;
  long_double_block_explicit_zero =
      long_double_file_explicit_zero;
  box.value = (double)long_double_block_explicit_zero;
  if (box.words.low != 0u || box.words.high != 0x3ff80000u) {
    return 706;
  }
  direct = long_double_identity(initial);
  indirect = callback(direct);
  box.value = (double)indirect;
  if (box.words.low != 0u || box.words.high != 0x3ff80000u) {
    return 702;
  }

  long_double_capture_low = 0u;
  long_double_capture_high = 0u;
  long_double_capture_count = 0u;
  long_double_open_calls(
      indirect, (long_double_open_callback)long_double_capture);
  if (long_double_capture_count != 2u ||
      long_double_capture_low != 0u ||
      long_double_capture_high != 0x3ff80000u) {
    return 703;
  }

  tail = long_double_variadic_tail(
      7, indirect, 0xa5c39e71u);
  if (tail != 0xa5c39e71u || long_double_capture_count != 3u ||
      long_double_capture_low != 0u ||
      long_double_capture_high != 0x3ff80000u) {
    return 704;
  }

  if (lower == higher || !(lower != higher) || !(lower < higher) ||
      !(lower <= higher) || lower > higher || lower >= higher) {
    return 709;
  }
  if (higher == lower || !(higher != lower) || higher < lower ||
      higher <= lower || !(higher > lower) || !(higher >= lower)) {
    return 710;
  }
  if (!(lower == lower) || lower != lower || lower < lower ||
      !(lower <= lower) || lower > lower || !(lower >= lower)) {
    return 711;
  }
  if (!(zero == negative_zero) || zero != negative_zero ||
      zero < negative_zero || !(zero <= negative_zero) ||
      zero > negative_zero || !(zero >= negative_zero)) {
    return 712;
  }
  if (!(lower == 1.0) || !(1.0 == lower) ||
      !(lower == 1.0f) || !(1.0f == lower) ||
      !(lower < 2.0) || !(1.0 < higher) ||
      !(lower < 2.0f) || !(1.0f < higher)) {
    return 716;
  }
  box.words.low = 1u;
  box.words.high = 0x7ff80000u;
  quiet_nan = (long double)box.value;
  if (zero || negative_zero || !lower || !higher || !quiet_nan ||
      (!zero) != 1 || (!negative_zero) != 1 || (!lower) != 0 ||
      (!quiet_nan) != 0) {
    return 717;
  }
  floating_truth_side_effect_count = 0u;
  if (zero && floating_truth_side_effect()) {
    return 718;
  }
  if (!(quiet_nan || floating_truth_side_effect()) ||
      floating_truth_side_effect_count != 0u) {
    return 719;
  }
  tail = quiet_nan ? 0xa5c39e71u : 0u;
  if (tail != 0xa5c39e71u) {
    return 720;
  }
  if (quiet_nan == lower || !(quiet_nan != lower) || quiet_nan < lower ||
      quiet_nan <= lower || quiet_nan > lower || quiet_nan >= lower) {
    return 713;
  }
  if (lower == quiet_nan || !(lower != quiet_nan) || lower < quiet_nan ||
      lower <= quiet_nan || lower > quiet_nan || lower >= quiet_nan) {
    return 714;
  }
  for (comparison_index = 0u; comparison_index < 32u;
       comparison_index++) {
    if (!(lower < higher) || !(higher > lower) ||
        !(lower < 2.0) || !(1.0f < higher) ||
        quiet_nan == lower || !(quiet_nan != lower) ||
        quiet_nan < lower || quiet_nan <= lower ||
        quiet_nan > lower || quiet_nan >= lower ||
        lower == quiet_nan || !(lower != quiet_nan) ||
        lower < quiet_nan || lower <= quiet_nan ||
        lower > quiet_nan || lower >= quiet_nan ||
        (!zero) != 1 || (!lower) != 0 || (!quiet_nan) != 0 ||
        !(lower && quiet_nan) || !(zero || lower)) {
      return 715;
    }
  }
  return 0;
}

static int stdio_contract(void) {
  static const char wide_expected[] =
      "-9223372036854775808|18446744073709551615|"
      "0123456789abcdef|trun";
  char formatted[96];
  char truncated[5];
  int result;

  if (printf("printf-ok %u\n", 7u) != 12) {
    return 601;
  }
  if (puts("puts-ok") < 0) {
    return 602;
  }
  if (fputs("fputs-", stdout) < 0 ||
      fputc('o', stdout) != 'o' ||
      fputc('k', stdout) != 'k' ||
      fputc('\n', stdout) != '\n') {
    return 603;
  }
  errno = 0;
  if (printf("%q") != -1 || errno != EINVAL) {
    return 604;
  }
  result = snprintf(formatted, sizeof(formatted), "%s-%03u-%d",
                    "value", 7u, -2);
  if (result != 12 || strcmp(formatted, "value-007--2") != 0) {
    return 605;
  }
  result = snprintf(truncated, sizeof(truncated), "abcdef");
  if (result != 6 || strcmp(truncated, "abcd") != 0) {
    return 606;
  }
  if (snprintf((char *)0, 0u, "%u", 1234u) != 4) {
    return 607;
  }
  result = snprintf(
      formatted, sizeof(formatted), "%lld|%llu|%016llx|%.*s",
      -9223372036854775807LL - 1LL,
      18446744073709551615ULL, 0x0123456789abcdefULL,
      4, "truncate");
  if (result != (int)strlen(wide_expected) ||
      strcmp(formatted, wide_expected) != 0) {
    return 608;
  }
  result = snprintf(formatted, sizeof(formatted),
                    "[%.*s][%.3s][%.*s]", 0, "hidden",
                    "abcdef", -1, "full");
  if (result != 13 || strcmp(formatted, "[][abc][full]") != 0) {
    return 609;
  }
  errno = 0;
  if (snprintf(formatted, sizeof(formatted), "%q") != -1 ||
      errno != EINVAL || formatted[0] != '\0') {
    return 610;
  }
  errno = 0;
  if (snprintf(formatted, sizeof(formatted), "%lls", "bad") != -1 ||
      errno != EINVAL || formatted[0] != '\0') {
    return 611;
  }
  errno = 0;
  if (fputc('x', (FILE *)0) != EOF || errno != EBADF) {
    return 612;
  }
  errno = 0;
  if (fputs("x", (FILE *)0) != EOF || errno != EBADF) {
    return 613;
  }
  errno = 0;
  if (fprintf((FILE *)0, "%s", "x") != -1 || errno != EBADF) {
    return 614;
  }
  return 0;
}

int runtime_contract_run(int argc, char **argv) {
  int result;
  if (argc != 3 || argv == (char **)0 ||
      argv[0] == (char *)0 || argv[1] == (char *)0 ||
      argv[2] == (char *)0) {
    return 1;
  }
  result = allocator_contract();
  if (result == 0) {
    result = string_contract();
  }
  if (result == 0) {
    result = file_contract(argv[1], argv[2]);
  }
  if (result == 0) {
    result = directory_contract();
  }
  if (result == 0) {
    result = integer_contract();
  }
  if (result == 0) {
    result = floating_truth_contract();
  }
  if (result == 0) {
    result = long_double_contract();
  }
  if (result == 0) {
    result = stdio_contract();
  }
  return result;
}

int main(int argc, char **argv) {
  int result = runtime_contract_run(argc, argv);
  if (result != 0) {
    (void)fprintf(stderr, "runtime-contract: %d\n", result);
    return result;
  }
  (void)fprintf(stdout, "runtime-ok\n");
  return 0;
}
