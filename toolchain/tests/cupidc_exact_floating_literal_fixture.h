#ifndef CUPID_TOOLCHAIN_TESTS_CUPIDC_EXACT_FLOATING_LITERAL_FIXTURE_H
#define CUPID_TOOLCHAIN_TESTS_CUPIDC_EXACT_FLOATING_LITERAL_FIXTURE_H

static const char cupidc_exact_floating_literal_source[] =
    "static const float exact_decimal_float[9] = {\n"
    "  1.000000059604644775390625f,\n"
    "  1.000000178813934326171875f,\n"
    "  1e-45f,\n"
    "  1.17549435082228750796873653722224568e-38f,\n"
    "  3.40282346638528859811704183484516925440e38f,\n"
    "  1e9999f,\n"
    "  1e-9999f,\n"
    "  -1e-9999f,\n"
    "  0e9999f};\n"
    "static const double exact_decimal_double[10] = {\n"
    "  1.00000000000000011102230246251565404236316680908203125,\n"
    "  1.00000000000000033306690738754696212708950042724609375,\n"
    "  5e-324,\n"
    "  2.2250738585072013830902327173324040642192159804623318305533274168872e-308,\n"
    "  1.7976931348623157e308,\n"
    "  1e9999,\n"
    "  1e-9999,\n"
    "  -1e-9999,\n"
    "  0e9999,\n"
    "  1.000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000};\n"
    "static const long double exact_decimal_long_double = 1.0L;\n"
    "static const float exact_hex_float[12] = {\n"
    "  0x1p0f,\n"
    "  0X1.000001P0F,\n"
    "  0x1.000003p0f,\n"
    "  0x1p-149f,\n"
    "  0x1p-150f,\n"
    "  0x1.8p-150f,\n"
    "  0x1p-126f,\n"
    "  0x1.fffffep127f,\n"
    "  0x1.ffffffp127f,\n"
    "  0x1p+9999f,\n"
    "  0x1p-9999f,\n"
    "  -0X0P+9999F};\n"
    "static const double exact_hex_double[12] = {\n"
    "  0x1p0,\n"
    "  0X1.00000000000008P0,\n"
    "  0x1.00000000000018p0,\n"
    "  0x1p-1074,\n"
    "  0x1p-1075,\n"
    "  0x1.8p-1075,\n"
    "  0x1p-1022,\n"
    "  0x1.fffffffffffffp1023,\n"
    "  0x1.fffffffffffff8p1023,\n"
    "  0x1p+9999,\n"
    "  0x1p-9999,\n"
    "  -0x0p+9999};\n"
    "static const long double exact_hex_long_double[12] = {\n"
    "  0x1p0L,\n"
    "  0X1.0000000000000001P0l,\n"
    "  0x1.0000000000000003p0L,\n"
    "  0x1p-16445L,\n"
    "  0x1p-16446L,\n"
    "  0x1.8p-16446L,\n"
    "  0x1p-16382L,\n"
    "  0x1.fffffffffffffffep16383L,\n"
    "  0x1.ffffffffffffffffp16383L,\n"
    "  0x1p+99999L,\n"
    "  0x1p-99999L,\n"
    "  -0x0p+99999L};\n"
    "float exact_hex_runtime_float(void) { return 0x1.000003p0f; }\n"
    "double exact_hex_runtime_double(void) { return 0x1p-1074; }\n"
    "long double exact_hex_runtime_long_double(void) { "
    "return 0x1p-16445L; }\n";

static const ctool_u64 cupidc_exact_decimal_float_bits[] = {
    0x3f800000ull, 0x3f800002ull, 0x00000001ull,
    0x00800000ull, 0x7f7fffffull, 0x7f800000ull,
    0x00000000ull, 0x80000000ull, 0x00000000ull};

static const ctool_u64 cupidc_exact_decimal_double_bits[] = {
    0x3ff0000000000000ull, 0x3ff0000000000002ull,
    0x0000000000000001ull, 0x0010000000000000ull,
    0x7fefffffffffffffull, 0x7ff0000000000000ull,
    0x0000000000000000ull, 0x8000000000000000ull,
    0x0000000000000000ull, 0x3ff0000000000000ull};

static const ctool_u64 cupidc_exact_hex_float_bits[] = {
    0x3f800000ull, 0x3f800000ull, 0x3f800002ull,
    0x00000001ull, 0x00000000ull, 0x00000001ull,
    0x00800000ull, 0x7f7fffffull, 0x7f800000ull,
    0x7f800000ull, 0x00000000ull, 0x80000000ull};

static const ctool_u64 cupidc_exact_hex_double_bits[] = {
    0x3ff0000000000000ull, 0x3ff0000000000000ull,
    0x3ff0000000000002ull, 0x0000000000000001ull,
    0x0000000000000000ull, 0x0000000000000001ull,
    0x0010000000000000ull, 0x7fefffffffffffffull,
    0x7ff0000000000000ull, 0x7ff0000000000000ull,
    0x0000000000000000ull, 0x8000000000000000ull};

typedef struct {
  ctool_u64 significand;
  ctool_u32 high_bits;
} cupidc_exact_hex_long_double_bits_t;

static const cupidc_exact_hex_long_double_bits_t
    cupidc_exact_hex_long_double_bits[] = {
        {0x8000000000000000ull, 0x3fffu},
        {0x8000000000000000ull, 0x3fffu},
        {0x8000000000000002ull, 0x3fffu},
        {0x0000000000000001ull, 0x0000u},
        {0x0000000000000000ull, 0x0000u},
        {0x0000000000000001ull, 0x0000u},
        {0x8000000000000000ull, 0x0001u},
        {0xffffffffffffffffull, 0x7ffeu},
        {0x8000000000000000ull, 0x7fffu},
        {0x8000000000000000ull, 0x7fffu},
        {0x0000000000000000ull, 0x0000u},
        {0x0000000000000000ull, 0x8000u}};

#endif
