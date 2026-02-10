//help: CupidC high-priority feature test #3
//help: Usage: cupidc_test3
//help: Verifies arr[pos++], 2D arrays, bitwise compound ops, typedef unsigned, struct forward tags, and void* call-through.

typedef unsigned int u32;

struct node {
    int value;
    struct node *next;
};

struct pair {
    int a;
    int b;
};

int add2(int x, int y) {
    return x + y;
}

void make_pair(int a, int b, struct pair *out) {
    out->a = a;
    out->b = b;
}

void main() {
    int ok = 1;

    int arr[4];
    int pos = 0;
    arr[pos++] = 11;
    arr[pos++] = 22;
    if (pos != 2) { serial_printf("[cupidc_test3] FAIL: pos=%u expected=2\n", pos); ok = 0; }
    if (arr[0] != 11) { serial_printf("[cupidc_test3] FAIL: arr[0]=%u expected=11\n", arr[0]); ok = 0; }
    if (arr[1] != 22) { serial_printf("[cupidc_test3] FAIL: arr[1]=%u expected=22\n", arr[1]); ok = 0; }

    int grid[2][3];
    grid[1][2] = 77;
    if (grid[1][2] != 77) { serial_printf("[cupidc_test3] FAIL: grid[1][2]=%u expected=77\n", grid[1][2]); ok = 0; }

    int flags = 0x0F;
    flags |= 0x10;
    flags &= 0x1F;
    flags ^= 0x03;
    flags <<= 1;
    flags >>= 2;
    if (flags != 14) { serial_printf("[cupidc_test3] FAIL: flags=%u expected=14\n", flags); ok = 0; }

    u32 hv = 0xABU;
    if (hv != 171) { serial_printf("[cupidc_test3] FAIL: hv=%u expected=171\n", hv); ok = 0; }

    struct node n1;
    struct node n2;
    n2.value = 9;
    n1.next = &n2;
    if (n1.next->value != 9) {
        serial_printf("[cupidc_test3] FAIL: n1.next->value=%u expected=9\n", n1.next->value);
        ok = 0;
    }

    struct pair p;
    make_pair(3, 4, &p);
    if (p.a != 3) { serial_printf("[cupidc_test3] FAIL: p.a=%u expected=3\n", p.a); ok = 0; }
    if (p.b != 4) { serial_printf("[cupidc_test3] FAIL: p.b=%u expected=4\n", p.b); ok = 0; }

    void *fn;
    fn = add2;
    int sum = fn(7, 5);
    if (sum != 12) { serial_printf("[cupidc_test3] FAIL: sum=%u expected=12\n", sum); ok = 0; }

    serial_printf("[cupidc_test3] pos=%u grid=%u flags=%u hv=0x%x sum=%u ok=%u\n",
                  pos, grid[1][2], flags, hv, sum, ok);

    if (ok) {
        println("cupidc_test3: PASS");
    } else {
        println("cupidc_test3: FAIL");
    }
}
