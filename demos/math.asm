; math.asm — Arithmetic operations test
; Tests add, sub, mul, div, shifts, and logic ops.
; Prints each result using kernel print_int.
; Run: as demos/math.asm

section .data
    msg_add db "10 + 20 = ", 0
    msg_sub db "30 - 5  = ", 0
    msg_shl db "25 << 2 = ", 0
    msg_div db "100 / 10 = ", 0
    msg_and db "0xFF & 0x0F = ", 0
    msg_or  db "0x0F | 0xF0 = ", 0
    newline db 10, 0
    msg_done db "Math tests complete!", 10, 0

section .text

main:
    ; --- add ---
    push msg_add
    call print
    add  esp, 4
    mov  eax, 10
    mov  ebx, 20
    add  eax, ebx            ; eax = 30
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; --- sub ---
    push msg_sub
    call print
    add  esp, 4
    mov  eax, 30
    sub  eax, 5              ; eax = 25
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; --- shl ---
    push msg_shl
    call print
    add  esp, 4
    mov  eax, 25
    shl  eax, 2              ; eax = 100
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; --- div ---
    push msg_div
    call print
    add  esp, 4
    mov  eax, 100
    xor  edx, edx
    mov  ecx, 10
    div  ecx                 ; eax = 10
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; --- and ---
    push msg_and
    call print
    add  esp, 4
    mov  eax, 0xFF
    and  eax, 0x0F           ; eax = 0x0F
    push eax
    call print_hex
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; --- or ---
    push msg_or
    call print
    add  esp, 4
    mov  eax, 0x0F
    or   eax, 0xF0           ; eax = 0xFF
    push eax
    call print_hex
    add  esp, 4
    push newline
    call print
    add  esp, 4

    push msg_done
    call print
    add  esp, 4
    ret
