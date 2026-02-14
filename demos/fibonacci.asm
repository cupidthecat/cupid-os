    ; fibonacci.asm — Compute and print first 15 Fibonacci numbers
; Run: as demos/fibonacci.asm

section .data
    msg   db "Fibonacci sequence:", 10, 0
    space db " ", 0
    newline db 10, 0

section .text

main:
    push msg
    call print
    add  esp, 4

    mov  ecx, 15            ; how many numbers
    mov  eax, 0              ; fib(n-2)
    mov  ebx, 1              ; fib(n-1)

.fib_loop:
    cmp  ecx, 0
    je   .fib_done

    ; Print current fib number (eax).
    ; Caller-saved regs (eax/ecx/edx) may be clobbered by calls.
    push ecx                 ; save counter
    push ebx                 ; save fib(n-1)
    push eax                 ; save fib(n-2)
    push eax                 ; arg: current number
    call print_int
    add  esp, 4
    push space
    call print
    add  esp, 4
    pop  eax                 ; restore fib(n-2)
    pop  ebx                 ; restore fib(n-1)
    pop  ecx                 ; restore counter

    ; Advance: next = eax + ebx
    mov  edx, eax
    add  edx, ebx
    mov  eax, ebx
    mov  ebx, edx

    dec  ecx
    jmp  .fib_loop

.fib_done:
    push newline
    call print
    add  esp, 4
    ret
