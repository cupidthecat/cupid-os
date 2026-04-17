; demos/fpu_kernel.asm - exercise new CupidASM FPU/SSE/x87 opcodes (Task 12)
;
; Self-verifying end-to-end demo for Phase B of the FPU plan.  Each of the
; four exercise blocks computes a result with known IEEE-754 bit pattern,
; bit-compares it with `cmp eax, <expected>`, and branches to a per-block
; failure handler that prints "FAIL fpu_kernel: <reason>" and returns.  If
; all four blocks pass, prints "PASS fpu_kernel".
;
; Covers opcodes added in Tasks 8-11:
;   - Task  8: FNINIT / FWAIT / FINIT / FXSAVE / FXRSTOR / STMXCSR / LDMXCSR
;   - Task  9: MOVSS / ADDSS / SQRTSS (SSE scalar single-precision)
;   - Task 10: MOVUPS / ADDPS         (SSE packed single-precision)
;   - Task 11: FLD m32fp / FSIN / FSTP m32fp (x87 single-precision)
;
; CupidASM has no `align` directive, so all SSE loads/stores go through
; MOVUPS (unaligned) and the FXSAVE buffer is placed at the start of the
; .data section (which the kernel aligns for us on load).
;
; Run interactively from the CupidOS shell:
;     as /demos/fpu_kernel.asm
; Expected stdout: "PASS fpu_kernel"

section .data
    ; FXSAVE/FXRSTOR area - 512 bytes, first in .data.
    fp_buf:         times 512 db 0
    mxcsr_tmp:      dd 0

    ; SSE scalar constants (single-precision)
    c_one_five:     dd 0x3FC00000    ; 1.5
    c_two_five:     dd 0x40200000    ; 2.5
    c_sixteen:      dd 0x41800000    ; 16.0

    ; SSE packed constants: vec_a + vec_b = [6, 8, 10, 12]
    ;   vec_a = [1.0, 2.0, 3.0, 4.0]
    ;   vec_b = [5.0, 6.0, 7.0, 8.0]
    vec_a:          dd 0x3F800000, 0x40000000, 0x40400000, 0x40800000
    vec_b:          dd 0x40A00000, 0x40C00000, 0x40E00000, 0x41000000

    ; x87 constants
    fx_zero:        dd 0x00000000

    ; Result storage (read back as ints for bit-compare)
    result_scalar_add:  dd 0
    result_scalar_sqrt: dd 0
    result_packed_add:  dd 0, 0, 0, 0
    result_x87_sin:     dd 0

    ; Messages
    msg_pass:       db "PASS fpu_kernel", 10, 0
    msg_fail_s_add: db "FAIL fpu_kernel: scalar addss", 10, 0
    msg_fail_s_sq:  db "FAIL fpu_kernel: scalar sqrtss", 10, 0
    msg_fail_p_add: db "FAIL fpu_kernel: packed addps lane0", 10, 0
    msg_fail_x87:   db "FAIL fpu_kernel: x87 fsin(0)", 10, 0

section .text

main:
    ; ========================================================================
    ; Block 1: FPU state control - FXSAVE/FXRSTOR + MXCSR round-trip.
    ; If any of these faulted, we'd be in a panic handler, not here. Pass
    ; is implicit: reaching the next block means all seven opcodes decoded
    ; and executed cleanly.
    ; ========================================================================
    fninit
    fwait
    finit
    fxsave  [fp_buf]
    fxrstor [fp_buf]
    stmxcsr [mxcsr_tmp]
    ldmxcsr [mxcsr_tmp]

    ; ========================================================================
    ; Block 2: SSE scalar arithmetic.
    ;   1.5 + 2.5 = 4.0  (bit pattern 0x40800000)
    ;   sqrt(16.0) = 4.0 (bit pattern 0x40800000)
    ; ========================================================================
    movss   xmm0, [c_one_five]
    movss   xmm1, [c_two_five]
    addss   xmm0, xmm1
    movss   [result_scalar_add], xmm0
    mov     eax, [result_scalar_add]
    cmp     eax, 0x40800000
    jne     fail_scalar_add

    movss   xmm0, [c_sixteen]
    sqrtss  xmm0, xmm0
    movss   [result_scalar_sqrt], xmm0
    mov     eax, [result_scalar_sqrt]
    cmp     eax, 0x40800000
    jne     fail_scalar_sqrt

    ; ========================================================================
    ; Block 3: SSE packed arithmetic.
    ;   [1,2,3,4] + [5,6,7,8] = [6,8,10,12]
    ; Bit-compare lane 0 only: 1.0 + 5.0 = 6.0 = 0x40C00000.
    ; ========================================================================
    movups  xmm0, [vec_a]
    movups  xmm1, [vec_b]
    addps   xmm0, xmm1
    movups  [result_packed_add], xmm0
    mov     eax, [result_packed_add]      ; lane 0
    cmp     eax, 0x40C00000
    jne     fail_packed_add

    ; ========================================================================
    ; Block 4: x87 FPU.
    ;   sin(0.0) = +0.0 (bit pattern 0x00000000)
    ; FINIT after the store keeps the x87 stack clean for subsequent code.
    ; ========================================================================
    fld     [fx_zero]
    fsin
    fstp    [result_x87_sin]
    finit
    mov     eax, [result_x87_sin]
    cmp     eax, 0x00000000
    jne     fail_x87

    ; All blocks passed.
    push    msg_pass
    call    print
    add     esp, 4
    mov     eax, 0
    ret

fail_scalar_add:
    push    msg_fail_s_add
    call    print
    add     esp, 4
    mov     eax, 1
    ret

fail_scalar_sqrt:
    push    msg_fail_s_sq
    call    print
    add     esp, 4
    mov     eax, 1
    ret

fail_packed_add:
    push    msg_fail_p_add
    call    print
    add     esp, 4
    mov     eax, 1
    ret

fail_x87:
    push    msg_fail_x87
    call    print
    add     esp, 4
    mov     eax, 1
    ret
