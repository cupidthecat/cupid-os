//help: P1 Phase F Tasks 32+33: SSE packed intrinsics (_mm_*_ps, _mm_*_pd)
//help: Usage: feature14_simd
//help: Verifies CupidC recognises _mm_*_ps / _mm_*_pd names and inlines
//help: them to SSE opcodes with bit-exact arithmetic. Prints PASS/FAIL.

void main() {
    int ok = 1;

    /* _mm_add_ps: lane-wise a+b.  Each result maps to an exact IEEE-754
     * bit pattern so == on bit-reinterpret-to-int is safe.
     *
     * CupidC doesn't support intrinsic calls as initializer rvalues
     * (initializer path only accepts '{...}' brace form).  Declare the
     * SIMD local zero-initialized, then assign via '=' — the assignment
     * path emits MOVUPS xmm0 -> [ebp+disp]. */
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

    /* _mm_sub_ps: non-commutative — verify a-b, not b-a. */
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

    /* _mm_div_ps: non-commutative — 4.0/8.0 = 0.5 -> 0x3F000000 */
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
    int eqx = *(int*)&eq;
    if (eqx != 0xFFFFFFFF) {
        serial_printf("[feature14] FAIL _mm_cmpeq_ps: got 0x%x\n", eqx);
        ok = 0;
    }

    /* _mm_cmpgt_ps(b, a) — every lane b>a -> all-ones in each lane.
     * Tests the operand-swap path. */
    float4 gt;
    gt = _mm_cmpgt_ps(b, a);
    int gtx = *(int*)&gt;
    if (gtx != 0xFFFFFFFF) {
        serial_printf("[feature14] FAIL _mm_cmpgt_ps: got 0x%x\n", gtx);
        ok = 0;
    }

    /* _mm_movemask_ps: extract sign bits. -1.0 has sign bit set.
     * Build {-1,-1,-1,-1} via 0.0 - 1.0 since CupidC's unary minus
     * mis-types FP literals as int. */
    float m1 = 0.0 - 1.0;
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

    /* Phase F Task 33: double-precision (_mm_*_pd) intrinsics.
     * Since CupidC doesn't support FP == / != scalars and doesn't emit
     * scaled pointer arithmetic for (int*)&d + 1, we verify results by
     * truncating to int via (int) casts. */

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

    /* _mm_add_pd: {1.5,2.5} + {0.5,-0.5} = {2.0, 2.0}.
     * Build -0.5 via 0.0 - 0.5 (unary minus broken for FP). */
    double neg_half = 0.0 - 0.5;
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
     * Use only positive operands to sidestep (int) cast of negatives
     * which is broken.
     * min({1.5,2.5},{0.5,2.0}) = {0.5, 2.0};  *2 -> 1, 4
     * max = {1.5, 2.5};                        *2 -> 3, 5 */
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

    if (ok) serial_printf("PASS feature14_simd\n");
    else    serial_printf("FAIL feature14_simd\n");
    if (ok) println("PASS feature14_simd");
    else    println("FAIL feature14_simd");
}
