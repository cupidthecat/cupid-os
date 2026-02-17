; parity_gfx2d.asm - gfx2d parity bindings smoke test for CupidASM
; Run: as demos/parity_gfx2d.asm

section .data
    msg_nogui db "gfx2d demo: GUI mode required", 10, 0
    title     db "ASM gfx2d parity", 0

section .text

main:
    call is_gui_mode
    cmp  eax, 0
    je   .no_gui

    call gfx2d_init

    push 0x00101020
    call gfx2d_clear
    add  esp, 4

    ; panel + shapes
    push 0x00303050
    push 180
    push 280
    push 30
    push 20
    call gfx2d_rect_fill
    add  esp, 20

    push 0x00FFD060
    push 32
    push 120
    push 80
    call gfx2d_circle_fill
    add  esp, 16

    push 0x0060D0FF
    push 10
    push 70
    push 230
    push 50
    push 20
    call gfx2d_rect_round_fill
    add  esp, 24

    ; text
    push 1
    push 0x00FFFFFF
    push title
    push 40
    push 28
    call gfx2d_text
    add  esp, 20

    call gfx2d_flip

    push 1200
    call sleep_ms
    add  esp, 4

    ret

.no_gui:
    push msg_nogui
    call print
    add  esp, 4
    ret
