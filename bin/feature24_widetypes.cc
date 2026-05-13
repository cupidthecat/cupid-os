//help: Smoke test: extra C keywords + width modifiers parse cleanly
//help: Usage: feature24_widetypes

/* Exercise the lexer + cc_parse_type qualifier-strip for the new
 * tokens added in M1: long, short, signed, U64/I64/uint64_t/int64_t,
 * extern/inline/register/restrict/goto storage classes, and
 * __attribute__ skipping. None of these need 64-bit codegen yet —
 * the test only confirms parsing accepts them. */

extern int unused_extern_var;
inline int square_inline(int x) { return x * x; }
register int __attribute__((unused)) maybe_reg;

void main() {
    int           a = 1;
    long          b = 2;
    long int      c = 3;
    short         d = 4;
    short int     e = 5;
    signed int    f = 6;
    unsigned int  g = 7;

    /* 64-bit aliases: declared but only the low 32 bits are exercised
     * until full 64-bit codegen lands. */
    long long      h = 8;
    long long int  i = 9;
    int64_t        j = 10;
    uint64_t       k = 11;
    I64            m = 12;
    U64            n = 13;

    int total = a + b + c + d + e + f + g + h + i + j + k + m + n;
    print_int(total);
    print("\n");

    /* goto + label: parser must accept them. */
    int loop = 0;
again:
    loop = loop + 1;
    if (loop < 3) goto again;
    print_int(loop);
    print("\n");
}
