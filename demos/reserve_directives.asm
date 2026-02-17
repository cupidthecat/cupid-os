; reserve_directives.asm - Demonstrate resw/resd support
; Run: as demos/reserve_directives.asm

section .data
    msg_w    db "resw bytes = ", 0
    msg_d    db "resd bytes = ", 0
    newline  db 10, 0

    wbuf     resw 4      ; 8 bytes
    dbuf     resd 3      ; 12 bytes
    endmark  db 0

section .text

main:
    ; Print size of wbuf region: dbuf - wbuf = 8
    push msg_w
    call print
    add  esp, 4

    mov  eax, dbuf
    sub  eax, wbuf
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ; Print size of dbuf region: endmark - dbuf = 12
    push msg_d
    call print
    add  esp, 4

    mov  eax, endmark
    sub  eax, dbuf
    push eax
    call print_int
    add  esp, 4
    push newline
    call print
    add  esp, 4

    ret
