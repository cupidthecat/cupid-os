//help: CupidC critical feature test #1
//help: Usage: cupidc_test1
//help: Verifies #include, #define object macros, serial_printf binding, and hex literals.

#ifndef CUPIDC_TEST1_DEFS
#define CUPIDC_TEST1_DEFS
#define T1_HEX_VALUE 0x2AU
#define T1_STRIDE 4
#endif

#ifndef CUPIDC_TEST1_MAIN
#define CUPIDC_TEST1_MAIN
#include "cupidc_test1.cc"

void main() {
    int ok = 1;
    int hv = T1_HEX_VALUE;
    int stride = T1_STRIDE;

    if (hv != 42) ok = 0;
    if ((stride * 3) != 12) ok = 0;

    serial_printf("[cupidc_test1] hv=0x%x (%u) stride=%u ok=%u\n",
                  hv, hv, stride, ok);

    if (ok) {
        println("cupidc_test1: PASS");
    } else {
        println("cupidc_test1: FAIL");
    }
}

#endif
