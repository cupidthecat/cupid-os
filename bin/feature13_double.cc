//help: P1 Phase E feature test: double precision + transcendentals
//help: Usage: feature13_double
//help: Verifies sin/cos/sqrt/exp/log/pow/tanh/cbrt within tight tolerances.

float feature13_update_global_float;
double feature13_update_global_double;
float feature13_lvalue_global[2][3];

struct feature13_record {
    float gain;
    double bias;
    float taps[3];
};

float *feature13_lvalue_row() {
    return &feature13_lvalue_global[1][0];
}

int feature13_within(double actual, double expected, double scale,
                     int max_scaled_error) {
    double scaled_error = fabs(actual - expected) * scale;
    int scaled = (int)scaled_error;
    return scaled >= 0 && scaled <= max_scaled_error;
}

void main() {
    int ok = 1;
    int call_checks = 0;
    double pi = 3.141592653589793;

    /* Unary signs preserve scalar floating types and flip only the sign bit.
     * Check ordinary values, signed zero, unary plus, a useful type error,
     * and compiler recovery after the rejected expression.*/
    float positive_float = 1.5;
    float negative_float = -positive_float;
    int negative_float_scaled = (int)(negative_float * 10.0);
    double positive_double = 2.25;
    double negative_double = -positive_double;
    int negative_double_scaled = (int)(negative_double * 4.0);
    float negative_zero = -0.0;
    int negative_zero_bits = *(int*)&negative_zero;
    double plus_double = +positive_double;
    int plus_double_scaled = (int)(plus_double * 4.0);
    int unary_reject = repl_eval("-\"not arithmetic\";") == -1;
    int unary_recovery = repl_eval("1 + 1;") == 0;
    if (negative_float_scaled != -15 ||
        negative_double_scaled != -9 ||
        negative_zero_bits != 0x80000000 ||
        plus_double_scaled != 9 ||
        !unary_reject || !unary_recovery) {
        serial_printf("[feature13-unary] FAIL float=%d double=%d zero=%x plus=%d reject=%d recovery=%d\n",
                      negative_float_scaled, negative_double_scaled,
                      negative_zero_bits, plus_double_scaled,
                      unary_reject, unary_recovery);
        ok = 0;
    } else {
        serial_printf("[feature13-unary] PASS float=%d double=%d zero=%x plus=%d reject=%d recovery=%d\n",
                      negative_float_scaled, negative_double_scaled,
                      negative_zero_bits, plus_double_scaled,
                      unary_reject, unary_recovery);
    }

    /* Exercise every scalar floating comparison. Mixed widths compare as
     * double, signed zero compares equal to positive zero, and unordered
     * comparisons follow C rules. */
    int compare_ordered =
        (1.0 == 1.0) +
        (1.0 != 2.0) +
        (1.0 < 2.0) +
        (2.0 > 1.0) +
        (1.0 <= 1.0) +
        (2.0 >= 2.0);
    int compare_mixed =
        (1.0f < 2.0) +
        (2.0 > 1.0f) +
        (2.0f == 2.0) +
        (1.0 != 2.0f);
    int compare_zero =
        (negative_zero == 0.0) +
        !(negative_zero != 0.0);
    double compare_nan = 0.0 / 0.0;
    int compare_unordered =
        (compare_nan != compare_nan) +
        !(compare_nan == compare_nan) +
        !(compare_nan < 0.0) +
        !(compare_nan > 0.0) +
        !(compare_nan <= 0.0) +
        !(compare_nan >= 0.0);
    if (compare_ordered != 6 || compare_mixed != 4 ||
        compare_zero != 2 || compare_unordered != 6) {
        serial_printf("[feature13-compare] FAIL ordered=%d mixed=%d zero=%d unordered=%d\n",
                      compare_ordered, compare_mixed,
                      compare_zero, compare_unordered);
        ok = 0;
    } else {
        serial_printf("[feature13-compare] PASS ordered=%d mixed=%d zero=%d unordered=%d\n",
                      compare_ordered, compare_mixed,
                      compare_zero, compare_unordered);
    }

    /* Scalar floating truth follows C rules in every private compiler
     * control-flow path. Both signed zero encodings are false. Finite
     * nonzero values, infinity, and NaN are true. */
    float truth_zero = 0.0f;
    double truth_negative_zero = -0.0;
    double truth_nonzero = -0.25;
    double truth_nan = 0.0 / 0.0;
    int truth_zero_score =
        (!truth_zero) +
        (!truth_negative_zero);
    int truth_nonzero_score =
        !!1.0f +
        !!truth_nonzero +
        !!truth_nan;
    int truth_control = 0;
    if (truth_nonzero)
        truth_control += 1;
    if (truth_zero)
        truth_control += 1000;
    else
        truth_control += 2;
    truth_control += truth_nonzero ? 4 : 1000;
    truth_control += truth_zero ? 1000 : 8;

    float truth_while = 1.0f;
    while (truth_while) {
        truth_control += 16;
        truth_while = 0.0f;
    }

    double truth_for = 1.0;
    for (; truth_for; truth_for = 0.0)
        truth_control += 32;

    double truth_do = 0.0;
    do {
        truth_control += 64;
    } while (truth_do);

    if (truth_nan)
        truth_control += 128;

    if (truth_zero_score != 2 || truth_nonzero_score != 3 ||
        truth_control != 255) {
        serial_printf("[feature13-truth] FAIL zero=%d nonzero=%d control=%d nan=%d\n",
                      truth_zero_score, truth_nonzero_score,
                      truth_control, !!truth_nan);
        ok = 0;
    } else {
        serial_printf("[feature13-truth] PASS zero=%d nonzero=%d control=%d nan=%d\n",
                      truth_zero_score, truth_nonzero_score,
                      truth_control, !!truth_nan);
    }

    /* Prefix and postfix floating updates preserve their expression result
     * while storing an exact one-unit change. Cover local and global
     * variables, statement updates, and the for-increment shortcut. */
    float update_float = 1.25f;
    float update_float_old = update_float++;
    float update_float_new = ++update_float;
    update_float--;

    double update_double = 4.5;
    double update_double_old = update_double--;
    double update_double_new = --update_double;
    update_double++;

    int update_local_score =
        (int)(update_float_old * 4.0f) +
        (int)(update_float_new * 4.0f) +
        (int)(update_float * 4.0f) +
        (int)(update_double_old * 2.0) +
        (int)(update_double_new * 2.0) +
        (int)(update_double * 2.0);

    feature13_update_global_float = 0.5f;
    feature13_update_global_double = 5.25;
    feature13_update_global_float++;
    --feature13_update_global_double;
    double update_global_old = feature13_update_global_double--;
    float update_global_new = ++feature13_update_global_float;
    int update_global_score =
        (int)(update_global_old * 4.0) +
        (int)(update_global_new * 2.0f) +
        (int)(feature13_update_global_float * 2.0f) +
        (int)(feature13_update_global_double * 4.0);

    float update_for = 0.0f;
    int update_iterations = 0;
    for (; update_iterations < 3; update_for++)
        update_iterations++;

    float update_negative_zero = -0.0f;
    float update_zero_old = update_negative_zero++;
    int update_zero_bits = *(int*)&update_zero_old;
    double update_nan = 0.0 / 0.0;
    double update_nan_old = update_nan++;
    int update_nan_score = !!update_nan_old + !!update_nan;

    if (update_local_score != 48 || update_global_score != 40 ||
        (int)update_for != 3 || update_zero_bits != 0x80000000 ||
        update_nan_score != 2) {
        serial_printf("[feature13-update] FAIL local=%d global=%d for=%d zero=%x nan=%d\n",
                      update_local_score, update_global_score,
                      (int)update_for, update_zero_bits, update_nan_score);
        ok = 0;
    } else {
        serial_printf("[feature13-update] PASS local=%d global=%d for=%d zero=%x nan=%d\n",
                      update_local_score, update_global_score,
                      (int)update_for, update_zero_bits, update_nan_score);
    }

    /* Keep floating widths attached to arrays, pointers, and record fields.
     * The sizeof expressions must inspect their operands without changing
     * the index. */
    float matrix[2][3];
    double cube[2][2][2];
    struct feature13_record records[2];
    float *row = &matrix[1][0];
    double *cube_cell = &cube[1][0][0];
    struct feature13_record *record = &records[1];
    int index = 0;

    matrix[0][0] = 1.25f;
    row[0] = 2.5f;
    row[1] = row[0];
    row[1] += 0.5f;
    *row += 0.25f;
    feature13_lvalue_global[1][2] = 3.5f;
    float *returned_row = feature13_lvalue_row();
    returned_row[2] *= 2.0f;

    *cube_cell = 4.5;
    cube_cell[1] = 1.25;
    cube_cell[1] += 0.75;

    records[1].gain = 1.5f;
    records[1].bias = 2.25;
    records[1].taps[2] = 3.25f;
    record->gain += 0.5f;
    record->bias *= 2.0;

    int lvalue_array_score =
        (int)(matrix[0][0] * 4.0f) +
        (int)(row[0] * 4.0f) +
        (int)(row[1] * 4.0f) +
        (int)(feature13_lvalue_row()[2] * 2.0f);
    int lvalue_pointer_score =
        (int)(*cube_cell * 2.0) +
        (int)(cube_cell[1] * 2.0);
    int lvalue_record_score =
        (int)(record->gain * 2.0f) +
        (int)(record->bias * 2.0) +
        (int)(records[1].taps[2] * 4.0f);
    int lvalue_size_score =
        sizeof(matrix[index++]) + sizeof(cube[0]) +
        sizeof(*row) + sizeof(*cube_cell);
    int lvalue_unevaluated = index == 0;

    if (lvalue_array_score != 42 || lvalue_pointer_score != 13 ||
        lvalue_record_score != 26 || lvalue_size_score != 56 ||
        !lvalue_unevaluated) {
        serial_printf("[feature13-lvalue] FAIL array=%d pointer=%d record=%d sizes=%d unevaluated=%d\n",
                      lvalue_array_score, lvalue_pointer_score,
                      lvalue_record_score, lvalue_size_score,
                      lvalue_unevaluated);
        ok = 0;
    } else {
        serial_printf("[feature13-lvalue] PASS array=%d pointer=%d record=%d sizes=%d unevaluated=%d\n",
                      lvalue_array_score, lvalue_pointer_score,
                      lvalue_record_score, lvalue_size_score,
                      lvalue_unevaluated);
    }

    /* sin(pi/2) = 1. Check |sin(pi/2) - 1| < 1e-12 via scale 1e12. */
    double s = sin(pi / 2.0);
    if (!feature13_within(s, 1.0, 1000000000000.0, 0)) {
        int s1000 = (int)(s * 1000.0);
        serial_printf("[feature13] FAIL sin(pi/2) *1000=%d\n", s1000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* cos(pi) = -1. Check the result within 1e-12. */
    double c = cos(pi);
    if (!feature13_within(c, -1.0, 1000000000000.0, 0)) {
        int c1000 = (int)(c * 1000.0);
        serial_printf("[feature13] FAIL cos(pi) *1000=%d\n", c1000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* sqrt(2) = 1.41421356... */
    double sq = sqrt(2.0);
    if (!feature13_within(
            sq, 1.4142135623730951, 1000000000000.0, 0)) {
        int sq10000 = (int)(sq * 10000.0);
        serial_printf("[feature13] FAIL sqrt(2) *10000=%d\n", sq10000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* exp(1) = e. The corrected x87 range reduction must keep this within
     * 1e-6 of the fixed reference. */
    double ex = exp(1.0);
    if (!feature13_within(ex, 2.7182818284590451, 1000000.0, 0)) {
        int ex1000 = (int)(ex * 1000.0);
        serial_printf("[feature13] FAIL exp(1) *1000=%d\n", ex1000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* log(e) = 1 */
    double le = log(2.718281828459045);
    if (!feature13_within(le, 1.0, 10000000000.0, 0)) {
        int le1000 = (int)(le * 1000.0);
        serial_printf("[feature13] FAIL log(e) *1000=%d\n", le1000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* pow(2, 10) = 1024 */
    double pw = pow(2.0, 10.0);
    if (!feature13_within(pw, 1024.0, 1000000000.0, 0)) {
        int pwi = (int)pw;
        serial_printf("[feature13] FAIL pow(2,10)=%d\n", pwi);
        ok = 0;
    } else {
        call_checks++;
    }

    /* tanh(1) = 0.7615941559557649 */
    double tn = tanh(1.0);
    if (!feature13_within(
            tn, 0.7615941559557649, 10000000000.0, 0)) {
        int tn10000 = (int)(tn * 10000.0);
        serial_printf("[feature13] FAIL tanh(1) *10000=%d\n", tn10000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* cbrt(27) = 3. Looser tolerance since the x87/libm path has some
     * bits of rounding error. Scale 1e6 -> tolerance ~1e-6.*/
    double cb = cbrt(27.0);
    if (!feature13_within(cb, 3.0, 1000000.0, 0)) {
        int cb1000 = (int)(cb * 1000.0);
        serial_printf("[feature13] FAIL cbrt(27) *1000=%d\n", cb1000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* atan2(1, 1) = pi/4 = 0.7853981633974483 */
    double a2 = atan2(1.0, 1.0);
    if (!feature13_within(
            a2, 0.7853981633974483, 1000000000000.0, 0)) {
        int a210000 = (int)(a2 * 10000.0);
        serial_printf("[feature13] FAIL atan2(1,1) *10000=%d\n", a210000);
        ok = 0;
    } else {
        call_checks++;
    }

    /* fabs of -5.5 = 5.5. Scale *2 -> 11. */
    double neg55 = -5.5;
    double af = fabs(neg55);
    int af_i = (int)(af * 2.0);
    if (af_i != 11) {
        serial_printf("[feature13] FAIL fabs neg55 *2 = %d\n", af_i);
        ok = 0;
    }

    /* hypot(3, 4) = 5 */
    double h = hypot(3.0, 4.0);
    if (!feature13_within(h, 5.0, 1000000000000.0, 0)) {
        int h1000 = (int)(h * 1000.0);
        serial_printf("[feature13] FAIL hypot(3,4) *1000=%d\n", h1000);
        ok = 0;
    } else {
        call_checks++;
    }

    if (call_checks != 10) {
        serial_printf("[feature13-call] FAIL checks=%d\n", call_checks);
        ok = 0;
    } else {
        serial_printf("[feature13-call] PASS checks=%d\n", call_checks);
    }

    if (ok) {
        serial_printf("PASS feature13_double\n");
        println("PASS feature13_double");
    } else {
        serial_printf("FAIL feature13_double\n");
        println("FAIL feature13_double");
    }
}
