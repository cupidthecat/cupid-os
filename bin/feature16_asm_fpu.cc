//help: P1 Phase G feature test: inline CupidASM using SSE + x87 from CupidC
//help: Usage: feature16_asm_fpu
//help: Verifies asm { } blocks accept SSE scalar and x87 opcodes from Phase B.

void main() {
    int ok = 1;

    /* Test 1: SSE scalar addss via inline asm */
    float a = 2.0;
    float b = 3.0;
    float r = 0.0;
    asm {
        movss xmm0, [a]
        movss xmm1, [b]
        addss xmm0, xmm1
        movss [r], xmm0
    }
    if (*(int*)&r != 0x40A00000) {   /* 5.0 */
        serial_printf("[feature16] FAIL addss: got=0x%x expected=0x40A00000\n",
                      *(int*)&r);
        ok = 0;
    }

    /* Test 2: SSE scalar sqrtss */
    float x = 9.0;
    float sq = 0.0;
    asm {
        movss xmm0, [x]
        sqrtss xmm0, xmm0
        movss [sq], xmm0
    }
    if (*(int*)&sq != 0x40400000) {   /* 3.0 */
        serial_printf("[feature16] FAIL sqrtss: got=0x%x expected=0x40400000\n",
                      *(int*)&sq);
        ok = 0;
    }

    /* Test 3: x87 fsin (m32fp — CupidASM has no qword/dword size keyword, so
     * fld / fstp always emit the D9 base m32fp encoding; use float, not
     * double, for this test). */
    float zero = 0.0;
    float sinz = 1.0;
    asm {
        fld [zero]
        fsin
        fstp [sinz]
        finit
    }
    /* sin(0) == 0.0 — expect bit pattern 0x00000000. */
    if (*(int*)&sinz != 0x00000000) {
        serial_printf("[feature16] FAIL fsin(0): bits=0x%x\n", *(int*)&sinz);
        ok = 0;
    }

    /* Test 4: SSE packed addps via inline asm using float4 locals.
     * float4 stores 16 bytes at a 16-byte aligned stack slot; lane 0 lives
     * at offset 0 so [name] in inline asm resolves to the low float. */
    float4 va = {1.0, 2.0, 3.0, 4.0};
    float4 vb = {5.0, 6.0, 7.0, 8.0};
    float4 vc;
    asm {
        movups xmm0, [va]
        movups xmm1, [vb]
        addps xmm0, xmm1
        movups [vc], xmm0
    }
    /* Expect {6, 8, 10, 12} — check lane 0 via vc.x.  CupidC can't yet
     * compare floats with !=, so bit-compare instead: 6.0 -> 0x40C00000. */
    float vc_x = vc.x;
    if (*(int*)&vc_x != 0x40C00000) {
        serial_printf("[feature16] FAIL addps lane0: got=0x%x expected=0x40C00000\n",
                      *(int*)&vc_x);
        ok = 0;
    }

    if (ok) serial_printf("PASS feature16_asm_fpu\n");
    else    serial_printf("FAIL feature16_asm_fpu\n");
    if (ok) println("PASS feature16_asm_fpu");
    else    println("FAIL feature16_asm_fpu");
}
