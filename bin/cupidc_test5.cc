//help: CupidC low-priority compatibility test #5
//help: Usage: cupidc_test5
//help: Verifies volatile(parse/ignore), static locals, #ifdef, variadic defs, and struct initializer-list parsing.

#define T5_ENABLED 1

#ifdef T5_ENABLED
int t5_ifdef_value = 1;
#else
int t5_ifdef_value = 0;
#endif

struct t5_pair {
    int a;
    int b;
};

struct t5_pair t5_global_pair = {0};

int t5_counter() {
    static int c;
    c += 1;
    return c;
}

int t5_first(int x, ...) {
    return x;
}

void main() {
    int ok = 1;

    volatile int v;
    v = 3;
    if (v != 3) {
        serial_printf("[cupidc_test5] FAIL: volatile value=%u expected=3\n", v);
        ok = 0;
    }

    int c1 = t5_counter();
    int c2 = t5_counter();
    if (c1 != 1 || c2 != 2) {
        serial_printf("[cupidc_test5] FAIL: static local sequence c1=%u c2=%u expected 1,2\n", c1, c2);
        ok = 0;
    }

    if (t5_ifdef_value != 1) {
        serial_printf("[cupidc_test5] FAIL: #ifdef value=%u expected=1\n", t5_ifdef_value);
        ok = 0;
    }

    int vf = t5_first(7, 8, 9);
    if (vf != 7) {
        serial_printf("[cupidc_test5] FAIL: variadic first=%u expected=7\n", vf);
        ok = 0;
    }

    struct t5_pair local_pair = {0};
    if (t5_global_pair.a != 0 || t5_global_pair.b != 0 ||
        local_pair.a != 0 || local_pair.b != 0) {
        serial_printf("[cupidc_test5] FAIL: struct init values g=(%u,%u) l=(%u,%u) expected zeros\n",
                      t5_global_pair.a, t5_global_pair.b, local_pair.a, local_pair.b);
        ok = 0;
    }

    serial_printf("[cupidc_test5] v=%u c1=%u c2=%u ifdef=%u vf=%u ok=%u\n",
                  v, c1, c2, t5_ifdef_value, vf, ok);

    if (ok) {
        println("cupidc_test5: PASS");
    } else {
        println("cupidc_test5: FAIL");
    }
}
