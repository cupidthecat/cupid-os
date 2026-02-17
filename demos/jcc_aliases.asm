; jcc_aliases.asm - Conditional jump alias demo
; Exercises aliases like jnc/jc/jpe/jpo/jnl/jng.
; Run: as demos/jcc_aliases.asm

section .data
    ok_msg   db "JCC alias checks passed", 10, 0
    bad_msg  db "JCC alias checks failed", 10, 0

section .text

main:
    clc
    jnc .carry_clear
    jmp .fail

.carry_clear:
    stc
    jc  .carry_set
    jmp .fail

.carry_set:
    xor eax, eax
    cmp eax, 0
    jpe .parity_even
    jmp .fail

.parity_even:
    mov eax, 5
    cmp eax, 2
    jnl .not_less
    jmp .fail

.not_less:
    mov eax, 1
    cmp eax, 2
    jng .not_greater
    jmp .fail

.not_greater:
    mov eax, 1
    cmp eax, 2
    jpo .parity_odd
    jmp .fail

.parity_odd:
    push ok_msg
    call print
    add  esp, 4
    ret

.fail:
    push bad_msg
    call print
    add  esp, 4
    ret
