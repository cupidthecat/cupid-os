; stack.asm — Stack frame and function call test
; Tests push/pop, call/ret, stack frames, and prints results.
; Run: as demos/stack.asm

section .data
    msg_add db "add_numbers(15, 27) = ", 0
    msg_mul db "multiply(6, 7) = ", 0
    newline db 10, 0

section .text

; add_numbers(a, b) — returns a + b in eax
add_numbers:
    push ebp
    mov  ebp, esp
    mov  eax, [ebp+8]    ; first arg
    add  eax, [ebp+12]   ; second arg
    pop  ebp
    ret

; multiply(a, b) — returns a * b in eax (repeated addition)
multiply:
    push ebp
    mov  ebp, esp
    push ebx
    push ecx

    mov  eax, 0           ; result
    mov  ebx, [ebp+8]     ; a
    mov  ecx, [ebp+12]    ; b

.mul_loop:
    cmp  ecx, 0
    je   .mul_done
    add  eax, ebx
    dec  ecx
    jmp  .mul_loop

.mul_done:
    pop  ecx
    pop  ebx
    pop  ebp
    ret

main:
    ; Test add_numbers(15, 27) = 42
    push msg_add
    call print
    add  esp, 4

    push 27
    push 15
    call add_numbers
    add  esp, 8

    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; Test multiply(6, 7) = 42
    push msg_mul
    call print
    add  esp, 4

    push 7
    push 6
    call multiply
    add  esp, 8

    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ret
