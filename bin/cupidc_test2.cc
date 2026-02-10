//help: CupidC critical feature test #2
//help: Usage: cupidc_test2
//help: Verifies sizeof(struct), sizeof(*ptr), object macros, include, and hex literals.

#ifndef CUPIDC_TEST2_DEFS
#define CUPIDC_TEST2_DEFS
#define T2_EXPECT_STRUCT_SIZE 12
#define T2_HEX_VALUE 0xFFU

struct t2_item {
    char a;
    int b;
    char c;
};
#endif

#ifndef CUPIDC_TEST2_MAIN
#define CUPIDC_TEST2_MAIN
#include "cupidc_test2.cc"

void main() {
    int ok = 1;
    int x = 0;
    int *xp;
    struct t2_item item;
    struct t2_item *ip;

    xp = &x;
    ip = &item;

    int sz_struct = sizeof(struct t2_item);
    int sz_deref_struct = sizeof(*ip);
    int sz_deref_int = sizeof(*xp);
    int hv = T2_HEX_VALUE;

    if (sz_struct != T2_EXPECT_STRUCT_SIZE) ok = 0;
    if (sz_deref_struct != T2_EXPECT_STRUCT_SIZE) ok = 0;
    if (sz_deref_int != 4) ok = 0;
    if (hv != 255) ok = 0;

    serial_printf("[cupidc_test2] sizeof(struct)=%u sizeof(*ip)=%u sizeof(*xp)=%u hv=0x%x ok=%u\n",
                  sz_struct, sz_deref_struct, sz_deref_int, hv, ok);

    if (ok) {
        println("cupidc_test2: PASS");
    } else {
        println("cupidc_test2: FAIL");
    }
}

#endif
