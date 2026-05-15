; parity_gfx2d.asm - gfx2d parity bindings smoke test for CupidASM
; Run: as demos/parity_gfx2d.asm

section .data
    msg_nogui db "gfx2d demo: GUI mode required", 10, 0
    msg_alloc db "FAIL parity_gfx2d: surface alloc", 10, 0
    msg_pixel db "FAIL parity_gfx2d: pixel verify", 10, 0
    msg_ok    db "PASS parity_gfx2d", 10, 0
    title     db "ASM gfx2d parity", 0
    surf      dd 0

section .text

main:
    call is_gui_mode
    cmp  eax, 0
    je   .no_gui

    call gfx2d_init

    push 96
    push 128
    call gfx2d_surface_alloc
    add  esp, 8
    cmp  eax, 0
    jl   .alloc_fail
    mov  [surf], eax

    push eax
    call gfx2d_surface_set_active
    add  esp, 4

    push 0x00101020
    call gfx2d_clear
    add  esp, 4

    ; Draw into the offscreen surface. This keeps the parity test from
    ; taking over the live desktop framebuffer.
    push 0x00303050
    push 40
    push 90
    push 30
    push 20
    call gfx2d_rect_fill
    add  esp, 20

    push 0x00FFD060
    push 16
    push 58
    push 64
    call gfx2d_circle_fill
    add  esp, 16

    push 0x0060D0FF
    push 6
    push 18
    push 78
    push 66
    push 24
    call gfx2d_rect_round_fill
    add  esp, 24

    ; text
    push 1
    push 0x00FFFFFF
    push title
    push 8
    push 8
    call gfx2d_text
    add  esp, 20

    push 31
    push 21
    call gfx2d_getpixel
    add  esp, 8
    cmp  eax, 0x00303050
    jne  .pixel_fail

    call gfx2d_surface_unset_active
    mov  eax, [surf]
    push eax
    call gfx2d_surface_free
    add  esp, 4

    push msg_ok
    call print
    add  esp, 4
    ret

.alloc_fail:
    push msg_alloc
    call print
    add  esp, 4
    ret

.pixel_fail:
    call gfx2d_surface_unset_active
    mov  eax, [surf]
    push eax
    call gfx2d_surface_free
    add  esp, 4
    push msg_pixel
    call print
    add  esp, 4
    ret

.no_gui:
    push msg_nogui
    call print
    add  esp, 4
    ret
