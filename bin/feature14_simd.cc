//help: Tests private CupidC packed arithmetic, arrays, and SSE intrinsics.
//help: Usage: feature14_simd
//help: Prints focused PASS markers followed by the overall result.

float4 feature14_global_floats[3];
double2 feature14_global_doubles[2];
int feature14_global_sentinel;
int feature14_matrix_leading_canary = 59;
float4 feature14_global_matrix[2][2];
double2 feature14_global_cube[2][2][2];
float4 feature14_update_float;
double2 feature14_update_double;
int feature14_matrix_trailing_canary = 61;
int feature14_matrix_outer_calls;
int feature14_matrix_middle_calls;
int feature14_matrix_inner_calls;
int feature14_matrix_sizeof_calls;
int feature14_float_call_count;
int feature14_double_call_count;

int feature14_next_outer() {
    feature14_matrix_outer_calls += 1;
    return 1;
}

int feature14_next_middle() {
    feature14_matrix_middle_calls += 1;
    return 0;
}

int feature14_next_inner() {
    feature14_matrix_inner_calls += 1;
    return 1;
}

int feature14_sizeof_index() {
    feature14_matrix_sizeof_calls += 1;
    return 1;
}

int feature14_test_updates() {
    static float4 saved_float;
    static double2 saved_double;
    int float_nan_bits = 0x7fc13579;
    float float_nan = *(float *)&float_nan_bits;
    float float_zero = 0.0f;
    float float_negative_zero = -float_zero;
    float4 float_seed = {
        float_nan, float_negative_zero, 6.0f, -2.0f
    };
    double double_nan;
    int *double_bits = (int *)&double_nan;
    double_bits[0] = (int)0x89abcdef;
    double_bits[1] = 0x7ff81234;
    double2 double_seed = {double_nan, -0.0};
    float4 local_float;
    double2 local_double;
    float4 old_float;
    double2 old_double;

    feature14_update_float = float_seed;
    feature14_update_double = double_seed;
    local_float = float_seed;
    local_double = double_seed;
    saved_float = float_seed;
    saved_double = double_seed;

    old_float = feature14_update_float++;
    old_double = feature14_update_double--;
    ++local_float;
    local_double--;
    ++saved_float;
    saved_double--;

    float old_float_lane = old_float.x;
    if (*(int *)&old_float_lane != float_nan_bits) return 1;
    old_float_lane = old_float.y;
    if (*(int *)&old_float_lane != (int)0x80000000) return 2;
    old_float_lane = old_float.z;
    if (*(int *)&old_float_lane != 0x40c00000) return 3;
    old_float_lane = old_float.w;
    if (*(int *)&old_float_lane != (int)0xc0000000) return 4;

    double old_double_lane = old_double.x;
    double_bits = (int *)&old_double_lane;
    if (double_bits[0] != (int)0x89abcdef ||
        double_bits[1] != 0x7ff81234) return 5;
    old_double_lane = old_double.y;
    double_bits = (int *)&old_double_lane;
    if (double_bits[0] != 0 ||
        double_bits[1] != (int)0x80000000) return 6;

    if (feature14_update_float.z != 7.0f ||
        feature14_update_float.w != -1.0f ||
        feature14_update_double.y != -1.0) return 7;
    if (local_float.z != 7.0f || local_float.w != -1.0f ||
        local_double.y != -1.0) return 8;
    if (saved_float.z != 7.0f || saved_float.w != -1.0f ||
        saved_double.y != -1.0) return 9;

    float4 line_seed = {1.0f, 2.0f, 3.0f, 4.0f};
    double2 cube_seed = {1.5, 2.5};
    float4 old_line;
    float4 new_matrix;
    double2 old_cube;
    int line_index = 1;
    int matrix_outer = 1;
    int matrix_inner = 0;
    int cube_outer = 1;
    int cube_middle = 0;
    int cube_inner = 1;

    feature14_global_floats[1] = line_seed;
    feature14_global_matrix[1][0] = line_seed;
    feature14_global_cube[1][0][1] = cube_seed;
    old_line = feature14_global_floats[line_index++]++;
    new_matrix = --feature14_global_matrix[matrix_outer++][matrix_inner++];
    old_cube = feature14_global_cube[cube_outer++][cube_middle++]
                                    [cube_inner++]--;

    if (old_line.x != 1.0f || old_line.w != 4.0f ||
        feature14_global_floats[1].x != 2.0f ||
        feature14_global_floats[1].w != 5.0f) return 10;
    if (new_matrix.x != 0.0f || new_matrix.w != 3.0f ||
        feature14_global_matrix[1][0].x != 0.0f ||
        feature14_global_matrix[1][0].w != 3.0f) return 11;
    if (old_cube.x != 1.5 || old_cube.y != 2.5 ||
        feature14_global_cube[1][0][1].x != 0.5 ||
        feature14_global_cube[1][0][1].y != 1.5) return 12;
    if (line_index != 2 || matrix_outer != 2 || matrix_inner != 1 ||
        cube_outer != 2 || cube_middle != 1 || cube_inner != 2) return 13;
    return 0;
}

