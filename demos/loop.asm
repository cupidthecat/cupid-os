; loop.asm - Loop and conditional branch test
; Computes sum of 1..100 = 5050 and prints it.
; Run: as demos/loop.asm

section .data
    msg db "Sum of 1..100 = ", 0
    newline db 10, 0

section .text

main:
    mov eax, 0            ; accumulator
    mov ecx, 1            ; counter

.sum_loop:
    add eax, ecx          ; sum += counter
    inc ecx
    cmp ecx, 101
    jl  .sum_loop         ; while counter < 101

    ; eax = 5050 - print it
    push eax              ; save result
    push msg
    call print
    add  esp, 4
    call print_int        ; arg already on stack
    add  esp, 4
    push newline
    call print
    add  esp, 4
    ret
