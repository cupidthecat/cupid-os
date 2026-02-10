; factorial.asm — Recursive factorial
; Computes and prints factorial(1) through factorial(10).
; Run: as demos/factorial.asm

section .data
    msg_hdr db "Factorials:", 10, 0
    msg_eq  db "! = ", 0
    newline db 10, 0

section .text

; factorial(n) — returns n! in EAX
; Argument: n passed on the stack
factorial:
    push ebp
    mov  ebp, esp

    mov  eax, [ebp+8]     ; n
    cmp  eax, 1
    jle  .base_case

    ; Recursive case: n * factorial(n-1)
    dec  eax
    push eax
    call factorial
    add  esp, 4

    ; eax = factorial(n-1), multiply by n
    mov  ebx, [ebp+8]     ; original n
    mul  ebx               ; eax = eax * ebx

    pop  ebp
    ret

.base_case:
    mov  eax, 1
    pop  ebp
    ret

main:
    push msg_hdr
    call print
    add  esp, 4

    mov  esi, 1            ; counter: 1..10

.print_loop:
    cmp  esi, 11
    je   .done

    ; Print "N! = "
    push esi
    call print_int
    add  esp, 4
    push msg_eq
    call print
    add  esp, 4

    ; Compute factorial(esi)
    push esi
    call factorial
    add  esp, 4

    ; Print result
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    inc  esi
    jmp  .print_loop

.done:
    ret
