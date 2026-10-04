//help: Regression test for FP compound assignment
//help: Usage: test_fpaug
//help: Exercises FP compound assignment, comparison, and scalar truth.
//help: Reports PASS/FAIL via serial and console.

int fpaug_equal(double left, double right) {
    return left == right;
}

int fpaug_not_equal(double left, double right) {
    return left != right;
}

int fpaug_truth(double value) {
    return !!value;
}

void main() {
    int ok = 1;

    /* float +=  : 10.0 + 5.5 = 15.5  -> (int)(a*2) = 31 */
    float a = 10.0;
    a += 5.5;
    int a_check = (int)(a * 2.0);
    if (a_check != 31) {
        serial_printf("[test_fpaug] FAIL float +=: got=%d expected=31\n", a_check);
        ok = 0;
    }

    /* float -=  *= */
    float b = 8.0;
    b -= 2.0;
    b *= 0.5;
    int b_check = (int)b;
    if (b_check != 3) {
        serial_printf("[test_fpaug] FAIL float -= *=: got=%d expected=3\n", b_check);
        ok = 0;
    }

    /* double /= */
    double c = 100.0;
    c /= 4.0;
    int c_check = (int)c;
    if (c_check != 25) {
        serial_printf("[test_fpaug] FAIL double /=: got=%d expected=25\n", c_check);
        ok = 0;
    }

    /* float += int (coerce RHS) */
    float d = 1.0;
    d += 2;
    int d_check = (int)d;
    if (d_check != 3) {
        serial_printf("[test_fpaug] FAIL float += int: got=%d expected=3\n", d_check);
        ok = 0;
    }

    /* double sequence: ((100 + 50) - 30) * 2 / 4 = 60 */
    double e = 100.0;
    e += 50.0;
    e -= 30.0;
    e *= 2.0;
    e /= 4.0;
    int e_check = (int)e;
    if (e_check != 60) {
        serial_printf("[test_fpaug] FAIL double seq: got=%d expected=60\n", e_check);
        ok = 0;
    }

    double unordered = 0.0 / 0.0;
    int equal = fpaug_equal(1.0, 1.0);
    int unequal = fpaug_not_equal(unordered, unordered);
    int truth = fpaug_truth(unordered);
    if (equal != 1 || unequal != 1 || truth != 1) {
        serial_printf("[test_fpaug-parity] FAIL equal=%d unequal=%d truth=%d\n",
                      equal, unequal, truth);
        ok = 0;
    } else {
        serial_printf("[test_fpaug-parity] PASS equal=%d unequal=%d truth=%d\n",
                      equal, unequal, truth);
    }

    if (ok) {
        serial_printf("PASS test_fpaug\n");
        println("PASS test_fpaug");
    } else {
        serial_printf("FAIL test_fpaug\n");
        println("FAIL test_fpaug");
    }
}
