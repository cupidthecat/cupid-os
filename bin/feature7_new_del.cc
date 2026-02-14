//help: CupidC Feature 7 demo — new / del keywords
//help: Usage: feature7_new_del
//help: Verifies new T, new T[n], zero-init, and del pointer reset behavior.

class Pair {
    I32 a;
    I32 b;
};

void main() {
    Pair *p = new Pair;
    U8 *buf = new U8[8];

    I32 ok = 1;

    if (!p || !buf) ok = 0;

    if (p->a != 0 || p->b != 0) ok = 0;

    p->a = 20;
    p->b = 22;

    print("pair=");
    print_int((U32)(p->a + p->b));
    print("\n");

    del p;
    del buf;

    if (p != NULL || buf != NULL) ok = 0;

    if (ok) {
        println("feature7_new_del: PASS");
    } else {
        println("feature7_new_del: FAIL");
    }
}