float4 feature14_merge_float4(float4 left, int marker, float4 right) {
    feature14_float_call_count += 1;
    if (marker != 7 && marker != 11) return left;
    return left + right;
}

float4 feature14_nested_float4(float4 first, float4 second, float4 third) {
    feature14_float_call_count += 1;
    return feature14_merge_float4(
        feature14_merge_float4(first, 7, second), 11, third);
}

double2 feature14_merge_double2(double2 left, int marker, double2 right) {
    feature14_double_call_count += 1;
    if (marker != 13 && marker != 17) return left;
    return left + right;
}

double2 feature14_nested_double2(double2 first, double2 second,
                                 double2 third) {
    feature14_double_call_count += 1;
    return feature14_merge_double2(
        feature14_merge_double2(first, 13, second), 17, third);
}

int feature14_test_calls() {
    float4 first = {1.0f, 2.0f, 3.0f, 4.0f};
    float4 second = {5.0f, 6.0f, 7.0f, 8.0f};
    float4 third = {9.0f, 10.0f, 11.0f, 12.0f};
    double2 wide_first = {1.5, 2.5};
    double2 wide_second = {3.0, 4.0};
    double2 wide_third = {5.5, 6.5};
    float4 floats;
    double2 doubles;

    feature14_float_call_count = 0;
    feature14_double_call_count = 0;
    floats = feature14_nested_float4(first, second, third);
    doubles = feature14_nested_double2(
        wide_first, wide_second, wide_third);

    if (feature14_float_call_count != 3 ||
        feature14_double_call_count != 3) return 1;
    if (floats.x != 15.0f || floats.y != 18.0f ||
        floats.z != 21.0f || floats.w != 24.0f) return 2;
    if (doubles.x != 10.0 || doubles.y != 13.0) return 3;
    return 0;
}

