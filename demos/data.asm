; data.asm - Data section test
; Tests db, dd directives, data references, and prints results.
; Run: as demos/data.asm

section .data
    greeting db "Hello from CupidASM!", 10, 0
    numbers  dd 10, 20, 30, 40, 50
    count    dd 5
    msg_sum  db "Array sum = ", 0
    msg_len  db "String length = ", 0
    newline  db 10, 0

section .text

main:
    ; Print greeting
    push greeting
    call print
    add  esp, 4

    ; Print string length of greeting
    push msg_len
    call print
    add  esp, 4
    push greeting
    call strlen
    add  esp, 4
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; Sum the numbers array
    mov  ecx, [count]      ; loop counter = 5
    mov  esi, numbers      ; pointer to array
    mov  eax, 0            ; accumulator

.loop:
    cmp  ecx, 0
    je   .print_sum
    add  eax, [esi]        ; add current element
    add  esi, 4            ; next dword
    dec  ecx
    jmp  .loop

.print_sum:
    push eax               ; save sum
    push msg_sum
    call print
    add  esp, 4
    call print_int         ; sum still on stack
    add  esp, 4
    push newline
    call print
    add  esp, 4
    ret
