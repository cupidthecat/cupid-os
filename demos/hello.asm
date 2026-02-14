; hello.asm — Hello World for CupidASM (JIT mode)
; Calls kernel print() directly via pre-registered bindings.
; Run with: as demos/hello.asm

section .data
    msg db "Hello from CupidASM!", 10, 0

section .text

main:
    push msg
    call print
    add  esp, 4
    ret
