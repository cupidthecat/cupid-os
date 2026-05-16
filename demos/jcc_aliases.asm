; jcc_aliases.asm - Conditional jump alias demo
; Exercises aliases like jnc/jc/jpe/jpo/jnl/jng.
; Run: as demos/jcc_aliases.asm

section .data
    ok_msg   db "JCC alias checks passed", 10, 0
    bad_msg  db "JCC alias checks failed", 10, 0
    fail_jnc db "FAIL jcc_aliases: jnc", 10, 0
    fail_jc  db "FAIL jcc_aliases: jc", 10, 0
    fail_jpe db "FAIL jcc_aliases: jpe", 10, 0
    fail_jnl db "FAIL jcc_aliases: jnl", 10, 0
    fail_jng db "FAIL jcc_aliases: jng", 10, 0
    fail_jpo db "FAIL jcc_aliases: jpo", 10, 0

section .text

main:
    clc
    jnc .carry_clear
    push fail_jnc
    jmp .fail

.carry_clear:
    stc
    jc  .carry_set
    push fail_jc
    jmp .fail

.carry_set:
    xor eax, eax
    cmp eax, 0
    jpe .parity_even
    push fail_jpe
    jmp .fail

.parity_even:
    mov eax, 5
    cmp eax, 2
    jnl .not_less
    push fail_jnl
    jmp .fail

.not_less:
    mov eax, 1
    cmp eax, 2
    jng .not_greater
    push fail_jng
    jmp .fail

.not_greater:
    mov eax, 1
    cmp eax, 0
    jpo .parity_odd
    push fail_jpo
    jmp .fail

.parity_odd:
    push ok_msg
    call print
    add  esp, 4
    ret

.fail:
    call print
    add  esp, 4
    push bad_msg
    call print
    add  esp, 4
    ret
