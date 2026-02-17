; asm_compat_reserve.asm - NASM-style reserve compatibility demo
; Run: as demos/asm_compat_reserve.asm

section .data
    msg1    db "ASM reserve aliases (rb/rw/rd/reserve):", 10, 0
    msg2    db "buf->wbuf bytes = ", 0
    msg3    db "wbuf->dbuf bytes = ", 0
    msg4    db "dbuf->tail bytes = ", 0
    nl      db 10, 0

    ; Inline label + directive syntax support
    buf:    rb 32
    wbuf:   rw 4          ; 8 bytes
    dbuf:   rd 3          ; 12 bytes
    tail    reserve 5     ; reserve alias = resb 5

section .text

main:
    push msg1
    call print
    add  esp, 4

    push msg2
    call print
    add  esp, 4
    mov  eax, wbuf
    sub  eax, buf
    push eax
    call print_int
    add  esp, 4
    push nl
    call print
    add  esp, 4

    push msg3
    call print
    add  esp, 4
    mov  eax, dbuf
    sub  eax, wbuf
    push eax
    call print_int
    add  esp, 4
    push nl
    call print
    add  esp, 4

    push msg4
    call print
    add  esp, 4
    mov  eax, tail
    sub  eax, dbuf
    push eax
    call print_int
    add  esp, 4
    push nl
    call print
    add  esp, 4

    ret