int main() {
    int ok = 1;

    /* _mm_add_ps: lane-wise a+b.  Each result maps to an exact IEEE-754
     * bit pattern so == on bit-reinterpret-to-int is safe.
     *
     * CupidC doesn't support intrinsic calls as initializer rvalues
     * (initializer path only accepts '{...}' brace form).  Declare the
     * SIMD local zero-initialized, then assign via '=' - the assignment
     * path emits MOVUPS xmm0 -> [ebp+disp].*/
    float4 a = {1.0, 2.0, 3.0, 4.0};
    float4 b = {5.0, 6.0, 7.0, 8.0};
    float4 s;
    s = _mm_add_ps(a, b);
    float sx = s.x;  /* 6.0  -> 0x40C00000 */
    float sy = s.y;  /* 8.0  -> 0x41000000 */
    float sz = s.z;  /* 10.0 -> 0x41200000 */
    float sw = s.w;  /* 12.0 -> 0x41400000 */
    if (*(int*)&sx != 0x40C00000 || *(int*)&sy != 0x41000000 ||
        *(int*)&sz != 0x41200000 || *(int*)&sw != 0x41400000) {
        serial_printf("[feature14] FAIL _mm_add_ps: 0x%x 0x%x 0x%x 0x%x\n",
                      *(int*)&sx, *(int*)&sy, *(int*)&sz, *(int*)&sw);
        ok = 0;
    }

    /* _mm_sub_ps: non-commutative - verify a-b, not b-a. */
    float4 d;
    d = _mm_sub_ps(a, b);
    float dx = d.x;  /* 1-5 = -4.0 -> 0xC0800000 */
    if (*(int*)&dx != 0xC0800000) {
        serial_printf("[feature14] FAIL _mm_sub_ps: bits 0x%x\n",
                      *(int*)&dx);
        ok = 0;
    }

    /* _mm_mul_ps: lane-wise a*b. 2*6=12 -> 0x41400000 */
    float4 m;
    m = _mm_mul_ps(a, b);
    float my = m.y;
    if (*(int*)&my != 0x41400000) {
        serial_printf("[feature14] FAIL _mm_mul_ps: bits 0x%x\n", *(int*)&my);
        ok = 0;
    }

    /* _mm_div_ps: non-commutative - 4.0/8.0 = 0.5 -> 0x3F000000 */
    float4 q;
    q = _mm_div_ps(a, b);
    float qw = q.w;
    if (*(int*)&qw != 0x3F000000) {
        serial_printf("[feature14] FAIL _mm_div_ps: bits 0x%x\n",
                      *(int*)&qw);
        ok = 0;
    }

    /* _mm_sqrt_ps: sqrt of {1,4,9,16} -> {1,2,3,4}. */
    float4 squares = {1.0, 4.0, 9.0, 16.0};
    float4 r;
    r = _mm_sqrt_ps(squares);
    float rx = r.x;
    float rw = r.w;
    if (*(int*)&rx != 0x3F800000 || *(int*)&rw != 0x40800000) {
        serial_printf("[feature14] FAIL _mm_sqrt_ps: 0x%x .. 0x%x\n",
                      *(int*)&rx, *(int*)&rw);
        ok = 0;
    }

    /* _mm_min_ps / _mm_max_ps. */
    float4 mn;
    float4 mx;
    mn = _mm_min_ps(a, b);
    mx = _mm_max_ps(a, b);
    float mnx = mn.x;
    float mxw = mx.w;
    if (*(int*)&mnx != 0x3F800000 || *(int*)&mxw != 0x41000000) {
        serial_printf("[feature14] FAIL min/max: min.x=0x%x max.w=0x%x\n",
                      *(int*)&mnx, *(int*)&mxw);
        ok = 0;
    }

    /* _mm_set1_ps: broadcast scalar to all 4 lanes. */
    float4 s1;
    s1 = _mm_set1_ps(2.5);
    float s1x = s1.x;
    float s1z = s1.z;
    if (*(int*)&s1x != 0x40200000 || *(int*)&s1z != 0x40200000) {
        serial_printf("[feature14] FAIL _mm_set1_ps: 0x%x 0x%x\n",
                      *(int*)&s1x, *(int*)&s1z);
        ok = 0;
    }

    /* _mm_cmpeq_ps: returns all-ones mask where equal, 0 where not. */
    float4 pv = {1.0, 2.0, 3.0, 4.0};
    float4 eq;
    eq = _mm_cmpeq_ps(a, pv);  /* a == p elementwise -> all-ones */
    float eq_lane = eq.x;
    int eqx = *(int*)&eq_lane;
    if (eqx != 0xFFFFFFFF) {
        serial_printf("[feature14] FAIL _mm_cmpeq_ps: got 0x%x\n", eqx);
        ok = 0;
    }

    /* _mm_cmpgt_ps(b, a) - every lane b>a -> all-ones in each lane.
     * Tests the operand-swap path.*/
    float4 gt;
    gt = _mm_cmpgt_ps(b, a);
    float gt_lane = gt.x;
    int gtx = *(int*)&gt_lane;
    if (gtx != 0xFFFFFFFF) {
        serial_printf("[feature14] FAIL _mm_cmpgt_ps: got 0x%x\n", gtx);
        ok = 0;
    }

    /* _mm_movemask_ps extracts one sign bit from each lane. */
    float m1 = -1.0;
    float4 neg = {m1, m1, m1, m1};
    int mask = _mm_movemask_ps(neg);
    if (mask != 15) {
        serial_printf("[feature14] FAIL _mm_movemask_ps: got %d\n", mask);
        ok = 0;
    }

    /* _mm_xor_ps with itself -> all zeros. */
    float4 z;
    z = _mm_xor_ps(a, a);
    float zx = z.x;
    if (*(int*)&zx != 0) {
        serial_printf("[feature14] FAIL _mm_xor_ps: 0x%x\n", *(int*)&zx);
        ok = 0;
    }

    /* Double-precision packed intrinsics use the same assignment path. */

    /* _mm_mul_pd: {1.5,2.5} * {1.5,2.5} = {2.25, 6.25}. scale*100 -> 225, 625. */
    double2 dv = {1.5, 2.5};
    double2 dw;
    dw = _mm_mul_pd(dv, dv);
    double dmx = dw.x;
    double dmy = dw.y;
    int dmx_i = (int)(dmx * 100.0);
    int dmy_i = (int)(dmy * 100.0);
    if (dmx_i != 225 || dmy_i != 625) {
        serial_printf("[feature14] FAIL _mm_mul_pd: %d %d\n", dmx_i, dmy_i);
        ok = 0;
    }

    /* _mm_add_pd: {1.5,2.5} + {0.5,-0.5} = {2.0, 2.0}. */
    double neg_half = -0.5;
    double2 du = {0.5, neg_half};
    double2 dsum;
    dsum = _mm_add_pd(dv, du);
    double dsum_x = dsum.x;
    double dsum_y = dsum.y;
    int dsum_x_i = (int)dsum_x;
    int dsum_y_i = (int)dsum_y;
    if (dsum_x_i != 2 || dsum_y_i != 2) {
        serial_printf("[feature14] FAIL _mm_add_pd: %d %d\n",
                      dsum_x_i, dsum_y_i);
        ok = 0;
    }

    /* _mm_sub_pd: {1.5,2.5} - {0.5,-0.5} = {1.0, 3.0}. */
    double2 dsub;
    dsub = _mm_sub_pd(dv, du);
    double dsub_x = dsub.x;
    double dsub_y = dsub.y;
    int dsub_x_i = (int)dsub_x;
    int dsub_y_i = (int)dsub_y;
    if (dsub_x_i != 1 || dsub_y_i != 3) {
        serial_printf("[feature14] FAIL _mm_sub_pd: %d %d\n",
                      dsub_x_i, dsub_y_i);
        ok = 0;
    }

    /* _mm_div_pd: {1.5,2.5}/{0.5,0.5} = {3.0, 5.0}. */
    double2 dhalf = {0.5, 0.5};
    double2 ddiv;
    ddiv = _mm_div_pd(dv, dhalf);
    double ddiv_x = ddiv.x;
    double ddiv_y = ddiv.y;
    int ddiv_x_i = (int)ddiv_x;
    int ddiv_y_i = (int)ddiv_y;
    if (ddiv_x_i != 3 || ddiv_y_i != 5) {
        serial_printf("[feature14] FAIL _mm_div_pd: %d %d\n",
                      ddiv_x_i, ddiv_y_i);
        ok = 0;
    }

    /* _mm_sqrt_pd: sqrt({4,16}) = {2,4}. */
    double2 dsq = {4.0, 16.0};
    double2 drt;
    drt = _mm_sqrt_pd(dsq);
    double drt_x = drt.x;
    double drt_y = drt.y;
    int drt_x_i = (int)drt_x;
    int drt_y_i = (int)drt_y;
    if (drt_x_i != 2 || drt_y_i != 4) {
        serial_printf("[feature14] FAIL _mm_sqrt_pd: %d %d\n",
                      drt_x_i, drt_y_i);
        ok = 0;
    }

    /* _mm_min_pd / _mm_max_pd.
     * min({1.5,2.5},{0.5,2.0}) = {0.5, 2.0};  *2 -> 1, 4
     * max = {1.5, 2.5};                        *2 -> 3, 5*/
    double2 dpos = {0.5, 2.0};
    double2 dmin;
    double2 dmax;
    dmin = _mm_min_pd(dv, dpos);
    dmax = _mm_max_pd(dv, dpos);
    double dmin_x = dmin.x;
    double dmin_y = dmin.y;
    double dmax_x = dmax.x;
    double dmax_y = dmax.y;
    int dmin_x_i = (int)(dmin_x * 2.0);
    int dmin_y_i = (int)(dmin_y * 2.0);
    int dmax_x_i = (int)(dmax_x * 2.0);
    int dmax_y_i = (int)(dmax_y * 2.0);
    if (dmin_x_i != 1 || dmin_y_i != 4 ||
        dmax_x_i != 3 || dmax_y_i != 5) {
        serial_printf("[feature14] FAIL min/max_pd: min={%d,%d} max={%d,%d}\n",
                      dmin_x_i, dmin_y_i, dmax_x_i, dmax_y_i);
        ok = 0;
    }

    /* _mm_xor_pd with itself -> all zeros. */
    double2 dzero;
    dzero = _mm_xor_pd(dv, dv);
    double dzero_x = dzero.x;
    double dzero_y = dzero.y;
    int dzero_x_i = (int)dzero_x;
    int dzero_y_i = (int)dzero_y;
    if (dzero_x_i != 0 || dzero_y_i != 0) {
        serial_printf("[feature14] FAIL _mm_xor_pd: %d %d\n",
                      dzero_x_i, dzero_y_i);
        ok = 0;
    }

    /* _mm_set1_pd: broadcast scalar to both lanes. 3.75 * 4 = 15. */
    double2 dbc;
    dbc = _mm_set1_pd(3.75);
    double dbc_x = dbc.x;
    double dbc_y = dbc.y;
    int dbc_x_i = (int)(dbc_x * 4.0);
    int dbc_y_i = (int)(dbc_y * 4.0);
    if (dbc_x_i != 15 || dbc_y_i != 15) {
        serial_printf("[feature14] FAIL _mm_set1_pd: %d %d\n",
                      dbc_x_i, dbc_y_i);
        ok = 0;
    }

    int operator_ok = 1;
    float4 direct_float;
    direct_float = a + b;
    if (direct_float.x != 6.0f || direct_float.w != 12.0f) {
        serial_printf("[feature14-operator] FAIL float add\n");
        operator_ok = 0;
    }
    direct_float = b - a;
    if (direct_float.y != 4.0f || direct_float.z != 4.0f) {
        serial_printf("[feature14-operator] FAIL float subtract\n");
        operator_ok = 0;
    }
    direct_float = a * b;
    if (direct_float.x != 5.0f || direct_float.w != 32.0f) {
        serial_printf("[feature14-operator] FAIL float multiply\n");
        operator_ok = 0;
    }
    direct_float = b / a;
    if (direct_float.x != 5.0f || direct_float.w != 2.0f) {
        serial_printf("[feature14-operator] FAIL float divide\n");
        operator_ok = 0;
    }

    double2 direct_double;
    direct_double = dv + dpos;
    if (direct_double.x != 2.0 || direct_double.y != 4.5) {
        serial_printf("[feature14-operator] FAIL double add\n");
        operator_ok = 0;
    }
    direct_double = dv - dpos;
    if (direct_double.x != 1.0 || direct_double.y != 0.5) {
        serial_printf("[feature14-operator] FAIL double subtract\n");
        operator_ok = 0;
    }
    direct_double = dv * dpos;
    double direct_double_fraction = direct_double.x;
    int *direct_double_bits = (int *)&direct_double_fraction;
    if (direct_double_bits[0] != 0 ||
        direct_double_bits[1] != 0x3fe80000) {
        serial_printf("[feature14-operator] FAIL double multiply x\n");
        operator_ok = 0;
    }
    if (direct_double.y != 5.0) {
        serial_printf("[feature14-operator] FAIL double multiply y\n");
        operator_ok = 0;
    }
    direct_double = dv / dpos;
    if (direct_double.x != 3.0 || direct_double.y != 1.25) {
        serial_printf("[feature14-operator] FAIL double divide\n");
        operator_ok = 0;
    }
    if (operator_ok) {
        serial_printf("[feature14-operator] PASS float=4 double=4\n");
    } else {
        serial_printf("[feature14-operator] FAIL\n");
        ok = 0;
    }

    int array_ok = 1;
    float4 local_floats[2];
    double2 local_doubles[2];
    static float4 saved_floats[2];
    static double2 saved_doubles[2];
    feature14_global_sentinel = 73;
    feature14_global_floats[0] = a;
    feature14_global_floats[1] = b;
    feature14_global_floats[1] += a;
    feature14_global_floats[1] -= a;
    feature14_global_floats[1] *= a;
    feature14_global_floats[1] /= a;
    feature14_global_doubles[0] = dv;
    feature14_global_doubles[1] = dpos;
    feature14_global_doubles[1] += dv;
    feature14_global_doubles[1] -= dv;
    feature14_global_doubles[1] *= dv;
    feature14_global_doubles[1] /= dv;
    local_floats[0] = feature14_global_floats[0];
    local_floats[1] = feature14_global_floats[1];
    local_doubles[0] = feature14_global_doubles[0];
    local_doubles[1] = feature14_global_doubles[1];
    int array_index = 0;
    local_floats[array_index++] += a;
    saved_floats[1] = local_floats[1];
    saved_doubles[1] = local_doubles[1];
    if (saved_floats[1].w != 8.0f || saved_doubles[1].y != 2.0)
        array_ok = 0;
    if (array_index != 1 || local_floats[0].x != 2.0f)
        array_ok = 0;
    if (feature14_global_floats[2].z != 0.0f)
        array_ok = 0;
    if (sizeof(*feature14_global_floats) != 16 ||
        sizeof(*local_doubles) != 16)
        array_ok = 0;
    if (feature14_global_sentinel != 73)
        array_ok = 0;
    if (array_ok) {
        serial_printf("[feature14-array] PASS global=2 local=2 static=2 sizeof=16 index=1\n");
    } else {
        serial_printf("[feature14-array] FAIL\n");
        ok = 0;
    }

    int matrix_ok = 1;
    int matrix_leading_canary = 67;
    float4 local_matrix[2][2];
    double2 local_cube[2][2][2];
    static float4 saved_matrix[2][2];
    static double2 saved_cube[2][2][2];
    int matrix_trailing_canary = 71;
    double2 local_step = {0.5, 0.5};

    feature14_global_matrix[1][0] = a;
    feature14_global_matrix[1][0] += b;
    feature14_global_matrix[1][0] -= b;
    feature14_global_matrix[1][0] *= b;
    feature14_global_matrix[1][0] /= b;
    feature14_global_cube[1][0][1] = dv;
    feature14_global_cube[feature14_next_outer()][feature14_next_middle()]
                         [feature14_next_inner()] += dpos;

    local_matrix[1][0] = a;
    local_matrix[1][0] += b;
    local_cube[0][1][1] = dv;
    local_cube[0][1][1] += local_step;
    saved_matrix[0][1] = b;
    saved_matrix[0][1] *= a;
    saved_cube[1][1][0] = dv;
    saved_cube[1][1][0] += local_step;
    saved_cube[1][1][0] -= local_step;
    saved_cube[1][1][0] *= local_step;
    saved_cube[1][1][0] /= local_step;

    if (feature14_global_matrix[1][0].x != 1.0f ||
        feature14_global_matrix[1][0].w != 4.0f)
        matrix_ok = 0;
    if (feature14_global_cube[feature14_next_outer()][feature14_next_middle()]
                             [feature14_next_inner()].x != 2.0)
        matrix_ok = 0;
    if (feature14_global_cube[1][0][1].x != 2.0 ||
        feature14_global_cube[1][0][1].y != 4.5)
        matrix_ok = 0;
    if (local_matrix[1][0].x != 6.0f ||
        local_matrix[1][0].w != 12.0f)
        matrix_ok = 0;
    if (local_cube[0][1][1].x != 2.0 ||
        local_cube[0][1][1].y != 3.0)
        matrix_ok = 0;
    if (saved_matrix[0][1].x != 5.0f ||
        saved_matrix[0][1].w != 32.0f)
        matrix_ok = 0;
    if (saved_cube[1][1][0].x != 1.5 ||
        saved_cube[1][1][0].y != 2.5)
        matrix_ok = 0;
    if (sizeof(*feature14_global_matrix) != 32 ||
        sizeof(**feature14_global_matrix) != 16 ||
        sizeof(*feature14_global_cube) != 64 ||
        sizeof(**feature14_global_cube) != 32 ||
        sizeof(***feature14_global_cube) != 16)
        matrix_ok = 0;
    if (sizeof(feature14_global_matrix[feature14_sizeof_index()]) != 32 ||
        sizeof(feature14_global_cube[0][feature14_sizeof_index()]) != 32 ||
        sizeof(feature14_global_cube[0][0][0]) != 16)
        matrix_ok = 0;
    if (feature14_matrix_sizeof_calls != 0)
        matrix_ok = 0;
    if (feature14_matrix_outer_calls != 2 ||
        feature14_matrix_middle_calls != 2 ||
        feature14_matrix_inner_calls != 2)
        matrix_ok = 0;
    if (feature14_matrix_leading_canary != 59 ||
        feature14_matrix_trailing_canary != 61 ||
        matrix_leading_canary != 67 || matrix_trailing_canary != 71)
        matrix_ok = 0;
    if (feature14_global_matrix[0][1].z != 0.0f ||
        saved_cube[0][0][0].y != 0.0)
        matrix_ok = 0;

    if (matrix_ok) {
        serial_printf("[feature14-matrix] PASS global=2 local=2 static=2 sizes=8 index=6 unevaluated=2 canary=4\n");
    } else {
        serial_printf("[feature14-matrix] FAIL\n");
        ok = 0;
    }

    int update_result = feature14_test_updates();
    if (update_result == 0) {
        serial_printf("[feature14-update] PASS direct=6 leaves=3 once=6 payload=8\n");
    } else {
        serial_printf("[feature14-update] FAIL check=%d\n", update_result);
        ok = 0;
    }

    int call_result = feature14_test_calls();
    if (call_result == 0) {
        serial_printf("[feature14-call] PASS float4=4 double2=2 nested=2 calls=6\n");
    } else {
        serial_printf("[feature14-call] FAIL check=%d\n", call_result);
        ok = 0;
    }

    int minmax_ok = 1;
    float edge_float_nan = 0.0f / 0.0f;
    float edge_float_zero = 0.0f;
    float edge_float_negative_zero = -edge_float_zero;
    float4 edge_float_first = {
        edge_float_nan, edge_float_zero,
        edge_float_nan, edge_float_zero
    };
    float4 edge_float_second = {
        5.0f, edge_float_negative_zero,
        -7.0f, edge_float_negative_zero
    };
    float4 edge_float_min;
    float4 edge_float_max;
    edge_float_min = _mm_min_ps(edge_float_first, edge_float_second);
    edge_float_max = _mm_max_ps(edge_float_first, edge_float_second);
    float edge_float_lane = edge_float_min.y;
    if (edge_float_min.x != 5.0f || edge_float_max.z != -7.0f ||
        *(int *)&edge_float_lane != (int)0x80000000)
        minmax_ok = 0;
    edge_float_lane = edge_float_max.w;
    if (*(int *)&edge_float_lane != (int)0x80000000)
        minmax_ok = 0;

    double edge_double_nan = 0.0 / 0.0;
    double edge_double_zero = 0.0;
    double edge_double_negative_zero = -edge_double_zero;
    double2 edge_double_first = {edge_double_nan, edge_double_zero};
    double2 edge_double_second = {9.0, edge_double_negative_zero};
    double2 edge_double_min;
    double2 edge_double_max;
    edge_double_min = _mm_min_pd(edge_double_first, edge_double_second);
    edge_double_max = _mm_max_pd(edge_double_first, edge_double_second);
    double edge_double_lane = edge_double_min.y;
    int *edge_double_bits = (int *)&edge_double_lane;
    if (edge_double_min.x != 9.0 || edge_double_max.x != 9.0 ||
        edge_double_bits[1] != (int)0x80000000)
        minmax_ok = 0;
    edge_double_lane = edge_double_max.y;
    edge_double_bits = (int *)&edge_double_lane;
    if (edge_double_bits[1] != (int)0x80000000)
        minmax_ok = 0;
    if (minmax_ok) {
        serial_printf("[feature14-minmax] PASS nan=4 signed_zero=4\n");
    } else {
        serial_printf("[feature14-minmax] FAIL\n");
        ok = 0;
    }

    /* Both-NaN ADD/MUL results remain NaNs from one of the written inputs.
     * The emitted instruction order is checked by the host byte contract;
     * hardware and emulators may select different payloads. */
    int nan_ok = 1;
    int float_left_count = 0;
    int float_right_count = 0;
    int double_left_count = 0;
    int double_right_count = 0;
    int float_left_bits = 0x7fc00011;
    int float_right_bits = 0x7fc00022;
    float float_left_nan = *(float *)&float_left_bits;
    float float_right_nan = *(float *)&float_right_bits;
    float4 float_left = {
        float_left_nan, float_left_nan,
        float_left_nan, float_left_nan
    };
    float4 float_right = {
        float_right_nan, float_right_nan,
        float_right_nan, float_right_nan
    };
    float4 float_result;
    float float_lane;
    int float_payload;
    float_result = float_left + float_right;
    float_lane = float_result.x;
    float_payload = *(int *)&float_lane;
    if (float_payload == float_left_bits) float_left_count++;
    else if (float_payload == float_right_bits) float_right_count++;
    else nan_ok = 0;
    float_result = float_left * float_right;
    float_lane = float_result.x;
    float_payload = *(int *)&float_lane;
    if (float_payload == float_left_bits) float_left_count++;
    else if (float_payload == float_right_bits) float_right_count++;
    else nan_ok = 0;
    float_result = _mm_add_ps(float_left, float_right);
    float_lane = float_result.x;
    float_payload = *(int *)&float_lane;
    if (float_payload == float_left_bits) float_left_count++;
    else if (float_payload == float_right_bits) float_right_count++;
    else nan_ok = 0;
    float_result = _mm_mul_ps(float_left, float_right);
    float_lane = float_result.x;
    float_payload = *(int *)&float_lane;
    if (float_payload == float_left_bits) float_left_count++;
    else if (float_payload == float_right_bits) float_right_count++;
    else nan_ok = 0;

    double double_left_nan;
    double double_right_nan;
    int *double_order_bits = (int *)&double_left_nan;
    double_order_bits[0] = 0x11;
    double_order_bits[1] = 0x7ff80000;
    double_order_bits = (int *)&double_right_nan;
    double_order_bits[0] = 0x22;
    double_order_bits[1] = 0x7ff80000;
    double2 double_left = {double_left_nan, double_left_nan};
    double2 double_right = {double_right_nan, double_right_nan};
    double2 double_result;
    double double_lane;
    double_result = double_left + double_right;
    double_lane = double_result.x;
    double_order_bits = (int *)&double_lane;
    if (double_order_bits[1] != 0x7ff80000) nan_ok = 0;
    else if (double_order_bits[0] == 0x11) double_left_count++;
    else if (double_order_bits[0] == 0x22) double_right_count++;
    else nan_ok = 0;
    double_result = double_left * double_right;
    double_lane = double_result.x;
    double_order_bits = (int *)&double_lane;
    if (double_order_bits[1] != 0x7ff80000) nan_ok = 0;
    else if (double_order_bits[0] == 0x11) double_left_count++;
    else if (double_order_bits[0] == 0x22) double_right_count++;
    else nan_ok = 0;
    double_result = _mm_add_pd(double_left, double_right);
    double_lane = double_result.x;
    double_order_bits = (int *)&double_lane;
    if (double_order_bits[1] != 0x7ff80000) nan_ok = 0;
    else if (double_order_bits[0] == 0x11) double_left_count++;
    else if (double_order_bits[0] == 0x22) double_right_count++;
    else nan_ok = 0;
    double_result = _mm_mul_pd(double_left, double_right);
    double_lane = double_result.x;
    double_order_bits = (int *)&double_lane;
    if (double_order_bits[1] != 0x7ff80000) nan_ok = 0;
    else if (double_order_bits[0] == 0x11) double_left_count++;
    else if (double_order_bits[0] == 0x22) double_right_count++;
    else nan_ok = 0;
    if (float_left_count + float_right_count != 4 ||
        double_left_count + double_right_count != 4) nan_ok = 0;
    if (nan_ok) {
        serial_printf(
            "[feature14-nan] PASS float_left=%d float_right=%d double_left=%d double_right=%d\n",
            float_left_count, float_right_count,
            double_left_count, double_right_count);
    } else {
        serial_printf("[feature14-nan] FAIL\n");
        ok = 0;
    }

    if (ok) serial_printf("PASS feature14_simd\n");
    else    serial_printf("FAIL feature14_simd\n");
    if (ok) println("PASS feature14_simd");
    else    println("FAIL feature14_simd");
    if (ok) return 0;
    return 1;
}
