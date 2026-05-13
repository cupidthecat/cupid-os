; demos/simd_blur.asm - 1D 3-tap box blur across 16 floats
; Demonstrates MOVUPS + ADDPS + MULPS packed SSE operations from Phase B.
;   out[i] = (in[i-1] + in[i] + in[i+1]) / 3   for i = 1..16
; Implemented as 4 batches of 4 lanes; DIVPS is avoided by multiplying by
; a broadcast 1/3 constant (MULPS), which also exercises packed multiply.
; Run interactively:  as /demos/simd_blur.asm
; Expected output:    "simd_blur done"

section .data
    ; Broadcast 1/3 for packed multiply (replaces DIVPS by 3.0).
    third:       dd 0x3EAAAAAB, 0x3EAAAAAB, 0x3EAAAAAB, 0x3EAAAAAB

    ; 20 consecutive floats of 1.0 - the blur window reads [i-1..i+1]
    ; so we need one guard element on each side of the 16-lane stride.
    pixels:      dd 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
                 dd 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
                 dd 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
                 dd 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000
                 dd 0x3F800000, 0x3F800000, 0x3F800000, 0x3F800000

    ; Output - 16 floats, initialised to zero.
    output:      dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

    msg_done:    db "simd_blur done", 10, 0

section .text

main:
    ; esi -> &pixels[1] so [esi-4]/[esi]/[esi+4] are the three taps.
    mov    esi, pixels
    add    esi, 4
    mov    edi, output
    mov    ecx, 4                   ; 4 batches of 4 lanes = 16 outputs
    movups xmm3, [third]            ; broadcast 1/3 into all four lanes

.loop:
    movups xmm0, [esi - 4]          ; in[i-1 .. i+2]
    movups xmm1, [esi]              ; in[i   .. i+3]
    movups xmm2, [esi + 4]          ; in[i+1 .. i+4]
    addps  xmm0, xmm1
    addps  xmm0, xmm2               ; packed sum of the three taps
    mulps  xmm0, xmm3               ; * (1/3) per lane
    movups [edi], xmm0
    add    esi, 16                  ; advance 4 floats (16 bytes)
    add    edi, 16
    dec    ecx
    jnz    .loop

    push   msg_done
    call   print
    add    esp, 4
    mov    eax, 0
    ret
