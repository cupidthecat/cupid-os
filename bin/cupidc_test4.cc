//help: CupidC medium feature test #4
//help: Usage: cupidc_test4
//help: Verifies sizeof(*ptr), const-in-params parsing, and char** support.

struct t4_pair {
    char c;
    int v;
};

int t4_first_const(const char *s) {
    return s[0];
}

int t4_first_const_alt(char const *s) {
    return s[0];
}

void main() {
    int ok = 1;

    int x = 0;
    int *xp = &x;
    if (sizeof(*xp) != 4) {
        serial_printf("[cupidc_test4] FAIL: sizeof(*xp)=%u expected=4\n", sizeof(*xp));
        ok = 0;
    }

    struct t4_pair p;
    struct t4_pair *pp = &p;
    if (sizeof(*pp) != 8) {
        serial_printf("[cupidc_test4] FAIL: sizeof(*pp)=%u expected=8\n", sizeof(*pp));
        ok = 0;
    }

    int c1 = t4_first_const("alpha");
    int c2 = t4_first_const_alt("beta");
    if (c1 != 'a') {
        serial_printf("[cupidc_test4] FAIL: c1=%u expected=%u\n", c1, 'a');
        ok = 0;
    }
    if (c2 != 'b') {
        serial_printf("[cupidc_test4] FAIL: c2=%u expected=%u\n", c2, 'b');
        ok = 0;
    }

    char *line = "ok";
    char **argv = &line;
    char *tmp = *argv;
    if (tmp != line) {
        serial_printf("[cupidc_test4] FAIL: char** roundtrip mismatch\n");
        ok = 0;
    }

    serial_printf("[cupidc_test4] s_int=%u s_struct=%u c1=%u c2=%u ok=%u\n",
                  sizeof(*xp), sizeof(*pp), c1, c2, ok);

    if (ok) {
        println("cupidc_test4: PASS");
    } else {
        println("cupidc_test4: FAIL");
    }
}
