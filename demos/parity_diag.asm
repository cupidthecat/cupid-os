; parity_diag.asm — diagnostics/debug parity bindings test for CupidASM
; Run: as demos/parity_diag.asm

section .data
    msg_hdr   db "[ASM parity diag]", 10, 0
    fmt_line  db "format test: %u 0x%x %s", 0
    fmt_txt   db "ok", 0
    msg_log   db " log-level=", 0
    nl        db 10, 0

section .text

main:
    push msg_hdr
    call print
    add  esp, 4

    ; variadic print builtin smoke test
    push fmt_txt
    push 0x2A
    push 42
    push fmt_line
    call __cc_PrintLine
    add  esp, 16

    ; print one hex byte
    push 255
    call print_hex_byte
    add  esp, 4
    push nl
    call print
    add  esp, 4

    ; query + print current serial log level name
    push msg_log
    call print
    add  esp, 4
    call get_log_level_name
    push eax
    call print
    add  esp, 4
    push nl
    call print
    add  esp, 4

    ; non-fatal diagnostics helpers
    call print_log_buffer
    call heap_check_integrity
    push 0
    call detect_memory_leaks
    add  esp, 4

    ; register/stack dump helpers
    call dump_registers
    call dump_stack_trace

    ret
