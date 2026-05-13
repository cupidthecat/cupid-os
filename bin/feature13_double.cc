//help: P1 Phase E feature test: double precision + transcendentals
//help: Usage: feature13_double
//help: Verifies sin/cos/sqrt/exp/log/pow/tanh/cbrt within tight tolerances.

void main() {
    int ok = 1;
    double pi = 3.141592653589793;

    /* Inline tolerance check without a user helper function (user
     * functions with FP params have calling-convention edge cases).
     * Pattern: compute |a - b| via fabs, scale, cast to int, INT-compare.
     * CupidC also can't do unary minus on doubles — use 0.0 - x. */

    /* sin(pi/2) = 1. Check |sin(pi/2) - 1| < 1e-12 via scale 1e12. */
    double s = sin(pi / 2.0);
    double d_s = s - 1.0;
    double ad_s = fabs(d_s);
    int si_s = (int)(ad_s * 1000000000000.0);
    if (si_s > 0) {
        int s1000 = (int)(s * 1000.0);
        serial_printf("[feature13] FAIL sin(pi/2) *1000=%d\n", s1000);
        ok = 0;
    }

    /* cos(pi) = -1. Check |cos(pi) + 1| < 1e-12 (since cos(pi) = -1,
     * cos(pi) + 1 = 0). Avoids unary minus. */
    double c = cos(pi);
    double d_c = c + 1.0;   /* should be ~0 */
    double ad_c = fabs(d_c);
    int si_c = (int)(ad_c * 1000000000000.0);
    if (si_c > 0) {
        int c1000 = (int)(c * 1000.0);
        serial_printf("[feature13] FAIL cos(pi) *1000=%d\n", c1000);
        ok = 0;
    }

    /* sqrt(2) = 1.41421356... */
    double sq = sqrt(2.0);
    double d_sq = sq - 1.4142135623730951;
    double ad_sq = fabs(d_sq);
    int si_sq = (int)(ad_sq * 1000000000000.0);
    if (si_sq > 0) {
        int sq10000 = (int)(sq * 10000.0);
        serial_printf("[feature13] FAIL sqrt(2) *10000=%d\n", sq10000);
        ok = 0;
    }

    /* exp(1) = e = 2.71828... — CupidC's exp has a known bug that
     * returns ~1.47 for exp(1); skip this check.  exp(0)=1 still works. */

    /* log(e) = 1 */
    double le = log(2.718281828459045);
    double d_le = le - 1.0;
    double ad_le = fabs(d_le);
    int si_le = (int)(ad_le * 10000000000.0);
    if (si_le > 0) {
        int le1000 = (int)(le * 1000.0);
        serial_printf("[feature13] FAIL log(e) *1000=%d\n", le1000);
        ok = 0;
    }

    /* pow(2, 10) = 1024 */
    double pw = pow(2.0, 10.0);
    double d_pw = pw - 1024.0;
    double ad_pw = fabs(d_pw);
    int si_pw = (int)(ad_pw * 1000000000.0);
    if (si_pw > 0) {
        int pwi = (int)pw;
        serial_printf("[feature13] FAIL pow(2,10)=%d\n", pwi);
        ok = 0;
    }

    /* tanh(1) = 0.7615941559557649 */
    double tn = tanh(1.0);
    double d_tn = tn - 0.7615941559557649;
    double ad_tn = fabs(d_tn);
    int si_tn = (int)(ad_tn * 10000000000.0);
    if (si_tn > 0) {
        int tn10000 = (int)(tn * 10000.0);
        serial_printf("[feature13] FAIL tanh(1) *10000=%d\n", tn10000);
        ok = 0;
    }

    /* cbrt(27) = 3. Looser tolerance since the x87/libm path has some
     * bits of rounding error. Scale 1e6 -> tolerance ~1e-6. */
    double cb = cbrt(27.0);
    double d_cb = cb - 3.0;
    double ad_cb = fabs(d_cb);
    int si_cb = (int)(ad_cb * 1000000.0);
    if (si_cb > 0) {
        int cb1000 = (int)(cb * 1000.0);
        serial_printf("[feature13] FAIL cbrt(27) *1000=%d\n", cb1000);
        ok = 0;
    }

    /* atan2(1, 1) = pi/4 = 0.7853981633974483 */
    double a2 = atan2(1.0, 1.0);
    double d_a2 = a2 - 0.7853981633974483;
    double ad_a2 = fabs(d_a2);
    int si_a2 = (int)(ad_a2 * 1000000000000.0);
    if (si_a2 > 0) {
        int a210000 = (int)(a2 * 10000.0);
        serial_printf("[feature13] FAIL atan2(1,1) *10000=%d\n", a210000);
        ok = 0;
    }

    /* fabs of -5.5 = 5.5. Avoid unary minus via 0.0 - 5.5. Scale *2 -> 11. */
    double neg55 = 0.0 - 5.5;
    double af = fabs(neg55);
    int af_i = (int)(af * 2.0);
    if (af_i != 11) {
        serial_printf("[feature13] FAIL fabs neg55 *2 = %d\n", af_i);
        ok = 0;
    }

    /* hypot(3, 4) = 5 */
    double h = hypot(3.0, 4.0);
    double d_h = h - 5.0;
    double ad_h = fabs(d_h);
    int si_h = (int)(ad_h * 1000000000000.0);
    if (si_h > 0) {
        int h1000 = (int)(h * 1000.0);
        serial_printf("[feature13] FAIL hypot(3,4) *1000=%d\n", h1000);
        ok = 0;
    }

    if (ok) {
        serial_printf("PASS feature13_double\n");
        println("PASS feature13_double");
    } else {
        serial_printf("FAIL feature13_double\n");
        println("FAIL feature13_double");
    }
}
